// Embedded pre-flight control page, served at GET / by web/server.cpp.
const char *kFirelinkPage = R"PAGE(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Firelink Flight Computer</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body { font-family: -apple-system, "Segoe UI", Roboto, sans-serif; margin: 0;
         background: #0d1117; color: #e6edf3; }
  header { display: flex; align-items: center; justify-content: space-between;
           padding: 12px 18px; background: #161b22; border-bottom: 1px solid #30363d; }
  h1 { font-size: 1.2rem; margin: 0; letter-spacing: 0.05em; }
  .badge { padding: 4px 14px; border-radius: 999px; font-weight: 700; font-size: 0.95rem; }
  .st-BOOT { background: #6e7681; color: #fff; }
  .st-PREFLIGHT { background: #1f6feb; color: #fff; }
  .st-ARMED { background: #da3633; color: #fff; animation: pulse 1.2s infinite; }
  .st-LANDED { background: #2da44e; color: #fff; }
  @keyframes pulse { 50% { opacity: 0.55; } }
  main { padding: 14px; display: grid; gap: 14px; max-width: 980px; margin: 0 auto; }
  section { background: #161b22; border: 1px solid #30363d; border-radius: 10px; padding: 14px 16px; }
  h2 { margin: 0 0 10px; font-size: 0.85rem; text-transform: uppercase;
       letter-spacing: 0.08em; color: #8b949e; }
  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 8px; }
  .kv { background: #0d1117; border-radius: 8px; padding: 8px 10px; }
  .kv .k { font-size: 0.7rem; color: #8b949e; text-transform: uppercase; }
  .kv .v { font-size: 1.05rem; font-weight: 600; margin-top: 2px; word-break: break-all; }
  .ok { color: #3fb950; } .bad { color: #f85149; }
  button { border: 0; border-radius: 8px; padding: 10px 18px; font-size: 0.95rem;
           font-weight: 700; cursor: pointer; margin: 4px 6px 4px 0; }
  #arm { background: #da3633; color: #fff; }
  #disarm { background: #2da44e; color: #fff; }
  .secondary { background: #30363d; color: #e6edf3; }
  table { width: 100%; border-collapse: collapse; font-size: 0.85rem; }
  td, th { padding: 4px 6px; text-align: left; border-bottom: 1px solid #21262d; }
  input[type=text] { width: 100%; background: #0d1117; color: #e6edf3;
                     border: 1px solid #30363d; border-radius: 6px; padding: 6px 8px; }
  a { color: #58a6ff; }
  #msg { min-height: 1.2em; font-size: 0.9rem; }
  .mono { font-family: ui-monospace, Menlo, monospace; }
</style>
</head>
<body>
<header>
  <h1>FIRELINK</h1>
  <span>uptime <span id="uptime" class="mono">-</span></span>
  <span id="state-badge" class="badge st-BOOT">BOOT</span>
</header>
<main>
  <section>
    <h2>Status</h2>
    <div class="grid" id="status-grid"></div>
  </section>

  <section>
    <h2>Actions</h2>
    <button id="arm">ARM</button>
    <button id="disarm">DISARM</button>
    <button id="recal" class="secondary">Recalibrate pad pressure</button>
    <button id="restart" class="secondary">Restart flight computer</button>
    <div id="msg"></div>
  </section>

  <section>
    <h2>Settings <span style="font-weight:400">(saved to firelink.conf; some need restart)</span></h2>
    <table id="cfg-table"></table>
    <br>
    <button id="save-cfg" class="secondary">Save settings</button>
  </section>

  <section>
    <h2>Recorded data</h2>
    <div id="files"></div>
    <br>
    <button id="refresh-files" class="secondary">Refresh list</button>
  </section>
</main>
<script>
const $ = id => document.getElementById(id);

function fmt(x, digits=5) { return (typeof x === 'number') ? x.toFixed(digits) : x; }

async function poll() {
  try {
    const s = await (await fetch('/api/status')).json();
    const badge = $('state-badge');
    badge.textContent = s.state;
    badge.className = 'badge st-' + s.state;
    $('uptime').textContent = Math.floor(s.uptime_s) + 's';

    const rows = [
      ['GPS', s.gps_ok ? (s.gps_fix ? 'FIX (' + s.gps_sats + ' sats)' : 'no fix') : 'offline',
        s.gps_fix ? 'ok' : 'bad'],
      ['Latitude', fmt(s.gps_lat, 6)], ['Longitude', fmt(s.gps_lon, 6)],
      ['GPS alt', fmt(s.gps_alt_m, 1) + ' m'], ['Speed', fmt(s.gps_speed_kmh, 1) + ' km/h'],
      ['Baro alt (rel)', fmt(s.baro_alt_rel_m, 1) + ' m'],
      ['Pressure', fmt(s.pressure_hpa, 2) + ' hPa'],
      ['Temperature', fmt(s.temperature_c, 1) + ' \u00B0C'],
      ['Humidity', fmt(s.humidity_pct, 0) + ' %'],
      ['|Accel|', fmt(s.accel_mag, 2) + ' m/s\u00B2'],
      ['Geiger CPM', s.geiger_cpm],
      ['Pad pressure', fmt(s.pad_pressure_hpa, 2) + ' hPa'],
      ['Recording', s.recording ? 'YES \u2192 ' + s.session_dir : 'no',
        s.recording ? 'ok' : ''],
      ['Disk free', s.disk_free_mb + ' MB'],
      ['Telemetry seq', s.telemetry_seq],
      ['Max alt', fmt(s.max_alt_rel_m, 1) + ' m'],
      ['Max |a|', fmt(s.max_accel_mag, 1) + ' m/s\u00B2'],
    ];
    $('status-grid').innerHTML = rows.map(([k, v, cls]) =>
      `<div class="kv"><div class="k">${k}</div><div class="v ${cls || ''}">${v}</div></div>`).join('');
  } catch (e) { /* retry next tick */ }
}
setInterval(poll, 1000); poll();

function msg(t, bad) {
  const m = $('msg');
  m.textContent = t;
  m.style.color = bad ? '#f85149' : '#3fb950';
}

$('arm').onclick = async () => {
  if (!confirm('Arm the flight computer? Data recording and telemetry will begin.')) return;
  let r = await (await fetch('/api/arm', {method: 'POST', body: ''})).json();
  if (!r.ok && r.checks_failed) {
    if (confirm('Arm checks failed: ' + r.checks_failed.join(', ') +
                '\n\nArm anyway (force)?')) {
      r = await (await fetch('/api/arm', {method: 'POST', body: 'force=true'})).json();
    }
  }
  msg(r.ok ? 'Armed. Good flight.' : ('Arm rejected: ' + (r.error || '?')), !r.ok);
};

$('disarm').onclick = async () => {
  const r = await (await fetch('/api/disarm', {method: 'POST', body: ''})).json();
  msg(r.ok ? 'Disarmed.' : ('Disarm failed: ' + (r.error || '?')), !r.ok);
};

$('recal').onclick = async () => {
  msg('Measuring pad pressure (keep still)...');
  const r = await (await fetch('/api/recalibrate', {method: 'POST', body: ''})).json();
  msg(r.ok ? ('Pad pressure: ' + fmt(r.pad_pressure_hpa, 2) + ' hPa')
           : ('Recalibration failed: ' + (r.error || '?')), !r.ok);
};

$('restart').onclick = async () => {
  if (!confirm('Restart the flight computer process? The page will reload in ~10 s.')) return;
  await fetch('/api/restart', {method: 'POST', body: ''});
  setTimeout(() => location.reload(), 10000);
};

async function loadConfig() {
  const cfg = await (await fetch('/api/config')).json();
  const keys = Object.keys(cfg).sort();
  $('cfg-table').innerHTML =
    '<tr><th>Key</th><th>Value</th></tr>' +
    keys.map(k => `<tr><td class="mono">${k}</td>
      <td><input type="text" data-key="${k}" value="${String(cfg[k]).replace(/"/g, '&quot;')}"></td></tr>`).join('');
}

$('save-cfg').onclick = async () => {
  const params = [];
  document.querySelectorAll('#cfg-table input').forEach(i =>
    params.push(encodeURIComponent(i.dataset.key) + '=' + encodeURIComponent(i.value)));
  const r = await (await fetch('/api/config', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: params.join('&'),
  })).json();
  msg(r.ok ? (r.restart_required ? 'Saved. Some settings need a restart to take effect.'
                                 : 'Saved.')
           : 'Save failed', !r.ok);
  loadConfig();
};
loadConfig();

async function loadFiles() {
  const j = await (await fetch('/api/data')).json();
  $('files').innerHTML = j.files.length
    ? '<table>' + j.files.map(f =>
        `<tr><td><a href="/data/${f.path}">${f.path}</a></td>
         <td>${(f.size / 1024).toFixed(1)} KB</td></tr>`).join('') + '</table>'
    : 'No recordings yet.';
}
$('refresh-files').onclick = loadFiles;
loadFiles();
</script>
</body>
</html>
)PAGE";
