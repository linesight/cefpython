// Copyright (c) 2016 CEF Python, see the Authors file.
// All rights reserved. Licensed under BSD 3-clause license.
// Project website: https://github.com/cztomczak/cefpython

#pragma once

// CEF headers must come before X11 headers: X11/Xlib.h defines 'Status'
// (typedef int Status) which conflicts with CefURLRequest::Status typedef.
// Pre-including cef_urlrequest.h ensures it is processed first.
#include "include/cef_urlrequest.h"
#include "include/cef_browser.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

void InstallX11ErrorHandlers();
unsigned long GetX11DefaultVisualId(int screen_number);
bool SetX11DefaultVisualForGtkWidget(void* widget);
void* GtkPlugNew(unsigned long socket_id);
void GtkWidgetShow(void* widget);
void SetX11WindowBounds(CefRefPtr<CefBrowser> browser,
                        int x, int y, int width, int height);
void SetX11WindowTitle(CefRefPtr<CefBrowser> browser, char* title);

XImage* CefBrowser_GetImage(CefRefPtr<CefBrowser> browser);
