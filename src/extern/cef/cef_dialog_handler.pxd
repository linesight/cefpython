# Copyright (c) 2026 CEF Python, see the Authors file.
# All rights reserved. Licensed under BSD 3-clause license.
# Project website: https://github.com/cztomczak/cefpython

from cef_string cimport CefString
from libcpp.vector cimport vector as cpp_vector

cdef extern from "include/cef_dialog_handler.h":
    cdef cppclass CefFileDialogCallback:
        void Continue(const cpp_vector[CefString]& file_paths)
        void Cancel()
