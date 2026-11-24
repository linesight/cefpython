// Copyright (c) 2017 CEF Python, see the Authors file.
// All rights reserved. Licensed under BSD 3-clause license.
// Project website: https://github.com/cztomczak/cefpython

#include "dialog_handler.h"


bool DialogHandler::OnFileDialog(CefRefPtr<CefBrowser> browser,
                                 FileDialogMode mode,
                                 const CefString& title,
                                 const CefString& default_file_path,
                                 const std::vector<CefString>& accept_filters,
                                 const std::vector<CefString>& accept_extensions,
                                 const std::vector<CefString>& accept_descriptions,
                                 CefRefPtr<CefFileDialogCallback> callback)
{
    REQUIRE_UI_THREAD();
    return DialogHandler_OnFileDialog(browser, mode, title, default_file_path,
                                      accept_filters, accept_extensions,
                                      accept_descriptions, callback);
}
