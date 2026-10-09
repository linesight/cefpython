"""Repro / diagnostic for Issue #699 (JS bindings after Reload()).

Usage: python issue699_repro.py <mode> <log_file>
  mode: "default"       - Chromium 147 default RenderDocument level (all-frames)
        "crashed-frame" - force pre-130 behavior (no new view on reload)
"""
import http.server
import os
import sys
import threading
import time

from cefpython3 import cefpython as cef

RELOADS = 3

HTML = b"""<!doctype html><html><body>issue 699
<script>
function check(tag) {
  var t = typeof py_func;
  try {
    py_func(tag);
    console.log("[#699] JS " + tag + " typeof=" + t + " call=OK");
  } catch (e) {
    console.log("[#699] JS " + tag + " typeof=" + t + " call=FAIL " + e);
  }
}
check("inline");
(function burst() {
  var start = performance.now(), ok = 0, fail = 0, firstErr = "";
  function tick() {
    try { py_func.name; py_func("burst"); ok++; }
    catch (e) { fail++; if (!firstErr) firstErr = String(e); }
    if (performance.now() - start < 100) { setTimeout(tick, 0); }
    else { console.log("[#699] JS burst ok=" + ok + " fail=" + fail +
                       (fail ? " first=" + firstErr : "")); }
  }
  tick();
})();
window.addEventListener("load", function() {
  check("load");
  setTimeout(function() { check("load+500ms"); }, 500);
});
</script></body></html>"""


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(HTML)

    def log_message(self, *args):
        pass


class ClientHandler:
    def __init__(self):
        self.loads = 0
        self.next_reload_at = None

    def OnLoadEnd(self, browser, frame, http_code, **_):
        if not frame.IsMain():
            return
        self.loads += 1
        print("[#699] PY OnLoadEnd #%d" % self.loads, flush=True)
        self.next_reload_at = time.time() + 1.5

    def OnConsoleMessage(self, browser, message, **_):
        if "[#699]" in message:
            print(message, flush=True)
        return False


def py_func(tag):
    if tag == "burst":
        return
    print("[#699] PY py_func(%s) reached the browser process" % tag,
          flush=True)


def main():
    mode, log_file = sys.argv[1], sys.argv[2]
    if os.path.exists(log_file):
        os.remove(log_file)
    server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = "http://127.0.0.1:%d/" % server.server_address[1]

    switches = {}
    if mode == "crashed-frame":
        switches["enable-features"] = "RenderDocument:level/crashed-frame"
    settings = {
        "debug": True,
        "log_severity": cef.LOGSEVERITY_INFO,
        "log_file": log_file,
    }
    sys.excepthook = cef.ExceptHook
    cef.Initialize(settings, switches)
    browser = cef.CreateBrowserSync(url=url, window_title="issue 699")
    handler = ClientHandler()
    browser.SetClientHandler(handler)
    bindings = cef.JavascriptBindings()
    bindings.SetFunction("py_func", py_func)
    browser.SetJavascriptBindings(bindings)

    reloads = 0
    deadline = time.time() + 60
    while time.time() < deadline:
        cef.MessageLoopWork()
        time.sleep(float(os.environ.get("ISSUE699_LOOP_SLEEP", "0.005")))
        if handler.next_reload_at and time.time() >= handler.next_reload_at:
            handler.next_reload_at = None
            if reloads == RELOADS:
                break
            reloads += 1
            print("[#699] PY Reload() #%d" % reloads, flush=True)
            browser.Reload()

    browser.CloseBrowser(True)
    end = time.time() + 3
    while time.time() < end:
        cef.MessageLoopWork()
        time.sleep(0.005)
    del browser
    cef.Shutdown()
    server.shutdown()


if __name__ == "__main__":
    main()
