# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ivan De Cesaris, IZ0JNY
"""NanoHam PC: the NanoHam boards' measurements on the PC, in EMELab's style (Streamlit).
Run with  streamlit run nanoham_main.py  (or  python -m nanoham_pc.desktop  for its own window).

Pages: 🔴 Misure (values, bars, alarms of every board), 📈 Grafici (graphs chosen by the user),
⚙️ Configurazione (boards, ports, channel names, units, divider ratios, thresholds, graphs), 📒 Log (CSV)."""
from __future__ import annotations

import html
import time
from datetime import datetime, timezone

import streamlit as st

import nanoham_pc
from nanoham_pc import link, settings

S = st.session_state
CFG: dict = {}                    # settings, one copy for the whole program (all windows see the same)
OK, BAD, OFF = "#2f9e44", "#e03131", "#868e96"
GRAPH_MIN = [1, 5, 15, 30, 60]


def _frag(run_every):
    f = getattr(st, "fragment", None)
    return f(run_every=run_every) if f else (lambda fn: fn)


def _save():
    try:
        settings.save(CFG)
    except Exception as e:
        st.warning(f"Impostazioni non salvate: {e}")
    _apply()


def _apply():
    """Readers for the enabled boards, none for the others; the CSV log follows the settings."""
    want = {b.port for b in CFG["boards"] if b.enabled}
    for p in link.active_ports():
        if p not in want:
            link.stop_reader(p)
    for b in CFG["boards"]:
        if b.enabled:
            link.reader(b.port, b.baud)
    link.logger().configure(CFG["boards"], CFG["log_dir"] if CFG["log_on"] else None, CFG["log_every_s"])


def _series_key(b, c):
    return f"{b.name}|{c.n}"


def _board_graphs(b):
    """One graph per unit of a board (kV, V, °C, states…), with its active channels."""
    groups: dict[str, list] = {}
    for c in b.channels:
        if c.enabled:
            groups.setdefault("stati" if c.logic else c.unit, []).append(_series_key(b, c))
    return [{"title": f"{b.name} · {u}", "series": ser, "height": 260 if u == "stati" else 320}
            for u, ser in groups.items()] or [{"title": b.name, "series": [], "height": 320}]


def _default_graphs():
    return [g for b in CFG["boards"] for g in _board_graphs(b)]


# ---------------------------------------------------------------- sidebar
def _sidebar():
    with st.sidebar:
        st.title("📟 NanoHam PC")
        st.subheader("Misure in fibra ottica")
        st.caption("Software progettato da Ivan IZ0JNY")
        st.divider()
        for b in CFG["boards"]:
            if not b.enabled:
                st.markdown(f"⚪ **{html.escape(b.name)}** · {b.port} · spenta")
                continue
            rd = link.reader(b.port, b.baud)
            stt = rd.status()
            dot = {"LINK": "🟢", "NO DATA": "🟠"}.get(stt, "🔴")
            st.markdown(f"{dot} **{html.escape(b.name)}** · {b.port} · {stt}")
        st.divider()
        st.caption(f"Versione {nanoham_pc.__version__} · © Ivan De Cesaris IZ0JNY · orari in UTC")


# ---------------------------------------------------------------- 🔴 Misure
def _tile(b, c, v, alarm):
    color = OFF if v is None else (BAD if alarm else OK)
    if c.logic:
        lit = v is not None and v >= 0.5
        bg = color if lit else "transparent"
        return (f"<div style='border:2px solid {color};border-radius:10px;padding:10px 14px;margin-bottom:8px'>"
                f"<div style='font-size:0.9rem;opacity:.8'>CH{c.n} · {html.escape(c.name)}</div>"
                f"<div style='display:inline-block;margin:6px 0;padding:4px 16px;border-radius:16px;font-size:1.6rem;"
                f"font-weight:700;border:2px solid {color};background:{bg};color:{'#fff' if lit else color}'>"
                f"{html.escape(c.fmt(v))}</div>"
                f"<div style='font-size:0.8rem;color:{BAD if alarm else 'inherit'};opacity:.85'>"
                f"{html.escape('🚨 ' + alarm if alarm else ('allarme se ' + (c.on_text if c.alarm_state == 'ON' else c.off_text) if c.alarm_state else 'nessun allarme'))}"
                f"</div></div>")
    pct = 0 if v is None or not c.full_scale else max(0.0, min(100.0, 100 * v / c.full_scale))
    lim = " · ".join(x for x in ((f"min {c.fmt(c.alarm_min)}" if c.alarm_min is not None else ""),
                                 (f"max {c.fmt(c.alarm_max)}" if c.alarm_max is not None else "")) if x)
    return (f"<div style='border:2px solid {color};border-radius:10px;padding:10px 14px;margin-bottom:8px'>"
            f"<div style='font-size:0.9rem;opacity:.8'>CH{c.n} · {html.escape(c.name)}</div>"
            f"<div style='font-size:2.2rem;font-weight:700;color:{color};font-variant-numeric:tabular-nums'>"
            f"{c.fmt(v)} <span style='font-size:1.1rem'>{html.escape(c.unit)}</span></div>"
            f"<div style='background:#8883;border-radius:4px;height:8px'><div style='width:{pct:.1f}%;height:8px;"
            f"border-radius:4px;background:{color}'></div></div>"
            f"<div style='font-size:0.8rem;margin-top:4px;color:{BAD if alarm else 'inherit'};opacity:.85'>"
            f"{html.escape('🚨 ' + alarm if alarm else (lim or 'nessuna soglia'))}</div></div>")


@_frag(0.25)
def _live():
    now = time.time()
    active = S.setdefault("alarms_on", {})
    for b in CFG["boards"]:
        if not b.enabled:
            continue
        rd = link.reader(b.port, b.baud)
        stt = rd.status(now)
        color = {"LINK": OK, "NO DATA": "#f08c00"}.get(stt, BAD)
        info = f"{rd.lines_per_s(now):.0f} righe/s · {rd.lines_ok} ok · {rd.lines_bad} scartate"
        if rd.error and not rd.connected:
            info = f"porta non aperta: {html.escape(rd.error)} (riprovo ogni 2 s)"
        st.markdown(f"#### {html.escape(b.name)} <span style='color:{color};font-size:1rem'>● {stt}</span> "
                    f"<span style='font-size:0.85rem;opacity:.7'>{b.port} · {b.baud} baud · {info}</span>",
                    unsafe_allow_html=True)
        chans = [c for c in b.channels if c.enabled]
        per_row = 4
        cols = [col for i in range(0, len(chans), per_row) for col in st.columns(per_row)]
        for col, c in zip(cols, chans):
            r = rd.raw(c.n, now)
            v = None if r is None else c.value(r)
            a = link.alarm_of(c, v)
            key = _series_key(b, c)
            if a and not active.get(key):
                st.toast(f"🚨 {b.name} · {c.name}: {a}", icon="🚨")
            active[key] = bool(a)
            col.markdown(_tile(b, c, v, a), unsafe_allow_html=True)


# ---------------------------------------------------------------- 📈 Grafici
def _figure(g, minutes, now):
    import plotly.graph_objects as go
    boards = {b.name: b for b in CFG["boards"]}
    fig = go.Figure()
    units = []
    for key in g.get("series") or []:
        name, _, n = key.rpartition("|")
        b = boards.get(name)
        c = b.channel(int(n)) if b and n.isdigit() else None
        if not c or not b.enabled:
            continue
        unit = "stato (0/1)" if c.logic else c.unit
        if unit not in units:
            units.append(unit)
        pts = link.reader(b.port, b.baud).series(c.n, minutes * 60, now)
        axis = "y" if units.index(unit) == 0 else "y2"
        fig.add_trace(go.Scatter(x=[datetime.fromtimestamp(t, timezone.utc) for t, _ in pts],
                                 y=[c.value(v) for _, v in pts], mode="lines", line_shape="hv" if c.logic else "linear",
                                 name=f"{b.name} · {c.name}" + ("" if c.logic else f" ({c.unit})"), yaxis=axis))
    lay = dict(height=int(g.get("height") or 320), margin=dict(l=10, r=10, t=40, b=10), title=g.get("title") or "",
               legend=dict(orientation="h", y=-0.15), uirevision=g.get("title"),
               yaxis=dict(title=units[0] if units else ""))
    if len(units) > 1:
        lay["yaxis2"] = dict(title=" / ".join(units[1:]), overlaying="y", side="right", showgrid=False)
    fig.update_layout(**lay)
    return fig


@_frag(1.0)
def _graphs():
    now = time.time()
    if not CFG["graphs"]:
        st.info("Nessun grafico: aggiungilo in ⚙️ Configurazione.")
    for i, g in enumerate(CFG["graphs"]):
        st.plotly_chart(_figure(g, CFG["graph_min"], now), key=f"graph_{i}", width="stretch")


# ---------------------------------------------------------------- ⚙️ Configurazione
CH_COLS = {"n": "CH", "name": "Nome", "kind": "Tipo", "unit": "Unità", "scale": "Rapporto (partitore)", "offset": "Offset",
           "decimals": "Decimali", "full_scale": "Fondo scala", "alarm_min": "Allarme sotto", "alarm_max": "Allarme sopra",
           "alarm_nodata": "Allarme NO DATA", "on_text": "Testo ON", "off_text": "Testo OFF",
           "alarm_state": "Allarme se (logico)", "enabled": "Attivo"}


def _board_editor(i, b):
    ports = link.serial_ports()
    names = [link.DEMO_PORT] + [p for p, _ in ports]
    if b.port not in names:
        names.append(b.port)
    desc = dict(ports)
    with st.expander(f"📟 {b.name} · {b.port}", expanded=len(CFG["boards"]) == 1):
        c = st.columns([3, 3, 2, 1])
        name = c[0].text_input("Nome (es. banda)", b.name, key=f"b_name_{i}")
        port = c[1].selectbox("Porta COM", names, index=names.index(b.port), key=f"b_port_{i}",
                              format_func=lambda p: "DEMO · NanoHam simulata" if p == link.DEMO_PORT
                              else (f"{p} · {desc[p]}" if desc.get(p) else p),
                              help="la porta del cavo USB-TTL collegato al modulo in fibra (TEL0153)")
        baud = c[2].selectbox("Baud", [115200, 57600, 38400, 19200, 9600], index=[115200, 57600, 38400, 19200, 9600]
                              .index(b.baud) if b.baud in (115200, 57600, 38400, 19200, 9600) else 0, key=f"b_baud_{i}")
        en = c[3].checkbox("Accesa", b.enabled, key=f"b_en_{i}")
        rows = [{k: getattr(ch, k) for k in CH_COLS} for ch in b.channels]
        st.caption("Valore mostrato = valore ricevuto × rapporto + offset (rapporto = partitore, es. 10 per 1:10). "
                   "Lascia vuote le soglie che non usi. Tipo «logic»: ricevuto ≥ 0.5 = ON, con i tuoi testi "
                   "(es. PTT / RX) e l'allarme nello stato che scegli. Aggiungi una riga per un canale in più (CH9…).")
        ed = st.data_editor(rows, key=f"b_ch_{i}", num_rows="dynamic", hide_index=True, width="stretch",
                            column_config={k: st.column_config.Column(v) for k, v in CH_COLS.items()} | {
                                "n": st.column_config.NumberColumn("CH", min_value=1, max_value=99, step=1, required=True),
                                "decimals": st.column_config.NumberColumn("Decimali", min_value=0, max_value=6, step=1),
                                "scale": st.column_config.NumberColumn("Rapporto (partitore)", format="%.4f"),
                                "alarm_nodata": st.column_config.CheckboxColumn("Allarme NO DATA"),
                                "kind": st.column_config.SelectboxColumn("Tipo", options=["analog", "logic"],
                                                                         required=True,
                                                                         help="analog: tensioni, correnti, temperature; "
                                                                              "logic: stato ON/OFF (≥ 0.5 = ON)"),
                                "alarm_state": st.column_config.SelectboxColumn("Allarme se (logico)",
                                                                                options=["", "ON", "OFF"]),
                                "enabled": st.column_config.CheckboxColumn("Attivo")})
        c = st.columns([1, 1, 4])
        if c[0].button("💾 Salva", key=f"b_save_{i}", type="primary"):
            if any(x.name == name and j != i for j, x in enumerate(CFG["boards"])):
                st.error("C'è già una scheda con questo nome.")
                return
            if any(x.port == port and x.enabled and j != i for j, x in enumerate(CFG["boards"])) and port != link.DEMO_PORT:
                st.error("Questa porta è già usata da un'altra scheda.")
                return
            chans, seen = [], set()
            for r in ed:
                if r.get("n") is None or int(r["n"]) in seen:
                    continue
                seen.add(int(r["n"]))
                d = link.Channel(int(r["n"]))
                for k in CH_COLS:
                    if k != "n" and r.get(k) is not None and r.get(k) == r.get(k):      # skips NaN
                        setattr(d, k, type(getattr(d, k))(r[k]) if getattr(d, k) is not None else float(r[k]))
                d.name = d.name or f"CH{d.n}"
                chans.append(d)
            old = b.name
            CFG["boards"][i] = link.Board(name=name.strip() or b.name, port=port, baud=int(baud), enabled=en,
                                          channels=sorted(chans, key=lambda x: x.n))
            for g in CFG["graphs"]:                        # renamed board: its graphs follow it
                g["series"] = [f"{CFG['boards'][i].name}|{s.rpartition('|')[2]}" if s.rpartition("|")[0] == old else s
                               for s in g["series"]]
            _save()
            st.rerun()
        if c[1].button("🗑️ Elimina", key=f"b_del_{i}", disabled=len(CFG["boards"]) == 1):
            CFG["boards"].pop(i)
            _save()
            st.rerun()


def _graph_editor():
    st.markdown("#### 📈 Grafici")
    all_keys = {_series_key(b, c): f"{b.name} · CH{c.n} {c.name} ({c.unit})" for b in CFG["boards"] for c in b.channels}
    for i, g in enumerate(CFG["graphs"]):
        with st.expander(f"📈 {g.get('title') or 'Grafico'}"):
            c = st.columns([3, 1])
            title = c[0].text_input("Titolo", g.get("title", ""), key=f"g_t_{i}")
            h = c[1].number_input("Altezza (px)", 150, 1200, int(g.get("height") or 320), 10, key=f"g_h_{i}")
            ser = st.multiselect("Canali nel grafico (unità diverse: secondo asse a destra)", list(all_keys),
                                 [s for s in g.get("series", []) if s in all_keys], format_func=all_keys.get,
                                 key=f"g_s_{i}")
            c = st.columns([1, 1, 1, 3])
            if c[0].button("💾 Salva", key=f"g_save_{i}", type="primary"):
                CFG["graphs"][i] = {"title": title, "series": ser, "height": int(h)}
                _save()
                st.rerun()
            if c[1].button("⬆️ Su", key=f"g_up_{i}", disabled=i == 0):
                CFG["graphs"].insert(i - 1, CFG["graphs"].pop(i))
                _save()
                st.rerun()
            if c[2].button("🗑️ Elimina", key=f"g_del_{i}"):
                CFG["graphs"].pop(i)
                _save()
                st.rerun()
    c = st.columns([1, 1, 3])
    if c[0].button("➕ Aggiungi un grafico", key="g_add"):
        CFG["graphs"].append({"title": f"Grafico {len(CFG['graphs']) + 1}", "series": [], "height": 320})
        _save()
        st.rerun()
    if c[1].button("↩️ Grafici predefiniti", key="g_reset", help="rifà i grafici: per ogni scheda uno per unità (kV, V, °C, stati)"):
        CFG["graphs"] = _default_graphs()
        _save()
        st.rerun()


def _config():
    st.markdown("#### 📟 Schede NanoHam")
    st.caption("Una scheda per banda (10, 24, 47 GHz…), ognuna sulla sua porta COM. Protocollo: righe «CH<n>,<valore>» "
               "a 115200 baud, come la reading board NanoHam. «DEMO» simula una scheda per provare il programma.")
    for i, b in enumerate(list(CFG["boards"])):
        _board_editor(i, b)
    if st.button("➕ Aggiungi una scheda", key="b_add"):
        used = {b.name for b in CFG["boards"]}
        name = next(n for n in ["10 GHz", "24 GHz", "47 GHz", "76 GHz"] + [f"NanoHam {k}" for k in range(2, 99)]
                    if n not in used)
        nb = link.Board(name=name, port=link.DEMO_PORT, enabled=False)
        CFG["boards"].append(nb)
        CFG["graphs"] += _board_graphs(nb)
        _save()
        st.rerun()
    st.divider()
    _graph_editor()


# ---------------------------------------------------------------- 📒 Log
def _log_page():
    st.markdown("#### 📒 Registrazione su file CSV")
    st.caption("Un file per scheda e per giorno (UTC), separatore «;», valori già scalati, con gli allarmi attivi. "
               "Si apre con Excel o LibreOffice. Continua a registrare anche se cambi pagina.")
    c = st.columns([1, 4, 1])
    on = c[0].toggle("Registra", CFG["log_on"], key="log_on")
    folder = c[1].text_input("Cartella", CFG["log_dir"], key="log_dir")
    every = c[2].number_input("Ogni (s)", 0.1, 3600.0, float(CFG["log_every_s"]), 0.5, key="log_every")
    if (on, folder, every) != (CFG["log_on"], CFG["log_dir"], CFG["log_every_s"]):
        CFG.update(log_on=on, log_dir=folder, log_every_s=every)
        _save()
    lg = link.logger()
    if lg.error:
        st.error(f"Errore di scrittura: {lg.error}")
    elif on and lg.last_path:
        st.success(f"Sto scrivendo in {lg.last_path}")
    log = link.CsvLog(folder)
    for b in CFG["boards"]:
        p = log.path(b)
        if p.exists():
            st.download_button(f"⬇️ {p.name}", p.read_bytes(), p.name, "text/csv", key=f"dl_{b.name}")


def main():
    """Builds the page; nanoham_main.py calls it at every run (an imported module runs only once)."""
    st.set_page_config(page_title="NanoHam PC — misure in fibra ottica", page_icon="📟", layout="wide")
    if not CFG:
        CFG.update(settings.load())
    if "graphs" not in CFG:
        CFG["graphs"] = _default_graphs()
    _apply()
    _sidebar()
    tabs = st.tabs(["🔴 Misure", "📈 Grafici", "⚙️ Configurazione", "📒 Log"])
    with tabs[0]:
        _live()
    with tabs[1]:
        st.select_slider("Ultimi minuti", GRAPH_MIN, value=CFG["graph_min"] if CFG["graph_min"] in GRAPH_MIN else 5,
                         key="graph_min_sel", on_change=lambda: (CFG.update(graph_min=S.graph_min_sel), _save()))
        _graphs()
    with tabs[2]:
        _config()
    with tabs[3]:
        _log_page()
