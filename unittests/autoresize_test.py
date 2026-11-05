# Copyright (c) 2026 CEF Python, see the Authors file.
# All rights reserved. Licensed under BSD 3-clause license.
# Project website: https://github.com/cztomczak/cefpython

"""OnAutoResize / SetAutoResizeEnabled test, modelled on CEF's
DisplayTest.AutoResize (tests/ceftests/display_unittest.cc).

Differences from the auto-resize part of main_test.py: a dedicated browser
(no popup or DevTools running at the same time), auto-resize is enabled right
after the browser is created (before the load finishes), and every step is
driven by the previous OnAutoResize notification instead of by a timer.
"""

import unittest
# noinspection PyUnresolvedReferences
import _test_runner
from _common import *

from cefpython3 import cefpython as cef

import os
import time

g_datauri_data = """
<!DOCTYPE html>
<html><head><style>
body {overflow:hidden;margin:0px;padding:0px;}
</style></head><body><div id=a>Content</div></body></html>
"""
g_datauri = cef.GetDataUrl(g_datauri_data)


class AutoResizeHandler(object):
    def __init__(self):
        self.t0 = time.monotonic()
        self.sizes = []          # sizes accepted by the state machine
        self.after_disable = []  # notifications seen after disabling
        self.disabled = False
        self.done = False

    def OnAutoResize(self, browser, new_size, **_):
        dbg("OnAutoResize %s" % (new_size,))
        if self.disabled:
            self.after_disable.append(list(new_size))
            return True
        if new_size[0] > 500 or new_size[1] > 500:
            # Initial resize to the default window size that may or may
            # not occur (same idea as the CEF test, but the maximum size set
            # below is 500x500 so anything larger cannot be a constrained
            # auto-resize size; a fixed threshold like 700x500 misses
            # default windows such as 752x486).
            return True
        self.sizes.append(list(new_size))
        if len(self.sizes) == 1:
            dbg("autoresize first notification %.3fs after enable"
                % (time.monotonic() - self.t0))
            browser.ExecuteJavascript(
                "document.getElementById('a').innerText='New Content';")
        elif len(self.sizes) == 2:
            dbg("autoresize second notification %.3fs after enable"
                % (time.monotonic() - self.t0))
            browser.SetAutoResizeEnabled(False, [0, 0], [0, 0])
            self.disabled = True
            browser.ExecuteJavascript(
                "document.getElementById('a').innerText='New Content Again';")
            cef.PostDelayedTask(cef.TID_UI, 300, self._finish)
        return True

    def _finish(self):
        self.done = True


class AutoResizeTest_IsolatedTest(unittest.TestCase):
    def test_autoresize(self):
        """Dedicated OnAutoResize test."""
        if LINUX and not os.environ.get("AUTORESIZE_ON_LINUX"):
            self.skipTest("Disabled on Linux, same as CEF (CEF issue #4038)")

        cef_active = True

        def shutdown_cef():
            nonlocal cef_active
            if cef_active:
                cef_active = False
                cef.Shutdown()

        cef.Initialize({"debug": False,
                        "log_severity": cef.LOGSEVERITY_WARNING,
                        "log_file": ""})
        self.addCleanup(shutdown_cef)
        subtest_message("cef.Initialize() ok")

        browser = cef.CreateBrowserSync(url=g_datauri)
        self.assertIsNotNone(browser)
        handler = AutoResizeHandler()
        browser.SetClientHandler(handler)
        browser.SetAutoResizeEnabled(True, [10, 10], [500, 500])
        handler.t0 = time.monotonic()
        subtest_message("browser.SetAutoResizeEnabled() ok")

        completed = run_message_loop_until(lambda: handler.done)
        if not completed:
            dbg("TIMEOUT sizes=%s disabled=%s" % (handler.sizes,
                                                   handler.disabled))
        self.assertTrue(completed, "Timed out waiting for OnAutoResize "
                                   "notifications")
        subtest_message("OnAutoResize notifications ok")

        # Content is "Content" then wraps/grows: the second size must differ.
        self.assertEqual(len(handler.sizes), 2)
        self.assertLess(handler.sizes[0][0], 500)
        self.assertLess(handler.sizes[0][1], 500)
        self.assertGreater(handler.sizes[1][1], handler.sizes[0][1])
        self.assertEqual(handler.after_disable, [],
                         "OnAutoResize called after it was disabled")
        subtest_message("OnAutoResize sizes ok")

        browser.CloseBrowser(True)
        del browser
        do_message_loop_work(25)
        shutdown_cef()
        subtest_message("cef.Shutdown() ok")
        show_test_summary(__file__)


if __name__ == "__main__":
    _test_runner.main(os.path.basename(__file__))
