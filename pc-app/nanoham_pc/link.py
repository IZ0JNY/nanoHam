# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ivan De Cesaris, IZ0JNY
"""NanoHam link on the PC: reads the reading board's lines from a serial (COM) port, keeps the latest values and a
short history, checks the alarm thresholds and writes a CSV log.

No Streamlit here: this module is the core that can be moved into EMELab as a tab later.

Protocol (nanoHam repo, control-board/GigaMeasureDisplay/PROTOCOLLO.md): UART 115200 8N1, one ASCII line per
measurement «CH<n>,<value>\\n» (a «\\r» is ignored), value with a decimal point, e.g. «CH1,7.532». Each channel
arrives every 100 ms or more often; a channel with no data for 1 s is shown as «NO DATA».

On the PC the fiber goes into a TEL0153 module and a 3.3 V USB-TTL cable (e.g. Waveshare FT232RNL): Windows sees
a COM port. Several boards (one per band: 10, 24, 47 GHz…) each have their own port and Reader.

Every channel is generic: name, unit, value shown = value received × scale + offset, decimals, alarm thresholds.
Two kinds: «analog» (voltages, currents, temperatures in °C…) and «logic» (a state: received ≥ 0.5 = ON, shown
with the user's words, e.g. «PTT» / «RX», alarm when it is in the state chosen as alarm).
The defaults are the NanoHam channels: CH1..CH4 voltages, CH5/CH6 temperatures (LM35), CH7/CH8 logic inputs."""
from __future__ import annotations

import collections
import csv
import math
import os
import re
import threading
import time
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path

BAUD = 115200
STALE_S = 1.0                     # no data for this long → NO DATA (as the GIGA display)
HISTORY_S = 3600                  # seconds of history kept in memory for the graphs
DEMO_PORT = "DEMO"                # pseudo port: a simulated NanoHam, to try the program without hardware

LINE_RE = re.compile(r"^\s*CH(\d{1,2})\s*,\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)\s*$")


def parse_line(line: bytes | str) -> tuple[int, float] | None:
    """«CH1,7.532» → (1, 7.532); anything else (noise on the fiber, debug text, a broken line) → None."""
    if isinstance(line, bytes):
        line = line.decode("ascii", "ignore")
    m = LINE_RE.match(line.replace("\r", ""))
    if not m:
        return None
    v = float(m.group(2))
    if not math.isfinite(v):
        return None
    return int(m.group(1)), v


@dataclass
class Channel:
    n: int                                 # number in «CH<n>»
    name: str = ""
    unit: str = "V"
    scale: float = 1.0                     # shown = received × scale + offset
    offset: float = 0.0
    decimals: int = 3
    full_scale: float = 10.0               # top of the bar, in shown units
    alarm_min: float | None = None         # alarm below this (shown units); None = off
    alarm_max: float | None = None         # alarm above this; None = off
    alarm_nodata: bool = True              # alarm when the channel goes silent
    enabled: bool = True
    kind: str = "analog"                   # "analog" or "logic"
    on_text: str = "ON"                    # logic: words for the two states
    off_text: str = "OFF"
    alarm_state: str = ""                  # logic: "ON" / "OFF" = alarm in that state, "" = no alarm

    @property
    def logic(self) -> bool:
        return self.kind == "logic"

    def value(self, raw: float) -> float:
        if self.logic:
            return 1.0 if raw >= 0.5 else 0.0
        return raw * self.scale + self.offset

    def fmt(self, v: float | None) -> str:
        if v is None:
            return "-----"
        if self.logic:
            return self.on_text if v >= 0.5 else self.off_text
        return f"{v:.{max(0, int(self.decimals))}f}"


def nanoham_channels() -> list[Channel]:
    """The NanoHam channels (PROTOCOLLO.md): CH1..CH4 voltages, CH5/CH6 temperatures, CH7/CH8 logic inputs."""
    return [
        Channel(1, "V Helix", "kV", full_scale=10.0),                   # DFR1184: 0..10 V = 0..10 kV
        Channel(2, "V Master", "V", full_scale=24.0),                   # NanoHam sends real volts (divider applied)
        Channel(3, "V Slave", "V", full_scale=24.0),
        Channel(4, "V Aux", "V", full_scale=24.0),
        Channel(5, "T box", "°C", decimals=1, full_scale=100.0, alarm_max=60.0),      # LM35 on A6
        Channel(6, "T heatsink", "°C", decimals=1, full_scale=100.0, alarm_max=70.0), # LM35 on A7
        Channel(7, "In 1", "", kind="logic"),                                       # GPIO 6
        Channel(8, "In 2", "", kind="logic"),                                       # GPIO 7
    ]


@dataclass
class Board:
    name: str = "10 GHz"                   # user label, e.g. the band
    port: str = DEMO_PORT
    baud: int = BAUD
    enabled: bool = True
    channels: list[Channel] = field(default_factory=nanoham_channels)

    def to_dict(self) -> dict:
        return asdict(self)

    @classmethod
    def from_dict(cls, d: dict) -> "Board":
        chans = [Channel(**{k: v for k, v in c.items() if k in Channel.__dataclass_fields__})
                 for c in d.get("channels") or []] or nanoham_channels()
        return cls(name=str(d.get("name") or "NanoHam"), port=str(d.get("port") or DEMO_PORT),
                   baud=int(d.get("baud") or BAUD), enabled=bool(d.get("enabled", True)), channels=chans)

    def channel(self, n: int) -> Channel | None:
        return next((c for c in self.channels if c.n == n), None)


def alarm_of(ch: Channel, value: float | None) -> str | None:
    """Alarm text for one channel, or None when all is fine."""
    if not ch.enabled:
        return None
    if value is None:
        return "NO DATA" if ch.alarm_nodata else None
    if ch.logic:
        return ch.fmt(value) if ch.alarm_state and (value >= 0.5) == (ch.alarm_state == "ON") else None
    if ch.alarm_max is not None and value > ch.alarm_max:
        return f"{ch.fmt(value)} {ch.unit} > {ch.fmt(ch.alarm_max)}"
    if ch.alarm_min is not None and value < ch.alarm_min:
        return f"{ch.fmt(value)} {ch.unit} < {ch.fmt(ch.alarm_min)}"
    return None


def serial_ports() -> list[tuple[str, str]]:
    """[(port, description)] of this PC (needs pyserial; empty without it)."""
    try:
        from serial.tools import list_ports
    except ImportError:
        return []
    return [(p.device, p.description or "") for p in sorted(list_ports.comports(), key=lambda p: p.device)]


class _DemoSerial:
    """A NanoHam that is not there: CH1..CH8 every 100 ms, slowly moving, CH1 near 7 kV, In 1 toggling every 20 s."""

    def __init__(self, seed: float = 0.0):
        self._t0 = time.time() - seed * 37
        self._buf: list[bytes] = []
        self._next = time.time()

    def readline(self) -> bytes:
        if not self._buf:
            now = time.time()
            if now < self._next:
                time.sleep(self._next - now)
            self._next = max(self._next + 0.1, time.time() - 0.5)
            t = time.time() - self._t0
            vals = (7.0 + 0.15 * math.sin(t / 7), 12.0 + 0.2 * math.sin(t / 3), 12.0 + 0.1 * math.cos(t / 5),
                    5.0 + 0.05 * math.sin(t), 31.0 + 2 * math.sin(t / 60), 45.0 + 5 * math.sin(t / 40),
                    float(int(t / 20) % 2), 0.0)
            self._buf = [f"CH{i},{v:.3f}\n".encode() for i, v in enumerate(vals, 1)]
        return self._buf.pop(0)

    def close(self):
        pass


class Reader:
    """Reads one board in a background thread; reconnects by itself if the port goes away (USB cable pulled)."""

    def __init__(self, port: str, baud: int = BAUD, history_s: float = HISTORY_S, opener=None):
        self.port, self.baud = port, int(baud)
        self._opener = opener
        self._lock = threading.Lock()
        self.latest: dict[int, tuple[float, float]] = {}            # n → (time, raw value)
        self.history: dict[int, collections.deque] = {}             # n → deque[(time, raw)]
        self._history_s = history_s
        self.lines_ok = 0
        self.lines_bad = 0
        self.error: str | None = None
        self.connected = False
        self._stop = threading.Event()
        self._rate = collections.deque(maxlen=200)                   # arrival times, for lines/s
        self._thread = threading.Thread(target=self._run, daemon=True, name=f"nanoham-{port}")
        self._thread.start()

    # --- reading
    def _open(self):
        if self._opener:
            return self._opener(self.port, self.baud)
        if self.port.upper() == DEMO_PORT:
            return _DemoSerial()
        import serial
        return serial.Serial(self.port, self.baud, timeout=0.5)

    def feed(self, line: bytes | str, t: float | None = None) -> bool:
        """One received line (also used by the tests)."""
        p = parse_line(line)
        t = time.time() if t is None else t
        with self._lock:
            if p is None:
                if (line.strip() if isinstance(line, str) else line.strip()):
                    self.lines_bad += 1
                return False
            n, v = p
            self.lines_ok += 1
            self.latest[n] = (t, v)
            h = self.history.setdefault(n, collections.deque())
            h.append((t, v))
            while h and h[0][0] < t - self._history_s:
                h.popleft()
            self._rate.append(t)
        return True

    def _run(self):
        while not self._stop.is_set():
            s = None
            try:
                s = self._open()
                self.connected, self.error = True, None
                while not self._stop.is_set():
                    ln = s.readline()
                    if ln:
                        self.feed(ln)
            except Exception as e:                                   # port busy, missing, cable pulled…
                self.connected, self.error = False, str(e)
            finally:
                try:
                    if s:
                        s.close()
                except Exception:
                    pass
            self.connected = False
            self._stop.wait(2.0)

    def stop(self):
        self._stop.set()

    # --- what the UI reads
    def raw(self, n: int, now: float | None = None, stale_s: float = STALE_S) -> float | None:
        now = time.time() if now is None else now
        with self._lock:
            x = self.latest.get(n)
        return None if x is None or now - x[0] > stale_s else x[1]

    def series(self, n: int, since_s: float, now: float | None = None) -> list[tuple[float, float]]:
        now = time.time() if now is None else now
        with self._lock:
            return [p for p in self.history.get(n, ()) if p[0] >= now - since_s]

    def lines_per_s(self, now: float | None = None) -> float:
        now = time.time() if now is None else now
        with self._lock:
            r = [t for t in self._rate if t >= now - 2.0]
        return len(r) / 2.0

    def status(self, now: float | None = None) -> str:
        """LINK / NO DATA / ERRORE (as the GIGA display: LINK when something arrived in the last second)."""
        if self.error and not self.connected:
            return "ERRORE"
        now = time.time() if now is None else now
        with self._lock:
            last = max((t for t, _ in self.latest.values()), default=0.0)
        return "LINK" if now - last <= STALE_S else "NO DATA"


_READERS: dict[str, Reader] = {}
_READERS_LOCK = threading.Lock()


def reader(port: str, baud: int = BAUD) -> Reader:
    """One Reader per port, kept across the app's reruns; a new baud rate restarts it."""
    with _READERS_LOCK:
        r = _READERS.get(port)
        if r and r.baud != int(baud):
            r.stop()
            r = None
        if r is None:
            r = _READERS[port] = Reader(port, baud)
        return r


def stop_reader(port: str):
    with _READERS_LOCK:
        r = _READERS.pop(port, None)
    if r:
        r.stop()


def active_ports() -> list[str]:
    with _READERS_LOCK:
        return list(_READERS)


class CsvLog:
    """One CSV per board per UTC day: time, then every enabled channel in shown units, plus the alarms."""

    def __init__(self, folder: str | os.PathLike):
        self.folder = Path(folder)
        self._last: dict[str, float] = {}
        self._lock = threading.Lock()

    def path(self, board: Board, when: datetime | None = None) -> Path:
        when = when or datetime.now(timezone.utc)
        safe = re.sub(r"[^\w.-]+", "_", board.name).strip("_") or "nanoham"
        return self.folder / f"nanoham_{safe}_{when:%Y%m%d}.csv"

    def write(self, board: Board, rd: Reader, every_s: float = 1.0, now: float | None = None) -> Path | None:
        """Adds a row if at least every_s seconds passed since the last one of this board."""
        now = time.time() if now is None else now
        with self._lock:
            if now - self._last.get(board.name, 0.0) < every_s:
                return None
            self._last[board.name] = now
        chans = [c for c in board.channels if c.enabled]
        when = datetime.fromtimestamp(now, timezone.utc)
        p = self.path(board, when)
        p.parent.mkdir(parents=True, exist_ok=True)
        new = not p.exists()
        vals, alarms = [], []
        for c in chans:
            r = rd.raw(c.n, now)
            v = None if r is None else c.value(r)
            vals.append("" if v is None else c.fmt(v))
            a = alarm_of(c, v)
            if a:
                alarms.append(f"{c.name}: {a}")
        with open(p, "a", newline="", encoding="utf-8") as f:
            w = csv.writer(f, delimiter=";")
            if new:
                w.writerow(["UTC"] + [f"{c.name} ({c.unit})" for c in chans] + ["Allarmi"])
            w.writerow([when.strftime("%Y-%m-%d %H:%M:%S")] + vals + [" | ".join(alarms)])
        return p


class Logger:
    """Writes the CSV in the background, also while the window is minimised or on another page."""

    def __init__(self):
        self.boards: list[Board] = []
        self.folder: str | None = None
        self.every_s = 1.0
        self.last_path: Path | None = None
        self.error: str | None = None
        self._log: CsvLog | None = None
        self._lock = threading.Lock()
        threading.Thread(target=self._run, daemon=True, name="nanoham-log").start()

    def configure(self, boards: list[Board], folder: str | None, every_s: float = 1.0):
        """folder None = logging off."""
        with self._lock:
            self.boards = [b for b in boards if b.enabled]
            if folder != self.folder:
                self._log = CsvLog(folder) if folder else None
            self.folder, self.every_s = folder, max(0.1, float(every_s))

    def _run(self):
        while True:
            with self._lock:
                log, boards, every = self._log, list(self.boards), self.every_s
            if log:
                for b in boards:
                    try:
                        p = log.write(b, reader(b.port, b.baud), every)
                        if p:
                            self.last_path, self.error = p, None
                    except Exception as e:                       # disk full, file open in Excel…
                        self.error = str(e)
            time.sleep(min(every, 0.5))


_LOGGER: Logger | None = None


def logger() -> Logger:
    global _LOGGER
    with _READERS_LOCK:
        if _LOGGER is None:
            _LOGGER = Logger()
        return _LOGGER
