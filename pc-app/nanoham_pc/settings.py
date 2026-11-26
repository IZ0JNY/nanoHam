# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ivan De Cesaris, IZ0JNY
"""Settings of NanoHam PC in %APPDATA%\\NanoHamPC\\settings.json (~/.config/NanoHamPC elsewhere): the boards with
their channels, the CSV log folder and interval."""
from __future__ import annotations

import json
import os
from pathlib import Path

from nanoham_pc.link import Board


def config_dir() -> Path:
    base = os.environ.get("NANOHAM_PC_HOME") or os.environ.get("APPDATA") or str(Path.home() / ".config")
    p = Path(base) / "NanoHamPC"
    p.mkdir(parents=True, exist_ok=True)
    return p


def default_log_dir() -> str:
    return str(Path.home() / "Documents" / "NanoHam log")


def load() -> dict:
    try:
        d = json.loads((config_dir() / "settings.json").read_text(encoding="utf-8"))
    except Exception:
        d = {}
    d["boards"] = [Board.from_dict(b) for b in d.get("boards") or []] or [Board()]
    d.setdefault("log_on", False)
    d.setdefault("log_dir", default_log_dir())
    d.setdefault("log_every_s", 1.0)
    d.setdefault("graph_min", 5)
    return d


def save(d: dict):
    out = {**d, "boards": [b.to_dict() if isinstance(b, Board) else b for b in d.get("boards", [])]}
    p = config_dir() / "settings.json"
    tmp = p.with_suffix(".tmp")
    tmp.write_text(json.dumps(out, indent=1, ensure_ascii=False), encoding="utf-8")
    tmp.replace(p)
