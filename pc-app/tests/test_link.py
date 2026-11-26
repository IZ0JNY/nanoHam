# SPDX-License-Identifier: GPL-3.0-or-later
import csv
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from nanoham_pc import link, settings  # noqa: E402


def test_parse_line():
    assert link.parse_line(b"CH1,7.532\n") == (1, 7.532)
    assert link.parse_line("CH4,0.000\r\n") == (4, 0.0)
    assert link.parse_line("CH12,-1.5") == (12, -1.5)
    for bad in ("", "CH1", "CH,1.0", "CH1;2.0", "Modulo 0x48 trovato", "CH1=7.5", "CH1,abc", "CH1,nan"):
        assert link.parse_line(bad) is None


class _Quiet:
    def readline(self):
        time.sleep(0.05)
        return b""

    def close(self):
        pass


def _rd():
    return link.Reader("TEST", opener=lambda p, b: _Quiet())


def test_port_error_status():
    r = link.Reader("COM_NONE", opener=lambda p, b: (_ for _ in ()).throw(OSError("no port")))
    time.sleep(0.1)
    r.stop()
    assert r.status() == "ERRORE" and "no port" in r.error


def test_reader_latest_and_stale():
    r = _rd()
    r.stop()
    t = 1000.0
    assert r.feed("CH1,5.000", t)
    assert not r.feed("garbage", t)
    assert r.raw(1, t + 0.5) == 5.0
    assert r.raw(1, t + 1.5) is None                  # no data for > 1 s → NO DATA
    assert r.lines_ok == 1 and r.lines_bad == 1
    assert r.status(t + 0.5) == "LINK"


def test_scale_and_alarms():
    c = link.Channel(2, "V Master", "V", scale=10.0, offset=0.5, decimals=2, alarm_min=10, alarm_max=30)
    assert c.value(1.2) == 12.5
    assert link.alarm_of(c, 12.5) is None
    assert "> 30.00" in link.alarm_of(c, 31)
    assert "< 10.00" in link.alarm_of(c, 9)
    assert link.alarm_of(c, None) == "NO DATA"
    c.alarm_nodata = False
    assert link.alarm_of(c, None) is None


def test_logic_channel():
    c = link.Channel(7, "PTT", kind="logic", on_text="TX", off_text="RX", alarm_state="ON")
    assert c.value(0.9) == 1.0 and c.value(0.1) == 0.0
    assert c.fmt(1.0) == "TX" and c.fmt(0.0) == "RX"
    assert link.alarm_of(c, 1.0) == "TX"
    assert link.alarm_of(c, 0.0) is None
    c.alarm_state = ""
    assert link.alarm_of(c, 1.0) is None


def test_board_roundtrip(tmp_path, monkeypatch):
    monkeypatch.setenv("NANOHAM_PC_HOME", str(tmp_path))
    d = settings.load()
    assert [c.name for c in d["boards"][0].channels][:4] == ["V Helix", "V Master", "V Slave", "V Aux"]
    d["boards"][0].channels[1].alarm_max = 15.0
    d["boards"].append(link.Board(name="24 GHz", port="COM9"))
    settings.save(d)
    e = settings.load()
    assert [b.name for b in e["boards"]] == ["10 GHz", "24 GHz"]
    assert e["boards"][0].channels[1].alarm_max == 15.0
    assert e["boards"][1].port == "COM9"


def test_csv_log(tmp_path):
    r = _rd()
    r.stop()
    now = time.time()
    r.feed("CH1,7.000", now)
    r.feed("CH2,25.000", now)
    b = link.Board(name="10 GHz")
    b.channels[1].alarm_max = 24.0
    log = link.CsvLog(tmp_path)
    p = log.write(b, r, 1.0, now)
    assert log.write(b, r, 1.0, now + 0.2) is None          # too soon
    rows = list(csv.reader(open(p, encoding="utf-8"), delimiter=";"))
    assert rows[0][:3] == ["UTC", "V Helix (kV)", "V Master (V)"]
    assert rows[1][1:3] == ["7.000", "25.000"]
    assert "V Master" in rows[1][-1] and "V Slave: NO DATA" in rows[1][-1]


def test_demo_reader():
    r = link.Reader(link.DEMO_PORT)
    time.sleep(0.5)
    r.stop()
    assert r.status() == "LINK"
    assert 6.5 < r.raw(1) < 7.5
    assert r.raw(8) == 0.0
