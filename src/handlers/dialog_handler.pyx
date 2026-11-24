# Copyright (c) 2026 CEF Python, see the Authors file.
# All rights reserved. Licensed under BSD 3-clause license.
# Project website: https://github.com/cztomczak/cefpython

include "../cefpython.pyx"
include "../browser.pyx"

# enum cef_file_dialog_mode_t
cimport cef_types
FILE_DIALOG_OPEN = cef_types.FILE_DIALOG_OPEN
FILE_DIALOG_OPEN_MULTIPLE = cef_types.FILE_DIALOG_OPEN_MULTIPLE
FILE_DIALOG_OPEN_FOLDER = cef_types.FILE_DIALOG_OPEN_FOLDER
FILE_DIALOG_SAVE = cef_types.FILE_DIALOG_SAVE

# -----------------------------------------------------------------------------
# PyFileDialogCallback
# -----------------------------------------------------------------------------
cdef py_bool IsAbsoluteFilePath(str path):
    # The rule of IsPathAbsolute() in Chromium's base/files/file_path.cc.
    # On Windows os.path.isabs() differs: it accepts "1:\file", and
    # "\file" before Python 3.13.
    IF UNAME_SYSNAME == "Windows":
        if len(path) >= 2 and path[1] == ":" and path[0].isascii() \
                and path[0].isalpha():
            return len(path) > 2 and path[2] in "\\/"
        return len(path) > 1 and path[0] in "\\/" and path[1] in "\\/"
    ELSE:
        return os.path.isabs(path)

cdef PyFileDialogCallback CreatePyFileDialogCallback(
        CefRefPtr[CefFileDialogCallback] cefCallback,
        cef_types.cef_file_dialog_mode_t mode):
    cdef PyFileDialogCallback pyCallback = PyFileDialogCallback()
    pyCallback.cefCallback = cefCallback
    pyCallback.mode = mode
    return pyCallback

cdef class PyFileDialogCallback:
    cdef CefRefPtr[CefFileDialogCallback] cefCallback
    cdef cef_types.cef_file_dialog_mode_t mode
    cdef object __weakref__

    cpdef py_void Continue(self, object file_paths):
        cdef cpp_vector[CefString] cefFilePaths
        if not isinstance(file_paths, (list, tuple)):
            raise TypeError("FileDialogCallback.Continue() failed:"
                            " file_paths must be a list of str or"
                            " os.PathLike")
        # Only FILE_DIALOG_OPEN_MULTIPLE takes several paths. CEF DCHECKs
        # this for downloads.
        if len(file_paths) > 1 \
                and self.mode != cef_types.FILE_DIALOG_OPEN_MULTIPLE:
            raise ValueError("FileDialogCallback.Continue() failed:"
                             " more than one path, but the dialog mode"
                             " is not FILE_DIALOG_OPEN_MULTIPLE")
        for filePath in file_paths:
            # pathlib.Path and other os.PathLike objects. A path that
            # converts to bytes is rejected below, like a bytes path.
            if isinstance(filePath, os.PathLike):
                filePath = os.fspath(filePath)
            if not isinstance(filePath, str):
                raise TypeError("FileDialogCallback.Continue() failed:"
                                " file_paths must be a list of str or"
                                " os.PathLike")
            # A relative path makes Chromium terminate the renderer.
            if not IsAbsoluteFilePath(filePath):
                raise ValueError("FileDialogCallback.Continue() failed:"
                                 " not an absolute path: " + filePath)
            cefFilePaths.push_back(PyToCefStringValue(filePath))
        self.cefCallback.get().Continue(cefFilePaths)

    cpdef py_void Cancel(self):
        self.cefCallback.get().Cancel()

# -----------------------------------------------------------------------------
# DialogHandler
# -----------------------------------------------------------------------------

cdef public cpp_bool DialogHandler_OnFileDialog(
        CefRefPtr[CefBrowser] cefBrowser,
        cef_types.cef_file_dialog_mode_t mode,
        const CefString& title,
        const CefString& default_file_path,
        const cpp_vector[CefString]& accept_filters,
        const cpp_vector[CefString]& accept_extensions,
        const cpp_vector[CefString]& accept_descriptions,
        CefRefPtr[CefFileDialogCallback] cefCallback
        ) noexcept with gil:
    cdef PyBrowser pyBrowser
    cdef py_string pyTitle
    cdef py_string pyDefaultFilePath
    cdef list pyAcceptFilters = []
    cdef list pyAcceptExtensions = []
    cdef list pyAcceptDescriptions = []
    cdef PyFileDialogCallback pyCallback
    cdef object clientCallback
    cdef py_bool returnValue
    cdef size_t i
    try:
        pyBrowser = GetPyBrowser(cefBrowser, "OnFileDialog")
        clientCallback = pyBrowser.GetClientCallback("OnFileDialog")
        if not clientCallback:
            return False
        pyTitle = CefToPyString(title)
        pyDefaultFilePath = CefToPyString(default_file_path)
        for i in range(accept_filters.size()):
            pyAcceptFilters.append(CefToPyString(accept_filters[i]))
        for i in range(accept_extensions.size()):
            pyAcceptExtensions.append(CefToPyString(accept_extensions[i]))
        for i in range(accept_descriptions.size()):
            pyAcceptDescriptions.append(CefToPyString(accept_descriptions[i]))
        pyCallback = CreatePyFileDialogCallback(cefCallback, mode)
        returnValue = clientCallback(
                browser=pyBrowser,
                mode=<int>mode,
                title=pyTitle,
                default_file_path=pyDefaultFilePath,
                accept_filters=pyAcceptFilters,
                accept_extensions=pyAcceptExtensions,
                accept_descriptions=pyAcceptDescriptions,
                callback=pyCallback)
        return bool(returnValue)
    except:
        (exc_type, exc_value, exc_trace) = sys.exc_info()
        sys.excepthook(exc_type, exc_value, exc_trace)
    return False
