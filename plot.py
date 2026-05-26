#!/usr/bin/env python3
"""
Bik'air — Log processing and visualization toolkit.

Usage:
    python plot.py <file_or_directory> [options]

Examples:
    python plot.py 01052025.TXT
    python plot.py logs/                  # directory -> aggregate report + one per ride
    python plot.py logs/ --weather        # enrich with OpenMeteo weather
    python plot.py log.json --smooth 10   # smoothing over 10 points

Output:
    ./output/index.html         dashboard (or multi-ride aggregate report)
    ./output/<session>.html     per-ride detail
    ./output/data/*.gpx,*.csv   GPS / tabular exports
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime
from pathlib import Path
from typing import Optional

import numpy as np
import pandas as pd
import plotly.graph_objects as go
from plotly.subplots import make_subplots
import folium
from folium.plugins import HeatMap


# =============================================================================
# Constants
# =============================================================================

NUMERIC_COLS = [
    "temperature", "humidity", "tvoc", "co2", "pm1", "pm2",
    "aqi", "speed", "latitude", "longitude", "altitude", "satellites",
]

# AQI scale (ENS160: 1 = Excellent ... 5 = Poor)
AQI_INFO = {
    1: ("Excellent", "#00b894"),
    2: ("Bon",       "#74d36a"),
    3: ("Moyen",     "#ffd43b"),
    4: ("Médiocre",  "#ff8a3d"),
    5: ("Mauvais",   "#e94e3a"),
}

# WHO 2021 thresholds for PM2.5
WHO_PM25_24H = 15.0      # µg/m³ — 24h mean
WHO_PM25_ANNUAL = 5.0    # µg/m³ — annual mean

# Indoor CO2 thresholds (indicative)
CO2_GOOD = 1000
CO2_BAD = 1500

PLOTLY_TEMPLATE = "plotly_dark"


# =============================================================================
# Data loading
# =============================================================================

def detect_format(path: Path) -> str:
    """Guess the format by reading the start of the file."""
    try:
        with path.open("rb") as f:
            head = f.read(2048).decode("utf-8", errors="ignore").lstrip()
    except OSError:
        return "empty"
    if not head:
        return "empty"
    if head.startswith("["):
        return "json_array"
    if head.startswith("{"):
        return "jsonl"
    if "," in head.splitlines()[0]:
        return "csv"
    return "unknown"


def load_file(path: Path) -> pd.DataFrame:
    """Load a file, auto-detecting its format."""
    fmt = detect_format(path)
    if fmt == "empty":
        return pd.DataFrame()

    if fmt == "json_array":
        try:
            data = json.loads(path.read_text(encoding="utf-8", errors="ignore"))
            if isinstance(data, list):
                return pd.DataFrame(data)
        except json.JSONDecodeError:
            # May be a truncated JSON array — fall back to line-by-line parsing
            pass
        fmt = "jsonl"

    if fmt == "jsonl":
        rows = []
        with path.open("r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                line = line.strip().rstrip(",")
                if not line or line in ("[", "]"):
                    continue
                try:
                    rows.append(json.loads(line))
                except json.JSONDecodeError:
                    continue
        return pd.DataFrame(rows)

    if fmt == "csv":
        try:
            return pd.read_csv(path)
        except Exception:
            return pd.DataFrame()

    return pd.DataFrame()


def load_path(input_path: Path) -> list[tuple[str, pd.DataFrame]]:
    """Load a single file or every log in a directory."""
    out: list[tuple[str, pd.DataFrame]] = []
    if input_path.is_dir():
        for p in sorted(input_path.iterdir()):
            if p.suffix.lower() in (".json", ".txt", ".csv") and p.name.lower() not in ("interval.json",):
                df = load_file(p)
                if not df.empty:
                    out.append((p.stem, df))
    else:
        df = load_file(input_path)
        if not df.empty:
            out.append((input_path.stem, df))
    return out


# =============================================================================
# Cleaning and normalization
# =============================================================================

def normalize(df: pd.DataFrame) -> pd.DataFrame:
    """Convert types and sort by time."""
    if df.empty:
        return df
    df = df.copy()
    if "time_utc" in df.columns:
        df["time_utc"] = pd.to_datetime(df["time_utc"], errors="coerce", utc=False, format="mixed")
    for col in NUMERIC_COLS:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors="coerce")
    if "time_utc" in df.columns:
        df = df.sort_values("time_utc").reset_index(drop=True)
    return df


def remove_outliers(df: pd.DataFrame, zscore_threshold: float = 3.5) -> pd.DataFrame:
    """Mask outliers via a robust rolling-median Z-score."""
    if df.empty:
        return df
    out = df.copy()
    for col in ("temperature", "humidity", "tvoc", "co2", "pm1", "pm2", "speed"):
        if col not in out.columns:
            continue
        s = out[col].astype(float)
        # Impossible values
        if col in ("pm1", "pm2", "co2", "tvoc"):
            s = s.where(s >= 0)
        if col == "humidity":
            s = s.where((s >= 0) & (s <= 100))
        if col == "temperature":
            s = s.where((s > -40) & (s < 80))
        # Robust Z-score
        med = s.rolling(window=11, min_periods=3, center=True).median()
        mad = (s - med).abs().rolling(window=11, min_periods=3, center=True).median()
        scale = mad.replace(0, np.nan) * 1.4826
        z = (s - med) / scale
        out[col] = s.where(z.abs().fillna(0) < zscore_threshold)
    return out


def smooth(df: pd.DataFrame, window: int = 5) -> pd.DataFrame:
    """Add smoothed `<col>_smooth` columns (rolling mean)."""
    if df.empty or window <= 1:
        return df
    out = df.copy()
    for col in ("temperature", "humidity", "tvoc", "co2", "pm1", "pm2", "aqi"):
        if col in out.columns:
            out[col + "_smooth"] = out[col].rolling(window=window, min_periods=1, center=True).mean()
    return out


# =============================================================================
# Session splitting
# =============================================================================

def split_sessions(df: pd.DataFrame, gap_minutes: int = 5, min_points: int = 5) -> list[pd.DataFrame]:
    """Split the DataFrame wherever the time gap exceeds `gap_minutes`."""
    if df.empty or "time_utc" not in df.columns:
        return [df] if not df.empty else []
    df = df.dropna(subset=["time_utc"]).sort_values("time_utc").reset_index(drop=True)
    if df.empty:
        return []
    dt = df["time_utc"].diff().dt.total_seconds() / 60
    session_id = (dt > gap_minutes).cumsum()
    sessions = [g.reset_index(drop=True) for _, g in df.groupby(session_id, sort=False)]
    return [s for s in sessions if len(s) >= min_points]


# =============================================================================
# Stats
# =============================================================================

def haversine_m(lat1, lon1, lat2, lon2):
    R = 6371000.0
    phi1, phi2 = np.radians(lat1), np.radians(lat2)
    dphi = np.radians(lat2 - lat1)
    dlam = np.radians(lon2 - lon1)
    a = np.sin(dphi / 2) ** 2 + np.cos(phi1) * np.cos(phi2) * np.sin(dlam / 2) ** 2
    return 2 * R * np.arcsin(np.sqrt(a))


def compute_stats(df: pd.DataFrame) -> dict:
    s: dict = {}
    if df.empty:
        return s
    if "time_utc" in df.columns and df["time_utc"].notna().any():
        s["start"] = df["time_utc"].min()
        s["end"] = df["time_utc"].max()
        s["duration_sec"] = (s["end"] - s["start"]).total_seconds()
    else:
        s["start"] = s["end"] = None
        s["duration_sec"] = 0
    s["points"] = len(df)

    # Distance + derived speed
    if "latitude" in df.columns and "longitude" in df.columns:
        gps = df.dropna(subset=["latitude", "longitude"])
        gps = gps[(gps["latitude"] != 0) & (gps["longitude"] != 0)]
        if len(gps) >= 2:
            lat = gps["latitude"].values
            lon = gps["longitude"].values
            dist = haversine_m(lat[:-1], lon[:-1], lat[1:], lon[1:])
            # Filter out spurious GPS jumps (> 200 m between samples)
            dist = dist[dist < 200]
            s["distance_m"] = float(dist.sum())
        else:
            s["distance_m"] = 0.0
    else:
        s["distance_m"] = 0.0

    # Per-pollutant stats
    for col in ("temperature", "humidity", "tvoc", "co2", "pm1", "pm2", "aqi", "speed"):
        if col in df.columns and df[col].notna().any():
            s[col] = {
                "min": float(df[col].min()),
                "max": float(df[col].max()),
                "mean": float(df[col].mean()),
                "median": float(df[col].median()),
                "p95": float(df[col].quantile(0.95)),
            }

    # Exposure dose: ∫ value × dt, in (unit × minutes)
    if "time_utc" in df.columns and df["time_utc"].notna().any() and s.get("duration_sec", 0) > 0:
        dt = df["time_utc"].diff().dt.total_seconds().fillna(0).clip(lower=0, upper=60)
        for col in ("pm2", "pm1", "co2", "tvoc"):
            if col in df.columns:
                val = df[col].fillna(0)
                dose = (val * dt / 60).sum()
                s.setdefault(col, {})["dose"] = float(dose)

    # WHO thresholds — PM2.5
    if "pm2" in df.columns:
        pm = df["pm2"].dropna()
        if len(pm) > 0:
            s["who_pm25_pct_24h"] = float((pm > WHO_PM25_24H).mean() * 100)
            s["who_pm25_pct_annual"] = float((pm > WHO_PM25_ANNUAL).mean() * 100)

    # AQI distribution (% of time in each level)
    if "aqi" in df.columns and df["aqi"].notna().any():
        aqi = df["aqi"].dropna().round().astype(int).clip(1, 5)
        breakdown = {lvl: float((aqi == lvl).mean() * 100) for lvl in range(1, 6)}
        s["aqi_time_pct"] = breakdown
        s["aqi_mean"] = float(aqi.mean())

    return s


def aggregate_stats(per_session_stats: list[dict]) -> dict:
    """Aggregate several per-session stats into a global summary."""
    agg = {
        "sessions": len(per_session_stats),
        "total_distance_m": sum(s.get("distance_m", 0) for s in per_session_stats),
        "total_duration_sec": sum(s.get("duration_sec", 0) for s in per_session_stats),
        "total_points": sum(s.get("points", 0) for s in per_session_stats),
    }
    for col in ("pm2", "pm1", "co2", "tvoc", "aqi"):
        vals = [s[col]["mean"] for s in per_session_stats if col in s and "mean" in s[col]]
        doses = [s[col].get("dose", 0) for s in per_session_stats if col in s]
        if vals:
            agg[col] = {
                "mean_of_means": float(np.mean(vals)),
                "max_seen": float(max(s[col]["max"] for s in per_session_stats if col in s)),
                "total_dose": float(sum(doses)),
            }
    return agg


# =============================================================================
# Peaks
# =============================================================================

def detect_peaks(df: pd.DataFrame, col: str, top_n: int = 5) -> pd.DataFrame:
    """Return the `top_n` most prominent peaks of a column."""
    if df.empty or col not in df.columns or df[col].notna().sum() < 10:
        return pd.DataFrame()
    try:
        from scipy.signal import find_peaks
    except ImportError:
        # Simple fallback: top N values above the 95th percentile
        thr = df[col].quantile(0.95)
        return df.nlargest(top_n, col)[df.nlargest(top_n, col)[col] > thr]
    s = df[col].interpolate().bfill().ffill()
    if s.empty:
        return pd.DataFrame()
    std = s.std()
    if not std or pd.isna(std):
        return pd.DataFrame()
    idx, props = find_peaks(s.values, prominence=std * 1.5, distance=10)
    if len(idx) == 0:
        return pd.DataFrame()
    peaks = df.iloc[idx].copy()
    peaks["_prom"] = props["prominences"]
    return peaks.nlargest(top_n, "_prom").drop(columns="_prom")


# =============================================================================
# Map
# =============================================================================

def build_map(df: pd.DataFrame, peaks: Optional[dict] = None, heatmap: bool = True) -> Optional[folium.Map]:
    if df.empty or "latitude" not in df.columns or "longitude" not in df.columns:
        return None
    gps = df.dropna(subset=["latitude", "longitude"]).copy()
    gps = gps[(gps["latitude"] != 0) & (gps["longitude"] != 0)]
    if gps.empty:
        return None

    center = [gps["latitude"].mean(), gps["longitude"].mean()]
    m = folium.Map(location=center, zoom_start=14, tiles="OpenStreetMap",
                   control_scale=True)

    # Track coloured by AQI (segments)
    if "aqi" in gps.columns and gps["aqi"].notna().any():
        cur_color = None
        cur_seg: list[list[float]] = []
        groups: list[tuple[str, list[list[float]]]] = []
        for _, row in gps.iterrows():
            aqi_v = row.get("aqi")
            aqi = int(aqi_v) if pd.notna(aqi_v) else 1
            aqi = max(1, min(5, aqi))
            color = AQI_INFO[aqi][1]
            if color != cur_color and cur_seg:
                groups.append((cur_color, cur_seg))
                cur_seg = [cur_seg[-1]]  # visual continuity between segments
            cur_color = color
            cur_seg.append([row["latitude"], row["longitude"]])
        if cur_seg:
            groups.append((cur_color, cur_seg))
        trace_layer = folium.FeatureGroup(name="Trace (AQI)", show=True)
        for color, coords in groups:
            if len(coords) >= 2:
                folium.PolyLine(coords, color=color, weight=5, opacity=0.85).add_to(trace_layer)
        trace_layer.add_to(m)
    else:
        coords = list(zip(gps["latitude"], gps["longitude"]))
        folium.PolyLine(coords, color="red", weight=4).add_to(m)

    # Heatmap PM2.5
    if heatmap and "pm2" in gps.columns:
        pts = gps[["latitude", "longitude", "pm2"]].dropna()
        if not pts.empty and pts["pm2"].max() > 0:
            heat = pts.values.tolist()
            HeatMap(heat, radius=18, blur=22, min_opacity=0.35,
                    name="Heatmap PM2.5", show=False).add_to(m)

    # Heatmap CO2
    if heatmap and "co2" in gps.columns:
        pts = gps[["latitude", "longitude", "co2"]].dropna()
        if not pts.empty and pts["co2"].max() > 0:
            heat = pts.values.tolist()
            HeatMap(heat, radius=18, blur=22, min_opacity=0.35,
                    name="Heatmap CO₂", show=False).add_to(m)

    # Start / Finish
    folium.Marker(
        [gps["latitude"].iloc[0], gps["longitude"].iloc[0]],
        popup="🚀 Départ", icon=folium.Icon(color="green", icon="play"),
    ).add_to(m)
    folium.Marker(
        [gps["latitude"].iloc[-1], gps["longitude"].iloc[-1]],
        popup="🏁 Arrivée", icon=folium.Icon(color="red", icon="stop"),
    ).add_to(m)

    # Peak markers
    if peaks:
        peak_layer = folium.FeatureGroup(name="Pics détectés", show=True)
        for col, pdf in peaks.items():
            if pdf is None or pdf.empty:
                continue
            for _, row in pdf.iterrows():
                if pd.isna(row.get("latitude")) or pd.isna(row.get("longitude")):
                    continue
                val = row.get(col)
                time_str = ""
                if "time_utc" in row and pd.notna(row["time_utc"]):
                    time_str = row["time_utc"].strftime("%H:%M:%S")
                folium.CircleMarker(
                    [row["latitude"], row["longitude"]],
                    radius=8, color="#e94e3a", fill=True, fill_opacity=0.7,
                    popup=f"Pic {col.upper()}<br>{val:.1f}<br>{time_str}",
                ).add_to(peak_layer)
        peak_layer.add_to(m)

    bounds = [
        [gps["latitude"].min(), gps["longitude"].min()],
        [gps["latitude"].max(), gps["longitude"].max()],
    ]
    m.fit_bounds(bounds, padding=(30, 30))
    folium.LayerControl(collapsed=False).add_to(m)
    return m


def build_aggregate_map(sessions: list[tuple[str, pd.DataFrame]]) -> Optional[folium.Map]:
    """Multi-ride map — each track in a distinct colour."""
    palette = ["#4d8dde", "#4bd07a", "#ffd43b", "#ff8a3d", "#e94e3a",
               "#bc6cf2", "#22c1c3", "#fb6f92", "#7c83fd", "#06d6a0"]
    all_pts: list[tuple[float, float]] = []
    m = None
    for i, (name, df) in enumerate(sessions):
        gps = df.dropna(subset=["latitude", "longitude"])
        gps = gps[(gps["latitude"] != 0) & (gps["longitude"] != 0)]
        if gps.empty:
            continue
        if m is None:
            center = [gps["latitude"].mean(), gps["longitude"].mean()]
            m = folium.Map(location=center, zoom_start=12, tiles="OpenStreetMap",
                           control_scale=True)
        color = palette[i % len(palette)]
        coords = list(zip(gps["latitude"], gps["longitude"]))
        folium.PolyLine(coords, color=color, weight=4, opacity=0.8,
                        tooltip=name).add_to(m)
        all_pts.extend(coords)
    if m and all_pts:
        lats = [p[0] for p in all_pts]
        lons = [p[1] for p in all_pts]
        m.fit_bounds([[min(lats), min(lons)], [max(lats), max(lons)]], padding=(30, 30))
    return m


# =============================================================================
# Charts
# =============================================================================

def build_timeseries(df: pd.DataFrame) -> str:
    if df.empty or "time_utc" not in df.columns:
        return ""
    cols = [c for c in ("aqi", "pm2", "pm1", "co2", "tvoc", "temperature", "humidity", "speed") if c in df.columns]
    if not cols:
        return ""
    fig = make_subplots(
        rows=len(cols), cols=1, shared_xaxes=True, vertical_spacing=0.025,
        subplot_titles=[c.upper() for c in cols],
    )
    for i, col in enumerate(cols, 1):
        fig.add_trace(
            go.Scatter(x=df["time_utc"], y=df[col], mode="lines",
                       name=col, opacity=0.35, line=dict(width=1),
                       showlegend=False, hoverinfo="x+y"),
            row=i, col=1,
        )
        smooth_col = col + "_smooth"
        if smooth_col in df.columns:
            fig.add_trace(
                go.Scatter(x=df["time_utc"], y=df[smooth_col], mode="lines",
                           name=f"{col} lissé", line=dict(width=2.5),
                           showlegend=False, hoverinfo="x+y"),
                row=i, col=1,
            )
    fig.update_layout(
        template=PLOTLY_TEMPLATE, height=180 * len(cols),
        margin=dict(t=50, b=30, l=40, r=20),
        hovermode="x unified",
    )
    return fig.to_html(full_html=False, include_plotlyjs="cdn")


def build_correlations(df: pd.DataFrame) -> str:
    pairs = [
        ("speed", "co2"),
        ("speed", "pm2"),
        ("altitude", "pm2"),
        ("temperature", "humidity"),
        ("temperature", "co2"),
        ("speed", "aqi"),
    ]
    valid = [
        (x, y) for x, y in pairs
        if x in df.columns and y in df.columns
        and df[x].notna().sum() > 5 and df[y].notna().sum() > 5
    ]
    if not valid:
        return ""

    rows = (len(valid) + 1) // 2
    fig = make_subplots(
        rows=rows, cols=2,
        subplot_titles=[f"{y.upper()} vs {x.upper()}" for x, y in valid],
    )
    for i, (x, y) in enumerate(valid):
        r = i // 2 + 1
        c = i % 2 + 1
        sub = df[[x, y]].dropna()
        corr = sub.corr().iloc[0, 1] if len(sub) > 1 else 0
        fig.add_trace(
            go.Scatter(x=sub[x], y=sub[y], mode="markers",
                       marker=dict(size=4, opacity=0.5, color="#4d8dde"),
                       showlegend=False, name=f"r={corr:.2f}"),
            row=r, col=c,
        )
        fig.layout.annotations[i].text = f"{y.upper()} vs {x.upper()} (r={corr:.2f})"
    fig.update_layout(
        template=PLOTLY_TEMPLATE, height=300 * rows,
        margin=dict(t=50, b=30, l=40, r=20),
    )
    return fig.to_html(full_html=False, include_plotlyjs="cdn")


def build_aqi_donut(stats: dict) -> str:
    if "aqi_time_pct" not in stats:
        return ""
    labels, values, colors = [], [], []
    for lvl in range(1, 6):
        pct = stats["aqi_time_pct"].get(lvl, 0)
        if pct > 0:
            labels.append(AQI_INFO[lvl][0])
            values.append(pct)
            colors.append(AQI_INFO[lvl][1])
    if not values:
        return ""
    fig = go.Figure(
        go.Pie(labels=labels, values=values, hole=0.45,
               marker=dict(colors=colors), textinfo="label+percent",
               sort=False)
    )
    fig.update_layout(
        template=PLOTLY_TEMPLATE, height=380,
        margin=dict(t=10, b=10, l=10, r=10),
        showlegend=False,
    )
    return fig.to_html(full_html=False, include_plotlyjs="cdn")


def build_session_compare(sessions: list[tuple[str, dict]]) -> str:
    """Bar chart: compare mean AQI / mean PM2.5 / distance across sessions."""
    if not sessions:
        return ""
    names = [n for n, _ in sessions]
    fig = make_subplots(
        rows=1, cols=3,
        subplot_titles=("AQI moyen", "PM2.5 moyen (µg/m³)", "Distance (km)"),
    )
    aqis = [s.get("aqi_mean", 0) for _, s in sessions]
    pms = [s.get("pm2", {}).get("mean", 0) for _, s in sessions]
    dists = [s.get("distance_m", 0) / 1000 for _, s in sessions]
    fig.add_trace(go.Bar(x=names, y=aqis, marker_color="#4d8dde", showlegend=False), 1, 1)
    fig.add_trace(go.Bar(x=names, y=pms, marker_color="#ff8a3d", showlegend=False), 1, 2)
    fig.add_trace(go.Bar(x=names, y=dists, marker_color="#4bd07a", showlegend=False), 1, 3)
    fig.update_layout(template=PLOTLY_TEMPLATE, height=380,
                      margin=dict(t=50, b=80, l=40, r=20))
    return fig.to_html(full_html=False, include_plotlyjs="cdn")


# =============================================================================
# Exports
# =============================================================================

def export_csv(df: pd.DataFrame, out_path: Path):
    if df.empty:
        return
    cols = [c for c in [
        "time_utc", "latitude", "longitude", "altitude", "speed",
        "temperature", "humidity", "co2", "tvoc", "aqi", "pm1", "pm2", "satellites",
    ] if c in df.columns]
    df[cols].to_csv(out_path, index=False)


def export_gpx(df: pd.DataFrame, out_path: Path):
    if df.empty or "latitude" not in df.columns or "longitude" not in df.columns:
        return
    gps = df.dropna(subset=["latitude", "longitude"])
    gps = gps[(gps["latitude"] != 0) & (gps["longitude"] != 0)]
    if gps.empty:
        return
    pts = []
    for _, row in gps.iterrows():
        ele = f"<ele>{row['altitude']}</ele>" if "altitude" in row and pd.notna(row["altitude"]) else ""
        t = ""
        if "time_utc" in row and pd.notna(row["time_utc"]):
            t = f"<time>{row['time_utc'].strftime('%Y-%m-%dT%H:%M:%SZ')}</time>"
        ext = []
        for c in ("aqi", "co2", "pm2", "temperature"):
            if c in row and pd.notna(row[c]):
                ext.append(f"<{c}>{row[c]}</{c}>")
        ext_str = f"<extensions>{''.join(ext)}</extensions>" if ext else ""
        pts.append(
            f'      <trkpt lat="{row["latitude"]}" lon="{row["longitude"]}">'
            f"{ele}{t}{ext_str}</trkpt>"
        )
    gpx = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<gpx version="1.1" creator="Bik\'air" xmlns="http://www.topografix.com/GPX/1/1">\n'
        f"  <metadata><time>{datetime.utcnow().strftime('%Y-%m-%dT%H:%M:%SZ')}</time></metadata>\n"
        f"  <trk><name>{out_path.stem}</name><trkseg>\n"
        + "\n".join(pts)
        + "\n    </trkseg></trk>\n</gpx>\n"
    )
    out_path.write_text(gpx, encoding="utf-8")


# =============================================================================
# Weather enrichment (OpenMeteo Archive)
# =============================================================================

def fetch_weather(df: pd.DataFrame) -> pd.DataFrame:
    try:
        import requests
    except ImportError:
        print("Module `requests` non installé — pas d'enrichissement météo.")
        return df
    if df.empty or "latitude" not in df.columns or "time_utc" not in df.columns:
        return df
    gps = df.dropna(subset=["latitude", "longitude", "time_utc"])
    gps = gps[(gps["latitude"] != 0) & (gps["longitude"] != 0)]
    if gps.empty:
        return df
    lat = round(float(gps["latitude"].mean()), 4)
    lon = round(float(gps["longitude"].mean()), 4)
    start = gps["time_utc"].min().date()
    end = gps["time_utc"].max().date()
    url = (
        f"https://archive-api.open-meteo.com/v1/era5"
        f"?latitude={lat}&longitude={lon}"
        f"&start_date={start}&end_date={end}"
        f"&hourly=temperature_2m,wind_speed_10m,relative_humidity_2m,pressure_msl"
        f"&timezone=UTC"
    )
    try:
        r = requests.get(url, timeout=10)
        r.raise_for_status()
        data = r.json()["hourly"]
        weather = pd.DataFrame({
            "time_utc": pd.to_datetime(data["time"]),
            "weather_temp_c": data["temperature_2m"],
            "weather_wind_kmh": data["wind_speed_10m"],
            "weather_humidity_pct": data["relative_humidity_2m"],
            "weather_pressure_hpa": data["pressure_msl"],
        })
        merged = pd.merge_asof(
            df.sort_values("time_utc"),
            weather.sort_values("time_utc"),
            on="time_utc", direction="nearest",
            tolerance=pd.Timedelta("1h"),
        )
        return merged
    except Exception as e:
        print(f"⚠ Météo non récupérée : {e}")
        return df


# =============================================================================
# HTML dashboard
# =============================================================================

CSS_THEME = """
:root {
    --bg: #0f1421; --surface: #1a2236; --surface-2: #232d44;
    --border: #2d3a55; --text: #e6edf7; --text-muted: #97a3b9;
    --brand: #4d8dde; --accent: #4bd07a; --error: #e94e3a;
    --warn: #ffd43b; --ok: #4bd07a;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text);
       font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Inter, Roboto, sans-serif;
       line-height: 1.55; }
header { background: linear-gradient(135deg, #142042, #1c2c54);
         border-bottom: 1px solid var(--border); padding: 24px 28px; }
header h1 { margin: 0 0 6px; font-size: 1.7rem; letter-spacing: -0.02em; }
header .subtitle { color: var(--text-muted); font-size: 0.95rem; }
.container { max-width: 1280px; margin: 0 auto; padding: 24px; }
.tabs { display: flex; gap: 2px; margin-bottom: 20px;
        border-bottom: 1px solid var(--border); flex-wrap: wrap; }
.tab { padding: 10px 18px; cursor: pointer; border: none;
       background: transparent; color: var(--text-muted);
       font: inherit; font-weight: 500; border-bottom: 2px solid transparent; }
.tab:hover { color: var(--text); }
.tab.active { color: var(--brand); border-bottom-color: var(--brand); }
.panel { display: none; }
.panel.active { display: block; }
.stats-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
              gap: 12px; margin-bottom: 24px; }
.stat-card { background: var(--surface); border: 1px solid var(--border);
             border-radius: 12px; padding: 14px 18px; }
.stat-card .label { font-size: 0.74rem; color: var(--text-muted);
                    text-transform: uppercase; letter-spacing: 0.05em; margin-bottom: 4px; }
.stat-card .value { font-size: 1.55rem; font-weight: 700; letter-spacing: -0.02em;
                    font-variant-numeric: tabular-nums; }
.stat-card .sub { font-size: 0.78rem; color: var(--text-muted); margin-top: 4px; }
.stat-card.warn .value { color: var(--warn); }
.stat-card.error .value { color: var(--error); }
.stat-card.ok .value { color: var(--ok); }
.section { background: var(--surface); border: 1px solid var(--border);
           border-radius: 12px; padding: 18px; margin-bottom: 18px; }
.section h2 { margin: 0 0 14px; font-size: 1.1rem; font-weight: 600; }
.map-frame { width: 100%; height: 620px; border: 0; border-radius: 8px; }
.empty { color: var(--text-muted); padding: 40px; text-align: center; font-style: italic; }
table { border-collapse: collapse; width: 100%; font-size: 0.92rem; }
th, td { padding: 8px 12px; text-align: right; border-bottom: 1px solid var(--border);
         font-variant-numeric: tabular-nums; }
th:first-child, td:first-child { text-align: left; }
th { color: var(--text-muted); font-weight: 600; font-size: 0.78rem;
     text-transform: uppercase; letter-spacing: 0.05em; }
.aqi-badge { display: inline-block; padding: 2px 10px; border-radius: 10px;
             color: #1a2236; font-weight: 700; font-size: 0.85rem; }
.dl-bar { display: flex; gap: 8px; flex-wrap: wrap; margin: 14px 0 0; }
.dl-link { background: var(--surface-2); color: var(--text);
           padding: 8px 14px; border-radius: 8px; text-decoration: none;
           border: 1px solid var(--border); font-size: 0.9rem;
           transition: background 0.15s; }
.dl-link:hover { background: var(--border); }
.session-card { display: block; background: var(--surface); border: 1px solid var(--border);
                border-radius: 12px; padding: 16px 18px; margin-bottom: 10px;
                color: inherit; text-decoration: none;
                transition: border-color 0.15s, transform 0.1s; }
.session-card:hover { border-color: var(--brand); transform: translateY(-1px); }
.session-card .row { display: flex; justify-content: space-between; align-items: baseline;
                     flex-wrap: wrap; gap: 8px; }
.session-card .title { font-weight: 600; font-size: 1.05rem; }
.session-card .meta { color: var(--text-muted); font-size: 0.88rem; }
.session-card .mini-stats { display: flex; gap: 14px; margin-top: 8px; flex-wrap: wrap; }
.session-card .mini-stats span { font-size: 0.85rem; color: var(--text-muted); }
.session-card .mini-stats b { color: var(--text); margin-left: 4px; }
"""

JS_TABS = """
document.querySelectorAll('.tab').forEach(t => {
    t.addEventListener('click', () => {
        const target = t.dataset.target;
        document.querySelectorAll('.tab').forEach(x => x.classList.toggle('active', x === t));
        document.querySelectorAll('.panel').forEach(p => p.classList.toggle('active', p.id === target));
        // resize plotly + folium iframes
        window.dispatchEvent(new Event('resize'));
    });
});
"""


def fmt_duration(sec: float) -> str:
    if not sec or sec <= 0:
        return "—"
    h = int(sec // 3600); m = int((sec % 3600) // 60); s = int(sec % 60)
    if h > 0:
        return f"{h:02d}:{m:02d}:{s:02d}"
    return f"{m:02d}:{s:02d}"


def fmt_distance(m: float) -> str:
    if m < 1000:
        return f"{m:.0f} m"
    return f"{m/1000:.2f} km"


def aqi_badge_html(aqi: float) -> str:
    a = max(1, min(5, int(round(aqi))))
    name, color = AQI_INFO[a]
    return f'<span class="aqi-badge" style="background:{color}">{a} · {name}</span>'


def render_session_html(name: str, df: pd.DataFrame, stats: dict, output_dir: Path) -> Path:
    """Generate a detailed HTML page for a session, plus its exports."""
    data_dir = output_dir / "data"
    data_dir.mkdir(parents=True, exist_ok=True)
    csv_path = data_dir / f"{name}.csv"
    gpx_path = data_dir / f"{name}.gpx"
    export_csv(df, csv_path)
    export_gpx(df, gpx_path)

    # Map
    peaks = {}
    for col in ("pm2", "co2", "tvoc"):
        peaks[col] = detect_peaks(df, col, top_n=3)
    m = build_map(df, peaks=peaks, heatmap=True)
    map_html = m.get_root().render() if m else ""

    map_iframe_path = data_dir / f"{name}_map.html"
    if m:
        m.save(str(map_iframe_path))

    timeseries_html = build_timeseries(df)
    correlations_html = build_correlations(df)
    donut_html = build_aqi_donut(stats)

    # Stat cards
    duration = fmt_duration(stats.get("duration_sec", 0))
    distance = fmt_distance(stats.get("distance_m", 0))
    aqi_mean = stats.get("aqi_mean")
    aqi_badge = aqi_badge_html(aqi_mean) if aqi_mean else ""

    pm2_mean = stats.get("pm2", {}).get("mean")
    pm2_max = stats.get("pm2", {}).get("max")
    pm2_dose = stats.get("pm2", {}).get("dose")
    co2_mean = stats.get("co2", {}).get("mean")
    co2_max = stats.get("co2", {}).get("max")
    speed_mean = stats.get("speed", {}).get("mean")
    speed_max = stats.get("speed", {}).get("max")
    who_pct = stats.get("who_pm25_pct_24h")

    cards = []
    cards.append(f'<div class="stat-card"><div class="label">Durée</div><div class="value">{duration}</div></div>')
    cards.append(f'<div class="stat-card"><div class="label">Distance</div><div class="value">{distance}</div></div>')
    cards.append(f'<div class="stat-card"><div class="label">Points</div><div class="value">{stats.get("points", 0)}</div></div>')
    if aqi_mean is not None:
        cards.append(f'<div class="stat-card"><div class="label">AQI moyen</div><div class="value">{aqi_mean:.1f}</div><div class="sub">{aqi_badge}</div></div>')
    if pm2_mean is not None:
        cls = "error" if pm2_mean > WHO_PM25_24H else ("warn" if pm2_mean > WHO_PM25_ANNUAL else "ok")
        cards.append(f'<div class="stat-card {cls}"><div class="label">PM2.5 moyen</div><div class="value">{pm2_mean:.1f}<span style="font-size:0.7em"> µg/m³</span></div><div class="sub">max {pm2_max:.1f}</div></div>')
    if pm2_dose is not None:
        cards.append(f'<div class="stat-card"><div class="label">Dose PM2.5</div><div class="value">{pm2_dose:.0f}<span style="font-size:0.7em"> µg·min/m³</span></div></div>')
    if co2_mean is not None:
        cls = "error" if co2_mean > CO2_BAD else ("warn" if co2_mean > CO2_GOOD else "ok")
        cards.append(f'<div class="stat-card {cls}"><div class="label">CO₂ moyen</div><div class="value">{co2_mean:.0f}<span style="font-size:0.7em"> ppm</span></div><div class="sub">max {co2_max:.0f}</div></div>')
    if speed_mean is not None:
        cards.append(f'<div class="stat-card"><div class="label">Vitesse moyenne</div><div class="value">{speed_mean:.1f}<span style="font-size:0.7em"> km/h</span></div><div class="sub">max {speed_max:.1f}</div></div>')
    if who_pct is not None:
        cls = "error" if who_pct > 50 else ("warn" if who_pct > 10 else "ok")
        cards.append(f'<div class="stat-card {cls}"><div class="label">% temps &gt; OMS 24h</div><div class="value">{who_pct:.0f}%</div><div class="sub">seuil {WHO_PM25_24H} µg/m³</div></div>')

    stats_grid = "".join(cards)

    # Detailed stats
    detail_rows = []
    for col, label in [("temperature", "Température (°C)"), ("humidity", "Humidité (%)"),
                       ("co2", "CO₂ (ppm)"), ("tvoc", "TVOC (ppb)"),
                       ("pm1", "PM1.0 (µg/m³)"), ("pm2", "PM2.5 (µg/m³)"),
                       ("speed", "Vitesse (km/h)"), ("aqi", "AQI")]:
        if col in stats:
            s = stats[col]
            detail_rows.append(
                f"<tr><td>{label}</td>"
                f"<td>{s['min']:.2f}</td>"
                f"<td>{s['mean']:.2f}</td>"
                f"<td>{s['median']:.2f}</td>"
                f"<td>{s['p95']:.2f}</td>"
                f"<td>{s['max']:.2f}</td></tr>"
            )
    detail_table = ""
    if detail_rows:
        detail_table = (
            "<table><thead><tr><th>Polluant</th><th>Min</th><th>Moy</th>"
            "<th>Médiane</th><th>P95</th><th>Max</th></tr></thead>"
            f"<tbody>{''.join(detail_rows)}</tbody></table>"
        )

    map_section = (
        f'<iframe class="map-frame" src="data/{name}_map.html"></iframe>'
        if m else '<div class="empty">Pas de coordonnées GPS valides</div>'
    )

    page = f"""<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Bik'air — {name}</title>
<style>{CSS_THEME}</style>
</head>
<body>
<header>
    <h1>🚴 {name}</h1>
    <div class="subtitle">{stats.get('start')} → {stats.get('end')}</div>
</header>
<div class="container">
    <div class="stats-grid">{stats_grid}</div>

    <div class="tabs">
        <button class="tab active" data-target="panel-map">🗺️ Carte</button>
        <button class="tab" data-target="panel-ts">📈 Séries temporelles</button>
        <button class="tab" data-target="panel-aqi">🍃 AQI</button>
        <button class="tab" data-target="panel-corr">🔗 Corrélations</button>
        <button class="tab" data-target="panel-stats">📊 Stats détaillées</button>
    </div>

    <div class="panel active" id="panel-map">
        <div class="section"><h2>Trace GPS (couleur = AQI)</h2>{map_section}</div>
    </div>
    <div class="panel" id="panel-ts">
        <div class="section"><h2>Séries temporelles (brut + lissé)</h2>{timeseries_html or '<div class="empty">Pas de données temporelles</div>'}</div>
    </div>
    <div class="panel" id="panel-aqi">
        <div class="section"><h2>Répartition AQI</h2>{donut_html or '<div class="empty">Pas de données AQI</div>'}</div>
    </div>
    <div class="panel" id="panel-corr">
        <div class="section"><h2>Corrélations</h2>{correlations_html or '<div class="empty">Pas assez de données</div>'}</div>
    </div>
    <div class="panel" id="panel-stats">
        <div class="section"><h2>Statistiques détaillées</h2>{detail_table or '<div class="empty">—</div>'}</div>
    </div>

    <div class="dl-bar">
        <a class="dl-link" href="data/{name}.csv">⬇ Export CSV</a>
        <a class="dl-link" href="data/{name}.gpx">🗺️ Export GPX</a>
        <a class="dl-link" href="index.html">← Retour</a>
    </div>
</div>
<script>{JS_TABS}</script>
</body>
</html>
"""
    out_file = output_dir / f"{name}.html"
    out_file.write_text(page, encoding="utf-8")
    return out_file


def render_index_html(sessions: list[tuple[str, pd.DataFrame, dict]],
                      output_dir: Path) -> Path:
    """Landing page — aggregate if several sessions, otherwise redirect to the only one."""
    if len(sessions) == 1:
        # Single session: index = the session page
        name = sessions[0][0]
        index_file = output_dir / "index.html"
        target = output_dir / f"{name}.html"
        if target.exists():
            index_file.write_text(target.read_text(encoding="utf-8"), encoding="utf-8")
        return index_file

    # Multiple: aggregate page
    agg = aggregate_stats([s for _, _, s in sessions])

    # Multi-track map
    agg_map = build_aggregate_map([(n, d) for n, d, _ in sessions])
    map_section = '<div class="empty">Pas de trace GPS</div>'
    if agg_map:
        agg_map_file = output_dir / "data" / "aggregate_map.html"
        agg_map_file.parent.mkdir(parents=True, exist_ok=True)
        agg_map.save(str(agg_map_file))
        map_section = f'<iframe class="map-frame" src="data/aggregate_map.html"></iframe>'

    compare_html = build_session_compare([(n, s) for n, _, s in sessions])

    # Session cards
    cards_html: list[str] = []
    for name, df, stats in sessions:
        duration = fmt_duration(stats.get("duration_sec", 0))
        distance = fmt_distance(stats.get("distance_m", 0))
        aqi_mean = stats.get("aqi_mean")
        pm2_mean = stats.get("pm2", {}).get("mean")
        start = stats.get("start")
        start_str = start.strftime("%Y-%m-%d %H:%M") if start else "—"

        chips = [f"<span>⏱ <b>{duration}</b></span>",
                 f"<span>📏 <b>{distance}</b></span>",
                 f"<span>📡 <b>{stats.get('points', 0)} pts</b></span>"]
        if aqi_mean is not None:
            chips.append(f"<span>🍃 AQI <b>{aqi_mean:.1f}</b></span>")
        if pm2_mean is not None:
            chips.append(f"<span>🌁 PM2.5 <b>{pm2_mean:.1f}</b></span>")

        cards_html.append(
            f'<a class="session-card" href="{name}.html">'
            f'  <div class="row"><div class="title">🚴 {name}</div>'
            f'  <div class="meta">{start_str}</div></div>'
            f'  <div class="mini-stats">{"".join(chips)}</div>'
            f"</a>"
        )

    # Global stat cards
    total_dist = fmt_distance(agg["total_distance_m"])
    total_dur = fmt_duration(agg["total_duration_sec"])
    cards = [
        f'<div class="stat-card"><div class="label">Sorties</div><div class="value">{agg["sessions"]}</div></div>',
        f'<div class="stat-card"><div class="label">Distance totale</div><div class="value">{total_dist}</div></div>',
        f'<div class="stat-card"><div class="label">Durée totale</div><div class="value">{total_dur}</div></div>',
        f'<div class="stat-card"><div class="label">Points totaux</div><div class="value">{agg["total_points"]}</div></div>',
    ]
    if "pm2" in agg:
        m = agg["pm2"]["mean_of_means"]
        cls = "error" if m > WHO_PM25_24H else ("warn" if m > WHO_PM25_ANNUAL else "ok")
        cards.append(f'<div class="stat-card {cls}"><div class="label">PM2.5 moyen global</div><div class="value">{m:.1f}<span style="font-size:0.7em"> µg/m³</span></div><div class="sub">max {agg["pm2"]["max_seen"]:.1f}</div></div>')
    if "aqi" in agg:
        m = agg["aqi"]["mean_of_means"]
        cards.append(f'<div class="stat-card"><div class="label">AQI moyen global</div><div class="value">{m:.1f}</div><div class="sub">{aqi_badge_html(m)}</div></div>')
    if "co2" in agg:
        m = agg["co2"]["mean_of_means"]
        cls = "error" if m > CO2_BAD else ("warn" if m > CO2_GOOD else "ok")
        cards.append(f'<div class="stat-card {cls}"><div class="label">CO₂ moyen global</div><div class="value">{m:.0f}<span style="font-size:0.7em"> ppm</span></div></div>')

    page = f"""<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Bik'air — Rapport agrégé</title>
<style>{CSS_THEME}</style>
</head>
<body>
<header>
    <h1>🚴 Bik'air — Rapport agrégé</h1>
    <div class="subtitle">{agg['sessions']} sorties · {total_dist} · {total_dur}</div>
</header>
<div class="container">
    <div class="stats-grid">{''.join(cards)}</div>

    <div class="tabs">
        <button class="tab active" data-target="panel-list">📂 Sorties</button>
        <button class="tab" data-target="panel-map">🗺️ Carte combinée</button>
        <button class="tab" data-target="panel-compare">📊 Comparaison</button>
    </div>

    <div class="panel active" id="panel-list">
        {''.join(cards_html)}
    </div>
    <div class="panel" id="panel-map">
        <div class="section"><h2>Toutes les traces</h2>{map_section}</div>
    </div>
    <div class="panel" id="panel-compare">
        <div class="section"><h2>Comparaison entre sorties</h2>{compare_html or '<div class="empty">—</div>'}</div>
    </div>
</div>
<script>{JS_TABS}</script>
</body>
</html>
"""
    out_file = output_dir / "index.html"
    out_file.write_text(page, encoding="utf-8")
    return out_file


# =============================================================================
# Processing pipeline
# =============================================================================

def process_session(name: str, df: pd.DataFrame, output_dir: Path,
                    smooth_window: int, zscore: float) -> tuple[pd.DataFrame, dict]:
    df = normalize(df)
    df = remove_outliers(df, zscore_threshold=zscore)
    df = smooth(df, window=smooth_window)
    stats = compute_stats(df)
    render_session_html(name, df, stats, output_dir)
    return df, stats


def main(argv=None):
    parser = argparse.ArgumentParser(description="Bik'air — traitement et visualisation des logs")
    parser.add_argument("path", type=str, help="Fichier .json/.TXT/.csv ou dossier de logs")
    parser.add_argument("-o", "--output", type=str, default="output",
                        help="Dossier de sortie (default: ./output)")
    parser.add_argument("--smooth", type=int, default=5,
                        help="Fenêtre de lissage (default: 5, 1=désactivé)")
    parser.add_argument("--outlier-zscore", type=float, default=3.5,
                        help="Seuil Z-score pour les outliers (default: 3.5)")
    parser.add_argument("--session-gap", type=int, default=5,
                        help="Découpage en sessions sur écart > N minutes (default: 5)")
    parser.add_argument("--no-split", action="store_true",
                        help="Ne pas découper en sessions, garder un seul DataFrame")
    parser.add_argument("--weather", action="store_true",
                        help="Enrichir avec données météo OpenMeteo (requiert internet)")
    parser.add_argument("--open", action="store_true",
                        help="Ouvrir le résultat dans le navigateur à la fin")
    args = parser.parse_args(argv)

    input_path = Path(args.path)
    if not input_path.exists():
        print(f"⛔ Chemin introuvable : {input_path}", file=sys.stderr)
        return 1

    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "data").mkdir(parents=True, exist_ok=True)

    files = load_path(input_path)
    if not files:
        print("⛔ Aucune donnée chargée.", file=sys.stderr)
        return 1
    print(f"📂 {len(files)} fichier(s) chargé(s)")

    # Weather (at file level, more efficient)
    if args.weather:
        print("☁ Récupération météo OpenMeteo...")
        files = [(n, fetch_weather(normalize(df))) for n, df in files]

    # Split into sessions
    all_sessions: list[tuple[str, pd.DataFrame]] = []
    for name, df in files:
        df = normalize(df)
        if args.no_split:
            all_sessions.append((name, df))
        else:
            subs = split_sessions(df, gap_minutes=args.session_gap)
            if len(subs) <= 1:
                all_sessions.append((name, df))
            else:
                for i, sub in enumerate(subs, 1):
                    all_sessions.append((f"{name}_session{i}", sub))

    print(f"📊 {len(all_sessions)} session(s) à traiter")

    processed: list[tuple[str, pd.DataFrame, dict]] = []
    for name, df in all_sessions:
        try:
            df2, stats = process_session(name, df, output_dir,
                                         smooth_window=args.smooth,
                                         zscore=args.outlier_zscore)
            processed.append((name, df2, stats))
            duration = fmt_duration(stats.get("duration_sec", 0))
            distance = fmt_distance(stats.get("distance_m", 0))
            print(f"  ✅ {name} · {duration} · {distance} · {stats.get('points', 0)} pts")
        except Exception as e:
            print(f"  ⛔ {name} : {e}")
            import traceback; traceback.print_exc()

    if not processed:
        print("⛔ Rien à afficher.", file=sys.stderr)
        return 1

    index_file = render_index_html(processed, output_dir)
    print(f"\n✨ Rapport généré : {index_file.resolve()}")

    if args.open:
        import webbrowser
        webbrowser.open(index_file.resolve().as_uri())

    return 0


if __name__ == "__main__":
    sys.exit(main())
