// Copyright (c) 2016 CEF Python, see the Authors file.
// All rights reserved. Licensed under BSD 3-clause license.
// Project website: https://github.com/cztomczak/cefpython

// NOTE: This file is also used by "subprocess" and "libcefpythonapp"
//       targets during build.

#include "x11.h"
#include "util.h"
#include "include/base/cef_logging.h"

#include <dlfcn.h>

namespace {

// cefpython does not link GTK. The GTK functions here act on widgets of the
// host toolkit (wxPython, PyGObject), which has loaded GTK 3 itself, so they
// are looked up in the already loaded libraries. RTLD_NOLOAD never loads GTK.
class LoadedLibrary {
public:
    explicit LoadedLibrary(const char* name)
            : handle_(dlopen(name, RTLD_LAZY | RTLD_NOLOAD)) {}
    ~LoadedLibrary() {
        if (handle_) {
            dlclose(handle_);
        }
    }
    LoadedLibrary(const LoadedLibrary&) = delete;
    LoadedLibrary& operator=(const LoadedLibrary&) = delete;

    template <typename Function>
    Function Symbol(const char* name) const {
        return handle_ ? reinterpret_cast<Function>(dlsym(handle_, name))
                       : nullptr;
    }

private:
    void* handle_;
};

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
    // on non-fatal errors. Call it after the host toolkit was initialized,
    // as toolkits reset the handlers.
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

void* GtkPlugNew(unsigned long socket_id) {
    LoadedLibrary gtk("libgtk-3.so.0");
    auto gtk_plug_new = gtk.Symbol<void* (*)(unsigned long)>("gtk_plug_new");
    if (!gtk_plug_new) {
        LOG(ERROR) << "GtkPlugNew: GTK 3 is not loaded in this process";
        return NULL;
    }
    return gtk_plug_new(socket_id);
}

void GtkWidgetShow(void* widget) {
    LoadedLibrary gtk("libgtk-3.so.0");
    auto gtk_widget_show = gtk.Symbol<void (*)(void*)>("gtk_widget_show");
    if (!gtk_widget_show) {
        LOG(ERROR) << "GtkWidgetShow: GTK 3 is not loaded in this process";
        return;
    }
    gtk_widget_show(widget);
}

bool SetX11DefaultVisualForGtkWidget(void* widget) {
    // Puts |widget| on the X server's default visual (Issue #701), for
    // toolkits that have no API to set the visual, e.g. wxPython. Must be
    // called before the widget is realized.
    REQUIRE_UI_THREAD();
    LoadedLibrary gtk("libgtk-3.so.0");
    LoadedLibrary gdk("libgdk-3.so.0");
    LoadedLibrary gobject("libgobject-2.0.so.0");
    auto gtk_widget_get_realized =
            gtk.Symbol<int (*)(void*)>("gtk_widget_get_realized");
    auto gtk_widget_get_screen =
            gtk.Symbol<void* (*)(void*)>("gtk_widget_get_screen");
    auto gtk_widget_set_visual =
            gtk.Symbol<void (*)(void*, void*)>("gtk_widget_set_visual");
    auto gdk_x11_screen_get_type =
            gdk.Symbol<size_t (*)()>("gdk_x11_screen_get_type");
    auto gdk_x11_screen_get_screen_number =
            gdk.Symbol<int (*)(void*)>("gdk_x11_screen_get_screen_number");
    auto gdk_x11_screen_lookup_visual =
            gdk.Symbol<void* (*)(void*, unsigned long)>(
                    "gdk_x11_screen_lookup_visual");
    auto g_type_check_instance_is_a =
            gobject.Symbol<int (*)(void*, size_t)>(
                    "g_type_check_instance_is_a");
    if (!gtk_widget_get_realized || !gtk_widget_get_screen
            || !gtk_widget_set_visual || !gdk_x11_screen_get_type
            || !gdk_x11_screen_get_screen_number
            || !gdk_x11_screen_lookup_visual || !g_type_check_instance_is_a) {
        LOG(WARNING) << "SetX11DefaultVisualForGtkWidget: "
                        "GTK 3 is not loaded in this process";
        return false;
    }
    if (!widget || gtk_widget_get_realized(widget)) {
        LOG(WARNING) << "SetX11DefaultVisualForGtkWidget: "
                        "invalid or already realized widget";
        return false;
    }
    void* screen = gtk_widget_get_screen(widget);
    if (!screen
            || !g_type_check_instance_is_a(screen, gdk_x11_screen_get_type())) {
        LOG(WARNING) << "SetX11DefaultVisualForGtkWidget: "
                        "widget is not on an X11 screen";
        return false;
    }
    unsigned long visual_id = GetX11DefaultVisualId(
            gdk_x11_screen_get_screen_number(screen));
    if (!visual_id) {
        // GetX11DefaultVisualId logged why
        return false;
    }
    void* visual = gdk_x11_screen_lookup_visual(screen, visual_id);
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
