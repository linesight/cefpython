// Copyright (c) 2026 CEF Python, see the Authors file.
// All rights reserved. Licensed under BSD 3-clause license.

#include "window_utils_win.h"

#include <tlhelp32.h>
#include "include/base/cef_logging.h"

HWND GetGtkWin32WindowHandle(void* gdk_window) {
    // GTK builds name the GDK DLL differently, so look for the GDK function
    // in the modules already loaded by PyGObject.
    FARPROC get_handle = nullptr;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        for (BOOL found = Module32FirstW(snapshot, &entry);
             found && !get_handle;
             found = Module32NextW(snapshot, &entry)) {
            get_handle = GetProcAddress(entry.hModule,
                                        "gdk_win32_window_get_handle");
        }
        CloseHandle(snapshot);
    }
    if (!get_handle) {
        LOG(WARNING) << "[Browser process] GetGtkWindowHandle: could not find"
                        " GDK in loaded modules";
        return nullptr;
    }
    // GTK declares gdk_win32_window_get_handle(GdkWindow*) -> HGDIOBJ.
    using GetHandle = HGDIOBJ (*)(void*);
    HWND hwnd = static_cast<HWND>(
            reinterpret_cast<GetHandle>(get_handle)(gdk_window));
    if (!IsWindow(hwnd)) {
        LOG(WARNING) << "[Browser process] GetGtkWindowHandle: GDK returned"
                        " no valid window handle";
        return nullptr;
    }
    return hwnd;
}
