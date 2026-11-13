// Copyright (c) 2016 CEF Python, see the Authors file.
// All rights reserved. Licensed under BSD 3-clause license.
// Project website: https://github.com/cztomczak/cefpython

// NOTE: This file is also used by "subprocess" and "libcefpythonapp"
//       targets during build.

#include "x11.h"
#include "util.h"
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/base/cef_logging.h"
#include "include/wrapper/cef_closure_task.h"

#include <X11/Xatom.h>
#include <unistd.h>

#include <algorithm>
#include <map>

namespace {

// Browser identifier -> the bridge window its browser window is in.
std::map<int, ::Window> g_bridge_windows;
// The bridge window of the browser being created, until OnAfterCreated.
::Window g_new_bridge_window = 0;

void DestroyBridgeWindowWhenEmpty(::Window bridge, int retries) {
    // CEF destroys its window over its own X connection, and may not have
    // sent that request yet. Destroying the bridge window first would destroy
    // CEF's window with it, and CEF's request would then fail.
    ::Display* xdisplay = cef_get_xdisplay();
    if (!xdisplay) {
        return;
    }
    ::Window root, parent, *children = nullptr;
    unsigned int count = 0;
    if (!XQueryTree(xdisplay, bridge, &root, &parent, &children, &count)) {
        // Destroyed together with its parent window
        return;
    }
    if (children) {
        XFree(children);
    }
    if (count && retries > 0) {
        CefPostDelayedTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                &DestroyBridgeWindowWhenEmpty, bridge, retries - 1)), 50);
        return;
    }
    XDestroyWindow(xdisplay, bridge);
    XFlush(xdisplay);
}

}  // namespace

int XErrorHandlerImpl(Display *display, XErrorEvent *event) {
    LOG(INFO) << ""
        << "X error received: "
        << "type " << event->type << ", "
        << "serial " << event->serial << ", "
        << "error_code " << static_cast<int>(event->error_code) << ", "
        << "request_code " << static_cast<int>(event->request_code) << ", "
        << "minor_code " << static_cast<int>(event->minor_code);
    return 0;
}

int XIOErrorHandlerImpl(Display *display) {
    return 0;
}

void InstallX11ErrorHandlers() {
    // Copied from upstream cefclient.
    // Install xlib error handlers so that the application won't be terminated
    // on non-fatal errors. Must be done after initializing GTK.
    LOG(INFO) << "Install X11 error handlers";
    XSetErrorHandler(XErrorHandlerImpl);
    XSetIOErrorHandler(XIOErrorHandlerImpl);
}

unsigned long GetX11DefaultVisualId(int screen_number) {
    REQUIRE_UI_THREAD();
    // See SetX11WindowBounds: NULL off the UI thread or on a non-X11 Ozone
    // backend.
    ::Display* xdisplay = cef_get_xdisplay();
    if (!xdisplay) {
        LOG(WARNING) << "GetX11DefaultVisualId: no X11 "
                        "display (wrong thread or non-X11 backend)";
        return 0;
    }
    if (screen_number < 0 || screen_number >= ScreenCount(xdisplay)) {
        LOG(WARNING) << "GetX11DefaultVisualId: invalid "
                        "screen number " << screen_number;
        return 0;
    }
    return XVisualIDFromVisual(DefaultVisual(xdisplay, screen_number));
}

bool SetX11DefaultVisualForGtkWidget(GtkWidget* widget) {
    // Puts |widget| on the X server's default visual (Issue #701), for
    // toolkits that have no API to set the visual, e.g. wxPython. Must be
    // called before the widget is realized.
    REQUIRE_UI_THREAD();
    if (!widget || gtk_widget_get_realized(widget)) {
        LOG(WARNING) << "SetX11DefaultVisualForGtkWidget: "
                        "invalid or already realized widget";
        return false;
    }
    GdkScreen* screen = gtk_widget_get_screen(widget);
    if (!screen || !GDK_IS_X11_SCREEN(screen)) {
        LOG(WARNING) << "SetX11DefaultVisualForGtkWidget: "
                        "widget is not on an X11 screen";
        return false;
    }
    unsigned long visual_id = GetX11DefaultVisualId(
            gdk_x11_screen_get_screen_number(GDK_X11_SCREEN(screen)));
    if (!visual_id) {
        // GetX11DefaultVisualId logged why
        return false;
    }
    GdkVisual* visual = gdk_x11_screen_lookup_visual(GDK_X11_SCREEN(screen),
                                                     visual_id);
    if (!visual) {
        LOG(WARNING) << "SetX11DefaultVisualForGtkWidget: "
                        "no GDK visual for X visual " << visual_id;
        return false;
    }
    gtk_widget_set_visual(widget, visual);
    return true;
}

void SetX11WindowBounds(CefRefPtr<CefBrowser> browser,
                        int x, int y, int width, int height) {
    REQUIRE_UI_THREAD();
    // xwindow is 0 for windowless (OSR) browsers - nothing to resize, and no
    // need to touch X11 at all, so check this first. Callers should not get
    // here for OSR (use WasResized() instead), so warn.
    ::Window xwindow = browser->GetHost()->GetWindowHandle();
    if (!xwindow) {
        LOG(WARNING) << "SetX11WindowBounds: no X11 window "
                        "handle (windowless browser?), skipping";
        return;
    }
    // cef_get_xdisplay() is NULL off the UI thread (cef_types_linux.h; the
    // assert above is compiled out in release builds) and on any thread when
    // the app selects a non-X11 Ozone backend with no X server.
    // XConfigureWindow(NULL, ...) segfaults (verified), so no-op instead.
    ::Display* xdisplay = cef_get_xdisplay();
    if (!xdisplay) {
        LOG(INFO) << "SetX11WindowBounds: no X11 display "
                     "(wrong thread or non-X11 backend), skipping";
        return;
    }
    XWindowChanges changes = {0};
    changes.x = x;
    changes.y = y;
    changes.width = static_cast<int>(width);
    changes.height = static_cast<int>(height);
    auto bridge = g_bridge_windows.find(browser->GetIdentifier());
    if (bridge != g_bridge_windows.end()) {
        XConfigureWindow(xdisplay, bridge->second,
                         CWX | CWY | CWHeight | CWWidth, &changes);
        changes.x = 0;
        changes.y = 0;
    }
    XConfigureWindow(xdisplay, xwindow,
                     CWX | CWY | CWHeight | CWWidth, &changes);
    XFlush(xdisplay);
}

void SetX11WindowTitle(CefRefPtr<CefBrowser> browser, char* title) {
    REQUIRE_UI_THREAD();
    // xwindow is 0 for windowless (OSR) browsers - no title bar to set. The
    // only caller is for windowed browsers, so this indicates a bug.
    ::Window xwindow = browser->GetHost()->GetWindowHandle();
    if (!xwindow) {
        LOG(WARNING) << "SetX11WindowTitle: no X11 window "
                        "handle (windowless browser?), skipping";
        return;
    }
    // See SetX11WindowBounds: NULL off the UI thread or on a non-X11 Ozone
    // backend; XStoreName(NULL, ...) would segfault.
    ::Display* xdisplay = cef_get_xdisplay();
    if (!xdisplay) {
        LOG(INFO) << "SetX11WindowTitle: no X11 display "
                     "(wrong thread or non-X11 backend), skipping";
        return;
    }
    XStoreName(xdisplay, xwindow, title);
}

void CreateX11BridgeWindow(CefWindowInfo& window_info) {
    // CEF creates its window on the X server's default visual with the
    // colormap CopyFromParent, which X allows only inside a window with that
    // same visual. GTK 3 and SDL put their windows on other visuals, so
    // embed the browser into a window in between that has the default visual
    // and an explicit colormap.
    REQUIRE_UI_THREAD();
    ::Window parent = window_info.parent_window;
    if (!parent || window_info.windowless_rendering_enabled) {
        return;
    }
    // See SetX11WindowBounds: NULL off the UI thread or on a non-X11 Ozone
    // backend.
    ::Display* xdisplay = cef_get_xdisplay();
    if (!xdisplay) {
        LOG(WARNING) << "CreateX11BridgeWindow: no X11 display "
                        "(wrong thread or non-X11 backend)";
        return;
    }
    XWindowAttributes parent_attrs;
    if (!XGetWindowAttributes(xdisplay, parent, &parent_attrs)) {
        LOG(WARNING) << "CreateX11BridgeWindow: XGetWindowAttributes failed"
                        " for parent window " << parent;
        return;
    }
    Screen* screen = parent_attrs.screen;
    Visual* visual = DefaultVisualOfScreen(screen);
    if (XVisualIDFromVisual(parent_attrs.visual)
            == XVisualIDFromVisual(visual)) {
        return;
    }
    cef_rect_t& bounds = window_info.bounds;
    // The size CEF uses when none is given
    if (!bounds.width) {
        bounds.width = 800;
    }
    if (!bounds.height) {
        bounds.height = 600;
    }
    XSetWindowAttributes attrs = {};
    attrs.background_pixel = BlackPixelOfScreen(screen);
    attrs.border_pixel = 0;
    attrs.colormap = DefaultColormapOfScreen(screen);
    ::Window bridge = XCreateWindow(
            xdisplay, parent, bounds.x, bounds.y,
            std::max(bounds.width, 1), std::max(bounds.height, 1), 0,
            DefaultDepthOfScreen(screen), InputOutput, visual,
            CWBackPixel | CWBorderPixel | CWColormap, &attrs);
    // CEF looks for the application's top-level window, e.g. to set
    // XdndProxy on it, through the ancestors that have _NET_WM_PID.
    long pid = getpid();
    XChangeProperty(xdisplay, bridge,
                    XInternAtom(xdisplay, "_NET_WM_PID", False),
                    XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&pid), 1);
    XMapWindow(xdisplay, bridge);
    // CEF creates its window over another X connection, so the bridge
    // window must exist on the server before that.
    XSync(xdisplay, False);
    LOG(INFO) << std::hex << "Parent window 0x" << parent
              << " has visual 0x" << XVisualIDFromVisual(parent_attrs.visual)
              << ", embedding the browser into bridge window 0x" << bridge
              << " with the default visual 0x" << XVisualIDFromVisual(visual);
    window_info.parent_window = bridge;
    bounds.x = 0;
    bounds.y = 0;
    g_new_bridge_window = bridge;
}

void AttachX11BridgeWindow(CefRefPtr<CefBrowser> browser) {
    REQUIRE_UI_THREAD();
    if (g_new_bridge_window) {
        g_bridge_windows[browser->GetIdentifier()] = g_new_bridge_window;
        g_new_bridge_window = 0;
    }
}

void DestroyUnusedX11BridgeWindow() {
    REQUIRE_UI_THREAD();
    if (g_new_bridge_window) {
        DestroyBridgeWindowWhenEmpty(g_new_bridge_window, 0);
        g_new_bridge_window = 0;
    }
}

void DestroyX11BridgeWindow(CefRefPtr<CefBrowser> browser) {
    REQUIRE_UI_THREAD();
    auto bridge = g_bridge_windows.find(browser->GetIdentifier());
    if (bridge == g_bridge_windows.end()) {
        return;
    }
    ::Display* xdisplay = cef_get_xdisplay();
    if (xdisplay) {
        XUnmapWindow(xdisplay, bridge->second);
        XFlush(xdisplay);
    }
    DestroyBridgeWindowWhenEmpty(bridge->second, 20);
    g_bridge_windows.erase(bridge);
}

GtkWindow* CefBrowser_GetGtkWindow(CefRefPtr<CefBrowser> browser) {
  // TODO: Should return NULL when using the Views framework
  // -- REWRITTEN FOR CEF PYTHON USE CASE --
  //
  // Returns a GtkWindow* to use as the transient parent for GTK dialogs (the
  // file-chooser and print handlers); it is never used for browser embedding.
  //
  // Upstream cefclient gets this parent from its own top-level GtkWindow, via
  // RootWindow::GetForBrowser(id)->GetWindowHandle() - see GetWindow() in CEF's
  // tests/cefclient/browser/dialog_handler_gtk.cc. cefpython has no such
  // GtkWindow: the browser is embedded with CefWindowInfo::SetAsChild() into a
  // foreign toolkit's X11 window (Qt/GTK/wx/tk), not a cefclient RootWindow.
  // So instead we wrap the browser's own X11 window - CefBrowserHost::
  // GetWindowHandle(), which is the XID on Linux (include/cef_browser.h) - as a
  // GtkWindow using gtk_plug_new_for_display().
  //
  // The browser window is not a real GtkSocket, so GTK may log
  // "Can't create GtkPlug as child of non-GtkSocket" (harmless; the dialog
  // still works - see the note further down).
  //
  // X11 window handle
  ::Window xwindow = browser->GetHost()->GetWindowHandle();
  // X11 display
  ::Display* xdisplay = cef_get_xdisplay();
  // GDK display
  GdkDisplay* gdk_display = NULL;
  if (xdisplay) {
    // See if we can find GDK display using X11 display
    gdk_display = gdk_x11_lookup_xdisplay(xdisplay);
  }
  if (!gdk_display) {
    // If not then get the default display
    gdk_display = gdk_display_get_default();
  }
  if (!gdk_display) {
    // The tkinter_.py and hello_world.py examples do not use GTK
    // internally, so GTK wasn't yet initialized and must do it
    // now, so that display is available. Also must install X11
    // error handlers to avoid 'BadWindow' errors.
    // --
    // A similar code is in cefpython_app.cpp and it might already
    // been executed. If making changes here, make changes there
    // as well.
    LOG(INFO) << "Initialize GTK";
    gtk_init(0, NULL);
    InstallX11ErrorHandlers();
    // Now the display is available
    gdk_display = gdk_display_get_default();
  }
  // In kivy_.py example getting error message:
  // > Can't create GtkPlug as child of non-GtkSocket
  // However dialog handler works just fine.
  GtkWidget* widget = gtk_plug_new_for_display(gdk_display, xwindow);
  // Getting top level widget doesn't seem to be required.
  // OFF: GtkWidget* toplevel = gtk_widget_get_toplevel(widget);
  GtkWindow* window = GTK_WINDOW(widget);
  if (!window) {
    LOG(ERROR) << "No GtkWindow for browser";
  }
  return window;
}

XImage* CefBrowser_GetImage(CefRefPtr<CefBrowser> browser) {
    REQUIRE_UI_THREAD();
    // See SetX11WindowBounds: NULL off the UI thread or on a non-X11 Ozone
    // backend.
    ::Display* display = cef_get_xdisplay();
    if (!display) {
        LOG(ERROR) << "cef_get_xdisplay() returned NULL in CefBrowser_GetImage"
                      " (wrong thread or non-X11 backend)";
        return NULL;
    }
    ::Window browser_window = browser->GetHost()->GetWindowHandle();
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(display, browser_window, &attrs)) {
        LOG(ERROR) << "XGetWindowAttributes failed in CefBrowser_GetImage";
        return NULL;
    }
    XImage* image = XGetImage(display, browser_window,
                              0, 0, attrs.width, attrs.height,
                              AllPlanes, ZPixmap);
    if (!image) {
        LOG(ERROR) << "XGetImage failed in CefBrowser_GetImage";
        return NULL;
    }
    return image;
}
