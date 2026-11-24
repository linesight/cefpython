# Copyright (c) 2026 CEF Python, see the Authors file.
# All rights reserved. Licensed under BSD 3-clause license.
# Project website: https://github.com/cztomczak/cefpython

"""DialogHandler.OnFileDialog and FileDialogCallback (Issue #361)."""

import unittest
# noinspection PyUnresolvedReferences
import _test_runner
from _common import *

from cefpython3 import cefpython as cef

import os
import pathlib
import shutil
import sys
import tempfile
import threading
import traceback

# A click on the left half opens the single file input, a click on the
# right half the multiple one.
g_datauri = cef.GetDataUrl("""
<!DOCTYPE html>
<html>
<head>
    <style type="text/css">
    html, body {
        height: 100%;
        margin: 0;
    }
    </style>
</head>
<body onclick="choose(event)">
    <input type="file" id="single" accept="image/*,.txt">
    <input type="file" id="multiple" multiple>
    <h1>File dialog test</h1>
    <script>
    var cancels = 0;
    var single = document.getElementById("single");
    var multiple = document.getElementById("multiple");
    function choose(event) {
        // input.click() sends a click that bubbles up to the body
        if (event.target !== document.body) {
            return;
        }
        var input = event.clientX < 400 ? single : multiple;
        input.value = "";
        input.click();
    }
    [single, multiple].forEach(function(input) {
        input.addEventListener("change", function() {
            var names = Array.from(input.files).map(function(file) {
                return file.name;
            });
            document.title = "chosen:" + names.sort().join("|");
        });
        input.addEventListener("cancel", function() {
            cancels++;
            document.title = "cancel:" + cancels;
        });
    });
    </script>
</body>
</html>
""")

SINGLE_X = 200
MULTIPLE_X = 600


class FileDialogTest_IsolatedTest(unittest.TestCase):
    def test_file_dialog(self):
        """All the code must run inside one single test, otherwise
        strange things happen."""

        print("")
        print("CEF Python {ver}".format(ver=cef.__version__))
        print("Python {ver}".format(ver=sys.version[:6]))

        tmp_dir = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp_dir, ignore_errors=True)
        first = os.path.join(tmp_dir, "first.txt")
        second = os.path.join(tmp_dir, "second.txt")
        saved = os.path.join(tmp_dir, "saved.txt")
        for path in [first, second]:
            with open(path, "w") as fp:
                fp.write("test")

        settings = {
            "debug": False,
            "log_severity": cef.LOGSEVERITY_ERROR,
            "log_file": "",
            "windowless_rendering_enabled": True,
            "root_cache_path": ROOT_CACHE_PATH,
        }
        if "--debug" in sys.argv:
            settings["debug"] = True
            settings["log_severity"] = cef.LOGSEVERITY_INFO
        switches = {
            "disable-gpu": "",
            "disable-gpu-compositing": "",
        }
        cef.Initialize(settings=settings, switches=switches)
        cef_active = True

        def shutdown_cef():
            nonlocal cef_active
            if cef_active:
                cef_active = False
                cef.Shutdown()

        self.addCleanup(shutdown_cef)
        subtest_message("cef.Initialize() ok")

        window_info = cef.WindowInfo()
        window_info.SetAsOffscreen(0)
        browser = cef.CreateBrowserSync(window_info=window_info,
                                        url=g_datauri)
        dialog_handler = DialogHandler(self)
        display_handler = DisplayHandler()
        for handler in [LoadHandler(), RenderHandler(),
                        dialog_handler, display_handler]:
            browser.SetClientHandler(handler)
        browser.SetFocus(True)
        browser.WasResized()

        # Each answer is given to one dialog, and the page title shows the
        # resulting "change" or "cancel" event of the file input.
        steps = [
            (SINGLE_X, ("continue", [first]), "chosen:first.txt",
             "FileDialogCallback.Continue()"),
            (SINGLE_X, ("cancel",), "cancel:1", "FileDialogCallback.Cancel()"),
            (SINGLE_X, ("drop",), "cancel:2", "released callback cancels"),
            (SINGLE_X, ("continue", []), "cancel:3", "Continue([]) cancels"),
            (MULTIPLE_X, ("continue", (first, pathlib.Path(second))),
             "chosen:first.txt|second.txt",
             "Continue() with two files, str and pathlib.Path"),
            (SINGLE_X, ("later", [pathlib.Path(second)]), "chosen:second.txt",
             "Continue() after OnFileDialog returned"),
            (SINGLE_X, ("thread", [first]), "chosen:first.txt",
             "Continue() on a non-UI thread"),
        ]
        for i, (x, answer, title, message) in enumerate(steps):
            dialog_handler.answers.append(answer)
            if i:
                # The first dialog is opened by LoadHandler.OnLoadEnd
                click(browser, x)
            completed = run_message_loop_until(
                    lambda: display_handler.title == title
                    or dialog_handler.errors)
            self.check_handler_errors(dialog_handler)
            self.assertTrue(completed, "Timed out waiting for title {0},"
                                       " title: {1}".format(
                                           title, display_handler.title))
            self.assertEqual(len(dialog_handler.calls), i + 1)
            subtest_message(message + " ok")

        # cefpython's DownloadHandler continues downloads with the Save As
        # dialog, which goes through OnFileDialog too.
        dialog_handler.answers.append(("continue", [saved]))
        browser.StartDownload("data:text/plain,hello")
        completed = run_message_loop_until(
                lambda: dialog_handler.errors or (os.path.exists(saved)
                and open(saved).read() == "hello"))
        self.check_handler_errors(dialog_handler)
        self.assertTrue(completed, "Timed out waiting for the download")
        subtest_message("FILE_DIALOG_SAVE ok")
        modes = [cef.FILE_DIALOG_OPEN_MULTIPLE if step[0] == MULTIPLE_X
                 else cef.FILE_DIALOG_OPEN for step in steps]
        self.assertEqual(dialog_handler.calls,
                         modes + [cef.FILE_DIALOG_SAVE])
        subtest_message("DialogHandler.OnFileDialog modes ok")

        # Close browser and clean reference
        browser.CloseBrowser(True)
        del browser
        do_message_loop_work(25)
        shutdown_cef()
        subtest_message("cef.Shutdown() ok")

        show_test_summary(__file__)
        sys.stdout.flush()

    def check_handler_errors(self, dialog_handler):
        if dialog_handler.errors:
            self.fail("DialogHandler.OnFileDialog failed:\n"
                      + dialog_handler.errors[0])


def click(browser, x=SINGLE_X):
    """A real click is a user gesture, which opening the file chooser
    from javascript requires."""
    browser.SendMouseClickEvent(x, 300, cef.MOUSEBUTTON_LEFT,
                                mouseUp=False, clickCount=1)
    browser.SendMouseClickEvent(x, 300, cef.MOUSEBUTTON_LEFT,
                                mouseUp=True, clickCount=1)


def click_after_load(browser):
    cef.PostDelayedTask(cef.TID_UI, 250, click, browser)


class LoadHandler(object):
    def OnLoadEnd(self, browser, frame, http_code, **_):
        if frame.IsMain():
            click_after_load(browser)


class RenderHandler(object):
    def GetViewRect(self, rect_out, **_):
        rect_out.extend([0, 0, 800, 600])
        return True

    def OnPaint(self, **_):
        pass


class DisplayHandler(object):
    def __init__(self):
        self.title = ""

    def OnTitleChange(self, title, **_):
        self.title = title


class DialogHandler(object):
    def __init__(self, test_case):
        self.test_case = test_case
        self.answers = []
        self.calls = []
        self.errors = []

    def OnFileDialog(self, callback, **kwargs):
        # An exception here would count as returning False, which opens
        # CEF's native dialog in this off-screen test and leaves the test
        # to time out. Cancel instead and let the test fail with the error.
        # The traceback is kept as text, because a traceback object would
        # keep the callback alive.
        try:
            self._on_file_dialog(callback=callback, **kwargs)
        except Exception:
            self.errors.append(traceback.format_exc())
            callback.Cancel()
        return True

    def _on_file_dialog(self, mode, title, default_file_path, accept_filters,
                        accept_extensions, accept_descriptions, callback,
                        **_):
        test_case = self.test_case
        self.calls.append(mode)
        test_case.assertIsInstance(title, str)
        test_case.assertIsInstance(default_file_path, str)
        if mode == cef.FILE_DIALOG_OPEN:
            test_case.assertEqual(accept_filters, ["image/*", ".txt"])
            test_case.assertIn(".png", accept_extensions[0].split(";"))
            test_case.assertEqual(accept_extensions[1], ".txt")
            test_case.assertNotEqual(accept_descriptions[0], "")
            test_case.assertEqual(accept_descriptions[1], "")
        if len(self.calls) == 1:
            self._check_invalid_arguments(callback)
        if mode == cef.FILE_DIALOG_OPEN_MULTIPLE:
            # Nothing is passed to CEF when any of the paths is invalid
            with test_case.assertRaises(ValueError):
                callback.Continue([tempfile.gettempdir(), "relative.txt"])
        action, file_paths = (self.answers.pop(0) + (None,))[:2]
        if action == "continue":
            callback.Continue(file_paths)
        elif action == "cancel":
            callback.Cancel()
        elif action == "later":
            cef.PostDelayedTask(cef.TID_UI, 100, callback.Continue,
                                file_paths)
        elif action == "thread":
            threading.Thread(target=callback.Continue,
                             args=(file_paths,)).start()

    def _check_invalid_arguments(self, callback):
        test_case = self.test_case
        for file_paths in ["first.txt", None, [1],
                           [os.fsencode(tempfile.gettempdir())]]:
            with test_case.assertRaises(TypeError):
                callback.Continue(file_paths)
        relative = ["relative.txt", pathlib.Path("relative.txt")]
        if WINDOWS:
            relative += ["\\relative.txt", "C:relative.txt",
                         "1:\\relative.txt"]
        for path in relative:
            with test_case.assertRaises(ValueError):
                callback.Continue([path])
        # Only FILE_DIALOG_OPEN_MULTIPLE takes more than one path
        with test_case.assertRaises(ValueError):
            callback.Continue([tempfile.gettempdir()] * 2)


if __name__ == "__main__":
    _test_runner.main(os.path.basename(__file__))
