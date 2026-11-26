# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ivan De Cesaris, IZ0JNY
"""NanoHam PC as a desktop program, as EMELab: starts the Streamlit server on this PC and shows it in its own window
(pywebview / WebView2), or in the default browser when pywebview is not installed.

    python -m nanoham_pc.desktop             # own window (or browser)
    python -m nanoham_pc.desktop --browser   # default browser
"""
from __future__ import annotations

import argparse
import socket
import subprocess
import sys
import time
import urllib.request
import webbrowser
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TITLE = "NanoHam PC — misure in fibra ottica"


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def wait_up(url: str, seconds: float = 30.0) -> bool:
    end = time.time() + seconds
    while time.time() < end:
        try:
            urllib.request.urlopen(url + "/_stcore/health", timeout=1).read()
            return True
        except Exception:
            time.sleep(0.3)
    return False


def main(argv=None):
    ap = argparse.ArgumentParser(description=TITLE)
    ap.add_argument("--browser", action="store_true", help="usa il browser predefinito")
    ap.add_argument("--port", type=int, default=0)
    a = ap.parse_args(argv)
    port = a.port or free_port()
    url = f"http://127.0.0.1:{port}"
    srv = subprocess.Popen([sys.executable, "-m", "streamlit", "run", str(ROOT / "nanoham_main.py"),
                            "--server.port", str(port), "--server.address", "127.0.0.1",
                            "--server.headless", "true"], cwd=str(ROOT))
    try:
        if not wait_up(url):
            print("Il server non è partito.")
            return 1
        webview = None
        if not a.browser:
            try:
                import webview
            except ImportError:
                webview = None
        if webview:
            webview.create_window(TITLE, url, width=1400, height=900)
            webview.start()
        else:
            webbrowser.open(url)
            print(f"NanoHam PC su {url} (Ctrl+C per chiudere)")
            srv.wait()
    except KeyboardInterrupt:
        pass
    finally:
        srv.terminate()
    return 0


if __name__ == "__main__":
    sys.exit(main())
