// Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

// Copyright (c) 2015 CEF Python, see the Authors file.
// All rights reserved. Licensed under BSD 3-clause license.
// Project website: https://github.com/cztomczak/cefpython

// Some code was copied from here:
// java-cef: src/master/native/util_mac.mm
// upstream cef: src/tests/ceftests/run_all_unittests_mac.mm
// upstream cef: src/tests/cefclient/cefclient_mac.mm
// upstream cef: src/tests/cefsimple/cefsimple_mac.mm

#import "util_mac.h"
#import <Cocoa/Cocoa.h>
#include <objc/runtime.h>
#include "include/cef_app.h"
#include "include/cef_application_mac.h"
#include "include/cef_browser.h"
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/base/cef_logging.h"
#include "include/wrapper/cef_closure_task.h"

extern "C" {
// Public Objective-C runtime functions, declared in <objc/objc-internal.h>
// which is not a public header.
void* objc_autoreleasePoolPush(void);
void objc_autoreleasePoolPop(void* pool);
}

namespace {

// static NSAutoreleasePool* g_autopool = nil;
BOOL g_handling_send_event = false;

}  // namespace

// Add the necessary CefAppProtocol functionality to NSApplication
// using categories and swizzling (Issue #442, Issue #156).
@interface NSApplication (CEFPythonApplication)<CefAppProtocol>

- (BOOL)isHandlingSendEvent;
- (void)setHandlingSendEvent:(BOOL)handlingSendEvent;
- (void)_swizzled_sendEvent:(NSEvent*)event;
- (void)_swizzled_terminate:(id)sender;

@end

@implementation NSApplication (CEFPythonApplication)

// This selector is called very early during the application initialization.
+ (void)load {
  // Swap NSApplication::sendEvent with _swizzled_sendEvent.
  Method original = class_getInstanceMethod(self, @selector(sendEvent));
  Method swizzled =
      class_getInstanceMethod(self, @selector(_swizzled_sendEvent));
  method_exchangeImplementations(original, swizzled);

  Method originalTerm = class_getInstanceMethod(self, @selector(terminate:));
  Method swizzledTerm =
      class_getInstanceMethod(self, @selector(_swizzled_terminate:));
  method_exchangeImplementations(originalTerm, swizzledTerm);
}

- (BOOL)isHandlingSendEvent {
  return g_handling_send_event;
}

- (void)setHandlingSendEvent:(BOOL)handlingSendEvent {
  g_handling_send_event = handlingSendEvent;
}

- (void)_swizzled_sendEvent:(NSEvent*)event {
  CefScopedSendingEvent sendingEventScoper;
  // Calls NSApplication::sendEvent due to the swizzling.
  [self _swizzled_sendEvent:event];
}

- (void)_swizzled_terminate:(id)sender {
  [self _swizzled_terminate:sender];
}

@end

void MacInitialize() {
    // OFF: it's causing a crash during shutdown release
    // g_autopool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];

    // CEF 130+ builds the MachPortRendezvousServer bootstrap service name as
    //   BaseBundleID() + ".MachPortRendezvousServer." + pid
    // When the process has no app bundle (e.g. a bare Python script),
    // BaseBundleID() returns "" and the name starts with ".", which
    // bootstrap_register rejects as invalid.  All renderer subprocesses then
    // crash with "Unknown service name" (1102) on bootstrap_look_up.
    // Inject a synthetic CFBundleIdentifier before CefInitialize() so the
    // service name becomes a valid reverse-DNS label ("org.cefpython...").
    CFBundleRef mainBundle = CFBundleGetMainBundle();
    if (mainBundle) {
        CFStringRef bundleID = CFBundleGetIdentifier(mainBundle);
        if (!bundleID || CFStringGetLength(bundleID) == 0) {
            // CFBundleGetInfoDictionary returns the bundle's internal mutable
            // dictionary; casting to CFMutableDictionaryRef is safe here.
            CFMutableDictionaryRef infoDict =
                (CFMutableDictionaryRef)CFBundleGetInfoDictionary(mainBundle);
            if (infoDict) {
                CFDictionarySetValue(infoDict,
                                     CFSTR("CFBundleIdentifier"),
                                     CFSTR("org.cefpython"));
            }
        }
    }
}

void MacShutdown() {
    // OFF: it's causing a crash during shutdown release
    // [g_autopool release];
}

std::string MacGetMainBundlePath() {
    @autoreleasepool {
        NSBundle* mainBundle = [NSBundle mainBundle];
        if (!mainBundle) {
            return std::string();
        }

        NSString* bundlePath = [mainBundle bundlePath];
        if (!bundlePath || ![bundlePath isAbsolutePath] ||
            ![bundlePath hasSuffix:@".app"]) {
            return std::string();
        }

        const char* fileSystemPath = [bundlePath fileSystemRepresentation];
        return fileSystemPath ? std::string(fileSystemPath) : std::string();
    }
}

void MacSetWindowTitle(CefRefPtr<CefBrowser> browser, char* title) {
    @autoreleasepool {
        NSView* view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(browser->GetHost()->GetWindowHandle());
        NSString* nstitle = [NSString stringWithFormat:@"%s" , title];
        view.window.title = nstitle;
    }
}

void* MacAutoreleasePoolPush() {
    return objc_autoreleasePoolPush();
}

void MacAutoreleasePoolPop(void* pool) {
    if (pool) {
        objc_autoreleasePoolPop(pool);
    }
}

namespace {

// How often and how long to wait for CEF to close its own window.
const int kDetachRetryDelayMs = 100;
const int kDetachMaxAttempts = 100;

void DetachBrowserView(CefRefPtr<CefBrowser> browser, bool cef_window,
                       int attempt) {
    // OnBeforeClose() was already called, e.g. the window close that CEF
    // sent after DoClose() destroyed the view first.
    if (!browser->IsValid()) {
        return;
    }
    @autoreleasepool {
        NSView* view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(
                browser->GetHost()->GetWindowHandle());
        // Only ever detach CEF's own browser view, never an application view.
        Class host_view_class = NSClassFromString(@"CefBrowserHostView");
        if (!view || !host_view_class
                || ![view isKindOfClass:host_view_class]) {
            LOG(WARNING) << "[Browser process] DetachBrowserView():"
                            " CefBrowserHostView not found, browser close"
                            " may not complete";
            return;
        }
        if (![view superview]) {
            return;
        }
        if (cef_window) {
            // The window was created by CEF. Wait until CEF has closed it
            // and has run its cleanup, which clears the window delegate.
            // Destroying the browser earlier frees the delegate while the
            // window still uses it, e.g. in its close animation (crash).
            NSWindow* window = [view window];
            Class delegate_class = NSClassFromString(@"CefWindowDelegate");
            if (window && ([window isVisible]
                    || (delegate_class && [[window delegate]
                                           isKindOfClass:delegate_class]))) {
                if (attempt >= kDetachMaxAttempts) {
                    LOG(WARNING) << "[Browser process] DetachBrowserView():"
                                    " CEF didn't close the browser window,"
                                    " browser close may not complete";
                    return;
                }
                CefPostDelayedTask(TID_UI, CefCreateClosureTask(
                        base::BindOnce(&DetachBrowserView, browser,
                                       cef_window, attempt + 1)),
                        kDetachRetryDelayMs);
                return;
            }
        }
        LOG(INFO) << "[Browser process] DetachBrowserView(): detach browser"
                     " view to complete the browser close";
        // When the superview releases the view, the view is deallocated and
        // CEF destroys the browser, which calls OnBeforeClose().
        [view removeFromSuperview];
    }
}

}  // namespace

void MacScheduleBrowserViewDetach(CefRefPtr<CefBrowser> browser) {
    // CEF destroys a windowed browser only when its browser view
    // (CefBrowserHostView) is deallocated, normally when the window is
    // deallocated. On macOS with Alloy style the closed window is never
    // deallocated (upstream CEF issues #3469 and #3810), so OnBeforeClose()
    // was never called and e.g. hello_world.py's MessageLoop() never
    // returned. DoClose() returning false commits to closing the browser, so
    // complete the close by detaching the browser view. Run it as a task so
    // that the window close that CEF sends after DoClose() is handled first.
    if (browser->GetHost()->IsWindowRenderingDisabled()) {
        // Off-screen rendering has no browser view, CEF destroys the browser
        // right after DoClose().
        return;
    }
    // Is the browser in a window that CEF created (e.g. hello_world.py) or
    // embedded in an application window (e.g. qt.py, SetAsChild())?
    bool cef_window = false;
    @autoreleasepool {
        NSView* view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(
                browser->GetHost()->GetWindowHandle());
        Class delegate_class = NSClassFromString(@"CefWindowDelegate");
        cef_window = view && delegate_class
                && [[[view window] delegate] isKindOfClass:delegate_class];
    }
    CefPostTask(TID_UI, CefCreateClosureTask(
            base::BindOnce(&DetachBrowserView, browser, cef_window, 0)));
}
