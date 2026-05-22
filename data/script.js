/* =========================================================
   Bik'air Quality Monitoring — Dashboard logic
   ========================================================= */
'use strict';

/* ---------- Configuration ---------- */

const WS_URL = `ws://${window.location.hostname}/ws`;
const HISTORY_MINUTES = 10;          // Window for sparklines (minutes)
const HISTORY_MAX_POINTS = 600;      // ~1pt/sec × 10min
const TRACE_MAX_POINTS = 2000;
const RECONNECT_DELAY_MS = 2000;

/* AQI descriptors (ENS160 scale 1..5) */
const AQI_INFO = {
    1: { name: 'Excellent', color: '#00b894' },
    2: { name: 'Bon',       color: '#74d36a' },
    3: { name: 'Moyen',     color: '#ffd43b' },
    4: { name: 'Médiocre',  color: '#ff8a3d' },
    5: { name: 'Mauvais',   color: '#e94e3a' },
};

/* Sensor definitions used to build the grid + stats + sparkline */
const SENSORS = [
    { id: 'temperature', icon: '🌡️', label: 'Température',  unit: '°C',    digits: 1, quality: tempQuality },
    { id: 'humidity',    icon: '💧', label: 'Humidité',      unit: '%',     digits: 0, quality: humQuality },
    { id: 'tvoc',        icon: '🌫️', label: 'TVOC',           unit: 'ppb',   digits: 0, quality: tvocQuality },
    { id: 'co2',         icon: '🫁', label: 'CO₂',            unit: 'ppm',   digits: 0, quality: co2Quality },
    { id: 'pm1',         icon: '🌁', label: 'PM 1.0',         unit: 'µg/m³', digits: 1, quality: pmQuality },
    { id: 'pm2',         icon: '🌁', label: 'PM 2.5',         unit: 'µg/m³', digits: 1, quality: pm25Quality },
];

/* ---------- Quality scoring helpers (return 0..1 + color) ---------- */

function makeQuality(thresholds) {
    return (v) => {
        if (v == null || isNaN(v)) return { q: 0, color: 'var(--ok)' };
        for (const [limit, color, score] of thresholds) {
            if (v <= limit) return { q: score, color };
        }
        const last = thresholds[thresholds.length - 1];
        return { q: 1, color: last[1] };
    };
}

const tempQuality = makeQuality([
    [15, '#3498db', 0.3], [25, '#2ecc71', 0.6], [30, '#f1c40f', 0.8], [40, '#e74c3c', 1],
]);
const humQuality = makeQuality([
    [30, '#f1c40f', 0.5], [60, '#2ecc71', 0.8], [80, '#f1c40f', 0.6], [100, '#e74c3c', 1],
]);
const tvocQuality = makeQuality([
    [220, '#2ecc71', 0.3], [660, '#f1c40f', 0.5], [2200, '#ff8a3d', 0.8], [10000, '#e74c3c', 1],
]);
const co2Quality = makeQuality([
    [600, '#2ecc71', 0.2], [1000, '#f1c40f', 0.5], [1500, '#ff8a3d', 0.8], [5000, '#e74c3c', 1],
]);
const pmQuality = makeQuality([
    [10, '#2ecc71', 0.3], [25, '#f1c40f', 0.5], [50, '#ff8a3d', 0.8], [150, '#e74c3c', 1],
]);
const pm25Quality = makeQuality([
    [12, '#2ecc71', 0.3], [35, '#f1c40f', 0.5], [55, '#ff8a3d', 0.8], [150, '#e74c3c', 1],
]);

/* ---------- State ---------- */

const State = {
    ws: null,
    wsConnected: false,
    history: Object.fromEntries(SENSORS.map((s) => [s.id, []])), // id -> [{t, v}]
    trace: [],          // [{t, lat, lon, aqi, speed}]
    session: {
        startedAt: null,
        points: 0,
        distance: 0,    // meters
        lastFix: null,  // {lat, lon}
        stats: Object.fromEntries(SENSORS.map((s) => [s.id, { min: Infinity, max: -Infinity, sum: 0, n: 0 }])),
    },
    recording: false,
    lastSpeed: 0,
    theme: localStorage.getItem('theme') || null,
};

/* ---------- Utilities ---------- */

const $ = (id) => document.getElementById(id);

function fmt(value, digits = 1) {
    const n = Number(value);
    if (!isFinite(n)) return '—';
    return n.toFixed(digits);
}

function fmtDuration(ms) {
    if (!ms || ms < 0) return '00:00';
    const s = Math.floor(ms / 1000);
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    const sec = s % 60;
    const pad = (x) => String(x).padStart(2, '0');
    return h > 0 ? `${pad(h)}:${pad(m)}:${pad(sec)}` : `${pad(m)}:${pad(sec)}`;
}

function fmtDistance(m) {
    if (m < 1000) return `${Math.round(m)} m`;
    return `${(m / 1000).toFixed(2)} km`;
}

function haversine(lat1, lon1, lat2, lon2) {
    const toRad = (x) => (x * Math.PI) / 180;
    const R = 6371000;
    const dLat = toRad(lat2 - lat1);
    const dLon = toRad(lon2 - lon1);
    const a = Math.sin(dLat / 2) ** 2 +
              Math.cos(toRad(lat1)) * Math.cos(toRad(lat2)) * Math.sin(dLon / 2) ** 2;
    return 2 * R * Math.asin(Math.sqrt(a));
}

function toast(message, type = 'info', duration = 3000) {
    const stack = $('toast-stack');
    if (!stack) return;
    const el = document.createElement('div');
    el.className = `toast ${type}`;
    el.textContent = message;
    stack.appendChild(el);
    setTimeout(() => {
        el.classList.add('hide');
        setTimeout(() => el.remove(), 220);
    }, duration);
}

/* ---------- Theme ---------- */

function applyTheme(theme) {
    if (theme) document.documentElement.setAttribute('data-theme', theme);
    else document.documentElement.removeAttribute('data-theme');
    State.theme = theme;
}

function toggleTheme() {
    const current = State.theme || (matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
    const next = current === 'dark' ? 'light' : 'dark';
    applyTheme(next);
    localStorage.setItem('theme', next);
}

/* ---------- Grid rendering ---------- */

function buildGrid() {
    const grid = $('grid-readings');
    grid.innerHTML = '';
    for (const s of SENSORS) {
        const card = document.createElement('div');
        card.className = 'reading';
        card.id = `card-${s.id}`;
        card.innerHTML = `
            <div class="reading-head">
                <span class="reading-label">
                    <span class="reading-icon">${s.icon}</span> ${s.label}
                </span>
            </div>
            <div>
                <span id="${s.id}" class="reading-value">—</span>
                <span class="reading-unit">${s.unit}</span>
            </div>
            <svg class="reading-sparkline" id="spark-${s.id}" viewBox="0 0 200 40" preserveAspectRatio="none"></svg>
            <div class="reading-meta">
                <span id="meta-${s.id}-min">min —</span>
                <span id="meta-${s.id}-max">max —</span>
            </div>
            <div class="reading-quality-bar"></div>
        `;
        grid.appendChild(card);
    }
    buildStatsRows();
}

function buildStatsRows() {
    const tbody = $('stats-tbody');
    tbody.innerHTML = '';
    for (const s of SENSORS) {
        const tr = document.createElement('tr');
        tr.innerHTML = `
            <td>${s.icon} ${s.label}</td>
            <td id="stat-${s.id}-min">—</td>
            <td id="stat-${s.id}-avg">—</td>
            <td id="stat-${s.id}-max">—</td>
            <td id="stat-${s.id}-cur">—</td>
        `;
        tbody.appendChild(tr);
    }
}

/* ---------- Sparklines ---------- */

function drawSparkline(id, points) {
    const svg = $(`spark-${id}`);
    if (!svg) return;
    if (points.length < 2) {
        svg.innerHTML = '';
        return;
    }
    const vals = points.map((p) => p.v).filter((v) => isFinite(v));
    if (vals.length < 2) { svg.innerHTML = ''; return; }

    let min = Math.min(...vals);
    let max = Math.max(...vals);
    if (min === max) { min -= 1; max += 1; }
    const range = max - min;

    const W = 200, H = 40, pad = 2;
    const tMin = points[0].t;
    const tMax = points[points.length - 1].t;
    const tRange = tMax - tMin || 1;

    let d = '';
    points.forEach((p, i) => {
        const x = pad + ((p.t - tMin) / tRange) * (W - 2 * pad);
        const y = H - pad - ((p.v - min) / range) * (H - 2 * pad);
        d += (i === 0 ? 'M' : 'L') + x.toFixed(1) + ',' + y.toFixed(1) + ' ';
    });

    const last = points[points.length - 1];
    const lastX = pad + ((last.t - tMin) / tRange) * (W - 2 * pad);
    const lastY = H - pad - ((last.v - min) / range) * (H - 2 * pad);

    const sensor = SENSORS.find((s) => s.id === id);
    const { color } = sensor.quality(last.v);

    svg.innerHTML = `
        <defs>
            <linearGradient id="grad-${id}" x1="0" y1="0" x2="0" y2="1">
                <stop offset="0%" stop-color="${color}" stop-opacity="0.32"/>
                <stop offset="100%" stop-color="${color}" stop-opacity="0"/>
            </linearGradient>
        </defs>
        <path d="${d} L${lastX.toFixed(1)},${H - pad} L${pad},${H - pad} Z" fill="url(#grad-${id})"/>
        <path d="${d}" fill="none" stroke="${color}" stroke-width="1.6" stroke-linejoin="round" stroke-linecap="round"/>
        <circle cx="${lastX.toFixed(1)}" cy="${lastY.toFixed(1)}" r="2.2" fill="${color}"/>
    `;
}

/* ---------- Trace SVG ---------- */

function drawTrace() {
    const svg = $('trace-svg');
    const empty = $('trace-empty');
    const scaleEl = $('trace-scale');
    if (!svg) return;

    const pts = State.trace;
    if (pts.length < 2) {
        svg.innerHTML = '';
        empty.style.display = 'flex';
        scaleEl.hidden = true;
        return;
    }
    empty.style.display = 'none';
    scaleEl.hidden = false;

    const lats = pts.map((p) => p.lat);
    const lons = pts.map((p) => p.lon);
    let minLat = Math.min(...lats), maxLat = Math.max(...lats);
    let minLon = Math.min(...lons), maxLon = Math.max(...lons);

    // Keep min span so a near-stationary trace isn't a point
    const minSpan = 0.0005;
    if (maxLat - minLat < minSpan) { const c = (maxLat + minLat) / 2; minLat = c - minSpan / 2; maxLat = c + minSpan / 2; }
    if (maxLon - minLon < minSpan) { const c = (maxLon + minLon) / 2; minLon = c - minSpan / 2; maxLon = c + minSpan / 2; }

    // Aspect correction
    const meanLat = (minLat + maxLat) / 2;
    const latM = 111320;
    const lonM = 111320 * Math.cos((meanLat * Math.PI) / 180);
    const widthM = (maxLon - minLon) * lonM;
    const heightM = (maxLat - minLat) * latM;

    const VB_W = 400, VB_H = 225;
    const pad = 12;
    const aspectVB = (VB_W - 2 * pad) / (VB_H - 2 * pad);
    const aspectData = widthM / heightM;

    let usableW = VB_W - 2 * pad, usableH = VB_H - 2 * pad;
    if (aspectData > aspectVB) {
        usableH = usableW / aspectData;
    } else {
        usableW = usableH * aspectData;
    }
    const offX = (VB_W - usableW) / 2;
    const offY = (VB_H - usableH) / 2;

    const toXY = (p) => {
        const x = offX + ((p.lon - minLon) / (maxLon - minLon)) * usableW;
        const y = offY + (1 - (p.lat - minLat) / (maxLat - minLat)) * usableH;
        return [x, y];
    };

    // Build segments (each colored by the destination point's AQI)
    let segments = '';
    for (let i = 1; i < pts.length; i++) {
        const [x1, y1] = toXY(pts[i - 1]);
        const [x2, y2] = toXY(pts[i]);
        const aqi = Math.max(1, Math.min(5, Math.round(pts[i].aqi || 1)));
        const color = AQI_INFO[aqi].color;
        segments += `<line x1="${x1.toFixed(1)}" y1="${y1.toFixed(1)}" x2="${x2.toFixed(1)}" y2="${y2.toFixed(1)}" stroke="${color}" stroke-width="3" stroke-linecap="round"/>`;
    }

    // Start + end markers
    const [sx, sy] = toXY(pts[0]);
    const [ex, ey] = toXY(pts[pts.length - 1]);

    // Subtle grid
    const grid = `
        <rect width="${VB_W}" height="${VB_H}" fill="var(--surface-2)" />
        <g stroke="var(--border)" stroke-width="0.5">
            ${[1,2,3].map(i => `<line x1="0" y1="${(VB_H/4)*i}" x2="${VB_W}" y2="${(VB_H/4)*i}"/>`).join('')}
            ${[1,2,3,4,5,6,7].map(i => `<line x1="${(VB_W/8)*i}" y1="0" x2="${(VB_W/8)*i}" y2="${VB_H}"/>`).join('')}
        </g>
    `;

    svg.innerHTML = `
        ${grid}
        ${segments}
        <circle cx="${sx.toFixed(1)}" cy="${sy.toFixed(1)}" r="5" fill="#fff" stroke="#1e6cb0" stroke-width="2"/>
        <circle cx="${ex.toFixed(1)}" cy="${ey.toFixed(1)}" r="5" fill="#e94e3a" stroke="#fff" stroke-width="2"/>
    `;

    // Scale bar (approx)
    const scaleMetersTarget = Math.max(widthM, heightM) / 4;
    const niceScales = [10, 25, 50, 100, 250, 500, 1000, 2000, 5000];
    const scaleM = niceScales.reduce((p, c) => (Math.abs(c - scaleMetersTarget) < Math.abs(p - scaleMetersTarget) ? c : p));
    scaleEl.textContent = scaleM >= 1000 ? `${(scaleM/1000).toFixed(1)} km` : `${scaleM} m`;
}

/* ---------- Data ingestion ---------- */

function ingest(data) {
    const now = Date.now();

    // Sensor numeric values into history
    for (const s of SENSORS) {
        const raw = data[s.id];
        if (raw == null) continue;
        const v = Number(raw);
        if (!isFinite(v)) continue;

        const hist = State.history[s.id];
        hist.push({ t: now, v });
        const tCut = now - HISTORY_MINUTES * 60 * 1000;
        while (hist.length && hist[0].t < tCut) hist.shift();
        while (hist.length > HISTORY_MAX_POINTS) hist.shift();

        // Update card
        const el = $(s.id);
        if (el) el.textContent = fmt(v, s.digits);
        const minEl = $(`meta-${s.id}-min`);
        const maxEl = $(`meta-${s.id}-max`);
        const stat = State.session.stats[s.id];
        if (stat) {
            stat.min = Math.min(stat.min, v);
            stat.max = Math.max(stat.max, v);
            stat.sum += v;
            stat.n++;
            if (minEl) minEl.textContent = `min ${fmt(stat.min, s.digits)}`;
            if (maxEl) maxEl.textContent = `max ${fmt(stat.max, s.digits)}`;
            updateStatsRow(s);
        }

        // Quality bar
        const card = $(`card-${s.id}`);
        const { q, color } = s.quality(v);
        if (card) {
            card.style.setProperty('--q', `${Math.round(q * 100)}%`);
            card.style.setProperty('--q-color', color);
        }

        drawSparkline(s.id, hist);
    }

    // AQI
    if (data.aqi != null) {
        const aqi = Number(data.aqi);
        const info = AQI_INFO[aqi] || { name: 'Indisponible', color: '#888' };
        $('aqi').textContent = isFinite(aqi) ? aqi : '—';
        $('aqi-name').textContent = info.name;
        $('aqi-card').dataset.aqi = isFinite(aqi) ? aqi : 0;
        $('aqi-bar-fill').style.width = `${(aqi / 5) * 100}%`;
    }

    // GPS / status
    if (data.gpsfix !== undefined) updateGpsPill(data.gpsfix === '1' || data.gpsfix === 1);
    if (data.satellites != null) {
        $('satellites').textContent = data.satellites;
        $('satellites-val').textContent = data.satellites;
    }
    if (data.latitude != null) $('latitude').textContent = data.latitude;
    if (data.longitude != null) $('longitude').textContent = data.longitude;
    if (data.altitude != null) $('altitude').textContent = data.altitude;

    if (data.time_utc) {
        const date = new Date(data.time_utc);
        if (!isNaN(date)) {
            $('time_utc').textContent = date.toLocaleTimeString();
        } else {
            $('time_utc').textContent = data.time_utc;
        }
    }

    // Speed
    if (data.speed != null) {
        const sp = Number(data.speed);
        if (isFinite(sp)) {
            State.lastSpeed = sp;
            $('sess-speed').textContent = `${sp.toFixed(1)} km/h`;
        }
    }

    // Update trace if we have a usable GPS fix
    const lat = parseFloat(data.latitude);
    const lon = parseFloat(data.longitude);
    const fix = (data.gpsfix === '1' || data.gpsfix === 1);
    if (fix && isFinite(lat) && isFinite(lon) && lat !== 0 && lon !== 0) {
        const aqi = Number(data.aqi) || 1;
        const last = State.trace[State.trace.length - 1];

        if (!last || haversine(last.lat, last.lon, lat, lon) > 2) {
            State.trace.push({ t: now, lat, lon, aqi, speed: State.lastSpeed });
            if (State.trace.length > TRACE_MAX_POINTS) State.trace.shift();

            // Session distance
            if (State.session.lastFix) {
                State.session.distance += haversine(State.session.lastFix.lat, State.session.lastFix.lon, lat, lon);
                $('sess-distance').textContent = fmtDistance(State.session.distance);
            }
            State.session.lastFix = { lat, lon };
            drawTrace();
        }
    }

    // Session counter
    if (!State.session.startedAt) State.session.startedAt = now;
    State.session.points++;
    $('sess-points').textContent = State.session.points;

    // Interval selector
    if (data.measureInterval !== undefined) {
        const sel = $('measure-interval');
        if (sel && !sel.matches(':focus')) {
            const v = String(data.measureInterval);
            if (sel.value !== v) sel.value = v;
        }
    }
}

function updateStatsRow(s) {
    const stat = State.session.stats[s.id];
    if (!stat || stat.n === 0) return;
    $(`stat-${s.id}-min`).textContent = fmt(stat.min, s.digits);
    $(`stat-${s.id}-max`).textContent = fmt(stat.max, s.digits);
    $(`stat-${s.id}-avg`).textContent = fmt(stat.sum / stat.n, s.digits);
    const last = State.history[s.id][State.history[s.id].length - 1];
    if (last) $(`stat-${s.id}-cur`).textContent = fmt(last.v, s.digits);
}

function updateGpsPill(online) {
    const pill = $('gps-pill');
    if (!pill) return;
    pill.classList.toggle('online', online);
    pill.classList.toggle('offline', !online);
    pill.title = online ? 'GPS connecté' : 'GPS non connecté';
}

function updateWsPill(online) {
    const pill = $('ws-pill');
    if (!pill) return;
    pill.classList.toggle('online', online);
    pill.classList.toggle('offline', !online);
    $('ws-state').textContent = online ? 'Connecté' : 'Reconnexion…';
    $('connection-lost').classList.toggle('show', !online);
}

/* ---------- Recording badge ---------- */

function setRecording(active) {
    State.recording = active;
    const badge = $('rec-badge');
    const text = $('rec-text');
    const btn = $('startstopmeas-btn');
    badge.classList.toggle('recording', active);
    text.textContent = active ? 'Enregistrement en cours' : 'Pas d\'enregistrement';

    if (active) {
        btn.textContent = '🛑 Arrêter l\'enregistrement';
        btn.classList.remove('success');
        btn.classList.add('danger');
        btn.dataset.state = 'false';
    } else {
        btn.textContent = '▶️ Démarrer l\'enregistrement';
        btn.classList.remove('danger');
        btn.classList.add('success');
        btn.dataset.state = 'true';
    }
}

/* ---------- Session timer ---------- */

setInterval(() => {
    if (State.session.startedAt) {
        $('sess-duration').textContent = fmtDuration(Date.now() - State.session.startedAt);
    }
}, 1000);

/* ---------- WebSocket ---------- */

function connectWS() {
    if (State.ws && State.ws.readyState === WebSocket.OPEN) return;
    try {
        State.ws = new WebSocket(WS_URL);
    } catch (e) {
        setTimeout(connectWS, RECONNECT_DELAY_MS);
        return;
    }

    State.ws.onopen = () => {
        State.wsConnected = true;
        updateWsPill(true);
        State.ws.send('getReadings');
    };

    State.ws.onclose = () => {
        State.wsConnected = false;
        updateWsPill(false);
        setTimeout(connectWS, RECONNECT_DELAY_MS);
    };

    State.ws.onerror = () => { /* onclose will fire */ };

    State.ws.onmessage = (event) => {
        let data;
        try { data = JSON.parse(event.data); } catch { return; }
        if (data.error || data.status) return;
        ingest(data);
    };
}

/* ---------- HTTP control ---------- */

async function sendTime() {
    const now = new Date();
    try {
        await fetch('/set-time', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                year: now.getFullYear(), month: now.getMonth() + 1, day: now.getDate(),
                hour: now.getHours(), minute: now.getMinutes(), second: now.getSeconds(),
                timezoneOffset: now.getTimezoneOffset(),
            }),
        });
    } catch (e) {
        toast('Synchronisation horaire échouée', 'error');
    }
}

async function fetchStatus() {
    try {
        const r = await fetch('/status');
        if (!r.ok) return;
        const s = await r.json();
        setRecording(!!s.measuring);
        if (s.measureInterval !== undefined) {
            const sel = $('measure-interval');
            if (sel && !sel.matches(':focus')) sel.value = String(s.measureInterval);
        }
    } catch (e) { /* ignored */ }
}

/* Prime sparklines & trace from the current log file (server-side persistence). */
async function fetchHistory() {
    try {
        const r = await fetch('/history');
        if (!r.ok) return;
        const points = await r.json();
        if (!Array.isArray(points) || points.length === 0) return;

        // Use a single fake "now" anchor: keep relative spacing if time_utc present,
        // otherwise space them by 1 second.
        const tNow = Date.now();
        let parsedTimes = points.map((p) => {
            const d = p.time_utc ? new Date(p.time_utc) : null;
            return d && !isNaN(d) ? d.getTime() : null;
        });
        if (parsedTimes.every((t) => t == null)) {
            parsedTimes = points.map((_, i) => tNow - (points.length - 1 - i) * 1000);
        }

        // Seed history & stats
        for (const s of SENSORS) {
            const hist = State.history[s.id];
            for (let i = 0; i < points.length; i++) {
                const v = Number(points[i][s.id]);
                if (!isFinite(v)) continue;
                hist.push({ t: parsedTimes[i] || tNow, v });
                const stat = State.session.stats[s.id];
                stat.min = Math.min(stat.min, v);
                stat.max = Math.max(stat.max, v);
                stat.sum += v;
                stat.n++;
            }
            while (hist.length > HISTORY_MAX_POINTS) hist.shift();
            drawSparkline(s.id, hist);
            updateStatsRow(s);
        }

        // Seed trace
        for (let i = 0; i < points.length; i++) {
            const p = points[i];
            const lat = parseFloat(p.latitude), lon = parseFloat(p.longitude);
            if (!isFinite(lat) || !isFinite(lon) || lat === 0 || lon === 0) continue;
            const last = State.trace[State.trace.length - 1];
            if (last && haversine(last.lat, last.lon, lat, lon) < 2) continue;
            if (last) State.session.distance += haversine(last.lat, last.lon, lat, lon);
            State.trace.push({ t: parsedTimes[i] || tNow, lat, lon,
                              aqi: Number(p.aqi) || 1, speed: Number(p.speed) || 0 });
        }
        if (State.trace.length > 1) drawTrace();
        if (State.session.distance) $('sess-distance').textContent = fmtDistance(State.session.distance);

        State.session.points = points.length;
        $('sess-points').textContent = State.session.points;
        if (parsedTimes[0]) State.session.startedAt = parsedTimes[0];
    } catch (e) { /* no history yet — fine */ }
}

async function toggleMeasure() {
    try {
        const r = await fetch('/startstopmeas');
        if (!r.ok) throw 0;
        setRecording(!State.recording);
        toast(State.recording ? 'Enregistrement démarré' : 'Enregistrement arrêté', 'success');
    } catch (e) {
        toast('Erreur lors du basculement', 'error');
    }
}

async function setInterval2(value) {
    try {
        const r = await fetch('/set-interval', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ interval: parseInt(value, 10) }),
        });
        if (!r.ok) throw 0;
        const j = await r.json();
        toast(`Intervalle réglé : ${j.currentInterval === 0 ? 'Auto' : j.currentInterval + 's'}`, 'success');
    } catch (e) {
        toast('Erreur lors du changement d\'intervalle', 'error');
    }
}

function resetSession() {
    if (!confirm('Réinitialiser les statistiques de session ?')) return;
    State.session.startedAt = Date.now();
    State.session.points = 0;
    State.session.distance = 0;
    State.session.lastFix = null;
    State.trace = [];
    for (const s of SENSORS) {
        State.history[s.id] = [];
        State.session.stats[s.id] = { min: Infinity, max: -Infinity, sum: 0, n: 0 };
        const minEl = $(`meta-${s.id}-min`); if (minEl) minEl.textContent = 'min —';
        const maxEl = $(`meta-${s.id}-max`); if (maxEl) maxEl.textContent = 'max —';
        ['min', 'avg', 'max', 'cur'].forEach(k => { const e = $(`stat-${s.id}-${k}`); if (e) e.textContent = '—'; });
        const spark = $(`spark-${s.id}`); if (spark) spark.innerHTML = '';
    }
    drawTrace();
    $('sess-distance').textContent = '0 m';
    $('sess-points').textContent = '0';
    toast('Session réinitialisée', 'success');
}

async function goToSleep() {
    if (!confirm('Mettre l\'appareil en veille ? La connexion va se fermer.')) return;
    try {
        await fetch('/sleep');
        toast('Mise en veille…', 'info');
    } catch (e) {
        toast('Erreur lors de la mise en veille', 'error');
    }
}

/* ---------- Init ---------- */

document.addEventListener('DOMContentLoaded', async () => {
    if (State.theme) applyTheme(State.theme);

    buildGrid();
    drawTrace();

    $('theme-toggle').addEventListener('click', toggleTheme);
    $('startstopmeas-btn').addEventListener('click', toggleMeasure);
    $('reset-session-btn').addEventListener('click', resetSession);
    $('sleep-btn').addEventListener('click', goToSleep);
    $('measure-interval').addEventListener('change', (e) => setInterval2(e.target.value));

    await sendTime();
    await fetchStatus();
    await fetchHistory();
    connectWS();

    // PWA registration (silently fail if no SW)
    if ('serviceWorker' in navigator) {
        navigator.serviceWorker.register('/sw.js').catch(() => {});
    }
});
