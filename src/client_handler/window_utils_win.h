// Copyright (c) 2026 CEF Python, see the Authors file.
// All rights reserved. Licensed under BSD 3-clause license.

#pragma once

#include <windows.h>

// The caller must keep the GdkWindow alive and call this on the GTK UI thread.
// GDK may create a native child window for a client-side GdkWindow.
// Returns NULL on failure and logs a warning.
HWND GetGtkWin32WindowHandle(void* gdk_window);
