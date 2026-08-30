/* ============================================================
   app.js  —  Smart Plant Irrigation Dashboard
   Vanilla JS, no external dependencies
   Polls /api/status every 5 s, handles all user interactions
   ============================================================ */

'use strict';

// ── Configuration ─────────────────────────────────────────────
const POLL_INTERVAL_MS = 5000;
const HISTORY_ENTRIES  = 50;

// ── State ─────────────────────────────────────────────────────
let g_lastStatus    = null;
let g_sparklineData = [];    // [{ ts, moisture }]  — up to 60 points
let g_pollTimer     = null;

// ─────────────────────────────────────────────────────────────
//  Boot
// ─────────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {
    wireProfileChips();
    fetchStatus();
    fetchHistory();
    g_pollTimer = setInterval(fetchStatus, POLL_INTERVAL_MS);
    // Refresh history every 30 s
    setInterval(fetchHistory, 30000);
});

// ─────────────────────────────────────────────────────────────
//  API helpers
// ─────────────────────────────────────────────────────────────
async function apiGet(url) {
    const r = await fetch(url);
    if (!r.ok) throw new Error(`${r.status} ${r.statusText}`);
    return r.json();
}

async function apiPost(url, body = null) {
    const opts = {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: body ? JSON.stringify(body) : undefined
    };
    const r = await fetch(url, opts);
    if (!r.ok) throw new Error(`${r.status} ${r.statusText}`);
    return r.json();
}

// ─────────────────────────────────────────────────────────────
//  Main status fetch & render
// ─────────────────────────────────────────────────────────────
async function fetchStatus() {
    try {
        const s = await apiGet('/api/status');
        g_lastStatus = s;
        renderStatus(s);
        hideAlert();
    } catch (e) {
        showAlert('Cannot reach ESP32 — ' + e.message);
        updateWifiPill(false, '—');
    }
}

function renderStatus(s) {
    // ── Time ──────────────────────────────────────────────────
    if (s.time?.hms) {
        setText('time-label', s.time.hms);
    }

    // ── Wi-Fi pill ────────────────────────────────────────────
    const connected = s.wifi?.connected ?? false;
    updateWifiPill(connected, s.wifi?.mode ?? '—');

    // ── Moisture gauge ────────────────────────────────────────
    const moisture = s.soil?.pct ?? 0;
    renderGauge(moisture, s.ctrl?.plant_status ?? 'Unknown', s.profile);

    // ── Plant status badge ────────────────────────────────────
    const emoji  = s.ctrl?.emoji ?? '';
    const status = s.ctrl?.plant_status ?? 'Unknown';
    setBadge('plant-status-badge', `${emoji} ${status}`, statusToClass(status));

    // ── Prediction ────────────────────────────────────────────
    const rateRaw = s.prediction?.rate_pct_hr;
    setText('drying-rate', rateRaw != null ? rateRaw.toFixed(2) : '—');

    const mins = s.ctrl?.mins_until_water;
    if (mins != null) {
        const h = Math.floor(mins / 60);
        const m = Math.round(mins % 60);
        setText('time-until-water', h > 0 ? `${h}h ${m}m` : `${m}m`);
    } else {
        setText('time-until-water', '—');
    }
    setText('prediction-status', s.prediction?.status ?? '—');

    // ── Sparkline ─────────────────────────────────────────────
    if (s.soil?.pct != null) {
        g_sparklineData.push({ ts: Date.now(), moisture: s.soil.pct });
        if (g_sparklineData.length > 60) g_sparklineData.shift();
        drawSparkline('sparkline-moisture', g_sparklineData);
    }

    // ── Tank ──────────────────────────────────────────────────
    const tankPct = s.tank?.pct ?? 0;
    setText('tank-pct', Math.round(tankPct) + '%');
    setText('tank-state', s.tank?.state ?? '—');
    setStyle('tank-fill', 'height', Math.round(tankPct) + '%');

    // ── Pump ──────────────────────────────────────────────────
    const pumpOn = s.ctrl?.pump_on ?? false;
    const pumpRing  = document.getElementById('pump-ring');
    const pumpState = document.getElementById('pump-state');
    pumpRing.classList.toggle('active', pumpOn);
    pumpState.textContent = pumpOn ? 'ON' : 'OFF';
    pumpState.classList.toggle('on', pumpOn);

    setText('daily-water', Math.round(s.ctrl?.daily_ml ?? 0));
    setText('pulse-num', s.ctrl?.pulse_num > 0 ? `#${s.ctrl.pulse_num}` : '—');

    // Last watered
    const lastMs = s.ctrl?.last_water_ms ?? 0;
    setText('last-water', lastMs > 0 ? msToRelative(lastMs, s.uptime_ms) : 'Never');

    // ── Profile ───────────────────────────────────────────────
    if (s.profile) {
        highlightProfile(s.profile.id);
        setText('th-critical', `${s.profile.critical}%`);
        setText('th-min',      `${s.profile.min_moisture}%`);
        setText('th-target',   `${s.profile.target}%`);
        setText('th-max',      `${s.profile.max_moisture}%`);
    }

    // ── System info ───────────────────────────────────────────
    setText('sys-state',      s.ctrl?.state     ?? '—');
    setText('sys-wifi',       s.wifi?.mode      ?? '—');
    setText('sys-ip',         s.wifi?.ip        ?? '—');
    setText('sys-rssi',       s.wifi?.rssi != null ? `${s.wifi.rssi} dBm` : '—');
    setText('sys-uptime',     s.uptime_ms != null ? msToHMS(s.uptime_ms) : '—');
    setText('sys-fw',         s.fw_version      ?? '—');
    setText('sys-raw',        s.soil?.raw       ?? '—');
    setText('sys-model-pts',  s.prediction?.data_points ?? '—');
    setText('footer-fw',      s.fw_version      ?? '—');
    setText('footer-sensor-state', `Sensor: ${s.soil?.state ?? '—'}`);

    // ── Toggles ───────────────────────────────────────────────
    setCheck('chk-auto',     s.ctrl?.auto_enabled ?? true);
    setCheck('chk-adaptive', s.ctrl?.adaptive     ?? true);

    // ── Error banner ──────────────────────────────────────────
    const err = s.ctrl?.last_error ?? '';
    if (err && err.length > 0) {
        showAlert(err);
    }
}

// ─────────────────────────────────────────────────────────────
//  Gauge rendering
// ─────────────────────────────────────────────────────────────
function renderGauge(pct, statusStr, profile) {
    // SVG arc: dashoffset = 283 * (1 - pct/100)
    const ARC_LEN = 283;
    const offset  = ARC_LEN * (1 - pct / 100);
    const arc = document.getElementById('gauge-arc');
    if (arc) {
        arc.style.strokeDashoffset = offset.toFixed(1);
        // Colour by status
        arc.style.stroke = statusToColour(statusStr);
        arc.style.filter = `drop-shadow(0 0 6px ${statusToColour(statusStr)})`;
    }

    setText('gauge-value', Math.round(pct) + '%');
    setText('gauge-sub',   statusStr.toUpperCase());

    // ── Threshold bar ─────────────────────────────────────────
    if (profile) {
        setStyle('threshold-fill', 'width', Math.round(pct) + '%');
        setPosMarker('marker-min',  profile.min_moisture);
        setPosMarker('marker-tgt',  profile.target);
        setPosMarker('marker-max',  profile.max_moisture);
    }
}

function setPosMarker(id, pct) {
    const el = document.getElementById(id);
    if (el) el.style.left = Math.round(pct) + '%';
}

function statusToColour(s) {
    switch (s.toLowerCase().replace(/\s/g,'_')) {
        case 'healthy':    return '#22c55e';
        case 'drying':     return '#eab308';
        case 'water_soon': return '#f97316';
        case 'needs_water':return '#ef4444';
        case 'critical':   return '#dc2626';
        default:           return '#94a3b8';
    }
}

function statusToClass(s) {
    switch (s.toLowerCase().replace(/\s/g,'_')) {
        case 'healthy':    return '';
        case 'drying':     return 'yellow';
        case 'water_soon': return 'orange';
        case 'needs_water':return 'red';
        case 'critical':   return 'red';
        default:           return 'gray';
    }
}

// ─────────────────────────────────────────────────────────────
//  Sparkline canvas chart
// ─────────────────────────────────────────────────────────────
function drawSparkline(canvasId, data) {
    const canvas = document.getElementById(canvasId);
    if (!canvas || data.length < 2) return;
    const ctx   = canvas.getContext('2d');
    const W     = canvas.width;
    const H     = canvas.height;
    const PAD   = 6;

    ctx.clearRect(0, 0, W, H);

    const vals = data.map(d => d.moisture);
    const minV = Math.min(...vals, 0);
    const maxV = Math.max(...vals, 100);
    const range = maxV - minV || 1;

    const xStep = (W - PAD * 2) / (data.length - 1);

    // Gradient fill
    const grad = ctx.createLinearGradient(0, 0, 0, H);
    grad.addColorStop(0, 'rgba(34,197,94,0.35)');
    grad.addColorStop(1, 'rgba(34,197,94,0)');

    ctx.beginPath();
    data.forEach((d, i) => {
        const x = PAD + i * xStep;
        const y = H - PAD - ((d.moisture - minV) / range) * (H - PAD * 2);
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    });
    // Close fill
    ctx.lineTo(PAD + (data.length - 1) * xStep, H);
    ctx.lineTo(PAD, H);
    ctx.closePath();
    ctx.fillStyle = grad;
    ctx.fill();

    // Line
    ctx.beginPath();
    data.forEach((d, i) => {
        const x = PAD + i * xStep;
        const y = H - PAD - ((d.moisture - minV) / range) * (H - PAD * 2);
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    });
    ctx.strokeStyle = '#22c55e';
    ctx.lineWidth   = 2;
    ctx.lineJoin    = 'round';
    ctx.lineCap     = 'round';
    ctx.stroke();

    // Latest dot
    const last = data[data.length - 1];
    const lx = PAD + (data.length - 1) * xStep;
    const ly = H - PAD - ((last.moisture - minV) / range) * (H - PAD * 2);
    ctx.beginPath();
    ctx.arc(lx, ly, 3.5, 0, Math.PI * 2);
    ctx.fillStyle = '#22c55e';
    ctx.fill();
}

// ─────────────────────────────────────────────────────────────
//  History table
// ─────────────────────────────────────────────────────────────
const EVENT_LABELS = [
    'Sensor Read', 'Watering Start', 'Watering Pulse', 'Watering Done',
    'Watering Failed', 'Fault Detected', 'Fault Cleared', 'Profile Changed',
    'Config Changed', 'System Boot', 'Manual Water', 'Calibration'
];

async function fetchHistory() {
    try {
        const data = await apiGet(`/api/history?n=${HISTORY_ENTRIES}`);
        renderHistory(data);
    } catch (e) {
        console.warn('History fetch failed:', e);
    }
}

function renderHistory(entries) {
    const tbody = document.getElementById('history-tbody');
    if (!tbody) return;

    if (!entries || entries.length === 0) {
        tbody.innerHTML = '<tr><td colspan="6" class="dim center">No history yet</td></tr>';
        return;
    }

    tbody.innerHTML = entries.map(e => {
        const label = EVENT_LABELS[e.type] ?? `Event ${e.type}`;
        const labelClass = e.type === 3 ? 'color:var(--accent-green)' :
                           e.type === 4 ? 'color:var(--accent-red)'   :
                           e.type === 5 ? 'color:var(--accent-orange)' : '';
        return `<tr>
          <td>${msToTimeStr(e.ts)}</td>
          <td style="${labelClass}">${label}</td>
          <td>${e.moisture}%</td>
          <td>${e.tank}%</td>
          <td>${e.water_ml > 0 ? e.water_ml.toFixed(0) : '—'}</td>
          <td class="dim">${e.notes || ''}</td>
        </tr>`;
    }).join('');
}

// ─────────────────────────────────────────────────────────────
//  Profile selection
// ─────────────────────────────────────────────────────────────
function wireProfileChips() {
    document.querySelectorAll('.profile-chip').forEach(chip => {
        chip.addEventListener('click', async () => {
            const id = chip.dataset.id;
            try {
                await apiPost('/api/profile', { profile: id });
                highlightProfile(id);
                fetchStatus();   // Refresh thresholds immediately
            } catch (e) {
                showAlert('Profile change failed: ' + e.message);
            }
        });
    });
}

function highlightProfile(id) {
    document.querySelectorAll('.profile-chip').forEach(c => {
        c.classList.toggle('active', c.dataset.id === id);
    });
}

// ─────────────────────────────────────────────────────────────
//  Manual watering controls
// ─────────────────────────────────────────────────────────────
async function manualWater() {
    const btn = document.getElementById('btn-water');
    btn.disabled = true;
    btn.textContent = '⏳ Sending…';
    try {
        await apiPost('/api/water');
        btn.textContent = '✅ Started';
        setTimeout(() => { btn.textContent = '💧 Water Now'; btn.disabled = false; }, 2000);
    } catch (e) {
        showAlert('Water command failed: ' + e.message);
        btn.textContent = '💧 Water Now';
        btn.disabled = false;
    }
}

async function emergencyStop() {
    if (!confirm('Stop the pump immediately?')) return;
    try {
        await apiPost('/api/stop');
    } catch (e) {
        showAlert('Stop command failed: ' + e.message);
    }
}

// ─────────────────────────────────────────────────────────────
//  Config toggles
// ─────────────────────────────────────────────────────────────
async function saveConfig() {
    const autoEn     = document.getElementById('chk-auto')?.checked ?? true;
    const adaptiveEn = document.getElementById('chk-adaptive')?.checked ?? true;
    try {
        await apiPost('/api/config', {
            auto_watering:      autoEn,
            adaptive_enabled:   adaptiveEn
        });
    } catch (e) {
        showAlert('Config save failed: ' + e.message);
    }
}

// ─────────────────────────────────────────────────────────────
//  Calibration
// ─────────────────────────────────────────────────────────────
async function calibSample() {
    const btn = document.getElementById('btn-sample');
    btn.disabled = true;
    try {
        const r = await apiPost('/api/calibrate', { action: 'sample' });
        document.getElementById('calib-result').textContent =
            `Raw ADC: ${r.raw}  →  ${r.pct?.toFixed(1)}%`;
    } catch (e) {
        showAlert('Calibration sample failed: ' + e.message);
    }
    btn.disabled = false;
}

async function calibSet() {
    const dry = parseInt(document.getElementById('calib-dry').value);
    const wet = parseInt(document.getElementById('calib-wet').value);
    if (isNaN(dry) || isNaN(wet)) { showAlert('Enter both dry and wet ADC values'); return; }
    if (dry <= wet) { showAlert('Dry value must be greater than wet value'); return; }
    try {
        await apiPost('/api/calibrate', { action: 'set', dry, wet });
        document.getElementById('calib-result').textContent =
            `✅ Calibration saved: dry=${dry}  wet=${wet}`;
    } catch (e) {
        showAlert('Calibration save failed: ' + e.message);
    }
}

// ─────────────────────────────────────────────────────────────
//  Wi-Fi credentials
// ─────────────────────────────────────────────────────────────
async function saveWifi() {
    const ssid = document.getElementById('wifi-ssid')?.value?.trim();
    const pass = document.getElementById('wifi-pass')?.value ?? '';
    if (!ssid) { showAlert('SSID is required'); return; }
    try {
        await apiPost('/api/wifi', { ssid, password: pass });
        showAlert(`✅ Credentials saved for "${ssid}". ESP32 is connecting…`);
    } catch (e) {
        showAlert('Wi-Fi save failed: ' + e.message);
    }
}

// ─────────────────────────────────────────────────────────────
//  History clear
// ─────────────────────────────────────────────────────────────
async function clearHistory() {
    if (!confirm('Clear all history and reset the drying model?')) return;
    try {
        await apiPost('/api/reset-history');
        g_sparklineData = [];
        fetchHistory();
    } catch (e) {
        showAlert('Clear failed: ' + e.message);
    }
}

// ─────────────────────────────────────────────────────────────
//  UI helpers
// ─────────────────────────────────────────────────────────────
function setText(id, val) {
    const el = document.getElementById(id);
    if (el) el.textContent = val;
}

function setStyle(id, prop, val) {
    const el = document.getElementById(id);
    if (el) el.style[prop] = val;
}

function setCheck(id, val) {
    const el = document.getElementById(id);
    if (el) el.checked = Boolean(val);
}

function setBadge(id, text, cls) {
    const el = document.getElementById(id);
    if (!el) return;
    el.textContent = text;
    el.className = 'badge ' + (cls || '');
}

function showAlert(msg) {
    const banner = document.getElementById('alert-banner');
    const text   = document.getElementById('alert-text');
    if (banner && text) {
        text.textContent = msg;
        banner.classList.remove('hidden');
    }
}

function hideAlert() {
    document.getElementById('alert-banner')?.classList.add('hidden');
}

function updateWifiPill(connected, label) {
    const dot = document.getElementById('wifi-dot');
    const lbl = document.getElementById('wifi-label');
    if (dot) {
        dot.className = 'pill-dot ' + (connected ? 'green' : connected === false ? 'yellow pulse' : 'pulse');
    }
    if (lbl) lbl.textContent = label;
}

// ─────────────────────────────────────────────────────────────
//  Time / duration formatting
// ─────────────────────────────────────────────────────────────
function msToHMS(ms) {
    const s = Math.floor(ms / 1000);
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    const sec = s % 60;
    if (h > 0) return `${h}h ${m}m`;
    if (m > 0) return `${m}m ${sec}s`;
    return `${sec}s`;
}

function msToRelative(lastMs, uptimeMs) {
    // lastMs is millis() on device; uptimeMs is current uptime
    const diffMs = uptimeMs - lastMs;
    if (diffMs < 0) return '—';
    const mins = Math.floor(diffMs / 60000);
    if (mins < 1)   return 'just now';
    if (mins < 60)  return `${mins}m ago`;
    const h = Math.floor(mins / 60);
    const m = mins % 60;
    return `${h}h ${m}m ago`;
}

function msToTimeStr(uptimeMs) {
    // Approximate wall-clock from uptime (no NTP)
    // For accurate time, the server includes time.hms in status
    const s = Math.floor(uptimeMs / 1000);
    const h = Math.floor(s / 3600) % 24;
    const m = Math.floor((s % 3600) / 60);
    const sec = s % 60;
    return `${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(sec).padStart(2,'0')}`;
}
