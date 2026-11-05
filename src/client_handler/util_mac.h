// Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#ifndef CEFPYTHON_UTIL_MAC_H_
#define CEFPYTHON_UTIL_MAC_H_

#include <string>
#include "include/cef_base.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"

void MacInitialize();
void MacShutdown();
std::string MacGetMainBundlePath();
void MacSetWindowTitle(CefRefPtr<CefBrowser> browser, char* title);

// Push/pop an autorelease pool for Cocoa calls made from Python, outside of
// any run loop iteration (e.g. CreateBrowserSync() before MessageLoop()).
void* MacAutoreleasePoolPush();
void MacAutoreleasePoolPop(void* pool);

// Called after DoClose() returned false for a windowed browser. Completes the
// browser close by detaching CEF's browser view from its superview.
void MacScheduleBrowserViewDetach(CefRefPtr<CefBrowser> browser);

#endif  // CEFPYTHON_UTIL_MAC_H_
