"""Web ground station: Flask JSON API + single-page Leaflet map.

Degrades gracefully with no internet: the map tiles simply fail to load and
the page falls back to a plain coordinate readout + track list.
"""

import json
import threading

from flask import Flask, Response

_HTML = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Firelink Ground Station</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<style>
  body { margin: 0; font-family: -apple-system, sans-serif; background: #0d1117; color: #e6edf3; }
  #map { position: absolute; inset: 0; background: #0d1117; }
  #hud { position: absolute; top: 10px; left: 10px; z-index: 1000;
         background: rgba(13,17,23,.88); border: 1px solid #30363d;
         border-radius: 10px; padding: 10px 14px; font-size: 13px; min-width: 240px; }
  #hud .state { font-weight: 800; font-size: 15px; }
  #hud .mono { font-family: ui-monospace, Menlo, monospace; }
  #hud table td { padding: 1px 6px 1px 0; }
  .st-ARMED { color: #f85149; } .st-PREFLIGHT { color: #58a6ff; }
  .st-LANDED { color: #3fb950; } .st-BOOT { color: #8b949e; }
  #hud a { color: #58a6ff; text-decoration: none; }
</style>
</head>
<body>
<div id="map"></div>
<div id="hud">
  <div class="state st-BOOT" id="state">-</div>
  <table class="mono">
    <tr><td>latitude</td><td id="lat">-</td></tr>
    <tr><td>longitude</td><td id="lon">-</td></tr>
    <tr><td>gps alt</td><td id="galt">-</td></tr>
    <tr><td>baro alt</td><td id="balt">-</td></tr>
    <tr><td>max alt</td><td id="malt">-</td></tr>
    <tr><td>sats</td><td id="sats">-</td></tr>
    <tr><td>packets</td><td id="pkts">-</td></tr>
    <tr><td>last rx</td><td id="age">-</td></tr>
    <tr><td>geiger</td><td id="cpm">-</td></tr>
    <tr><td></td><td><a id="gmaps" target="_blank" href="#">open in maps</a></td></tr>
  </table>
</div>
<script>
const map = L.map('map', {attributionControl: false}).setView([55, -3], 6);
let online = true;
try {
  L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {maxZoom: 19})
    .addTo(map);
} catch (e) { online = false; }
const track = L.polyline([], {color: '#f85149'}).addTo(map);
const marker = L.circleMarker([0, 0], {radius: 6, color: '#3fb950'}).addTo(map);
let seenFirstFix = false;

async function poll() {
  let j;
  try { j = await (await fetch('/api/live')).json(); } catch (e) { return; }
  const t = j.latest || {};
  const st = document.getElementById('state');
  st.textContent = t.state_name || 'no link';
  st.className = 'state st-' + (t.state_name || 'BOOT');
  document.getElementById('lat').textContent = (t.lat ?? 0).toFixed(6);
  document.getElementById('lon').textContent = (t.lon ?? 0).toFixed(6);
  document.getElementById('galt').textContent = (t.gps_alt_m ?? 0) + ' m';
  document.getElementById('balt').textContent = (t.baro_alt_rel_m ?? 0) + ' m';
  document.getElementById('malt').textContent = (j.max_alt ?? 0).toFixed(0) + ' m';
  document.getElementById('sats').textContent = t.sats ?? 0;
  document.getElementById('pkts').textContent =
      j.packets_ok + ' ok, ' + j.packets_lost + ' lost';
  document.getElementById('age').textContent =
      j.latest ? j.rx_age_s.toFixed(1) + ' s ago' : '-';
  document.getElementById('cpm').textContent = (t.geiger_cpm ?? 0) + ' CPM';
  const gm = document.getElementById('gmaps');
  gm.href = 'https://maps.google.com/?q=' + t.lat + ',' + t.lon;

  if (j.track.length > 0) {
    const pts = j.track.map(p => [p[0], p[1]]);
    track.setLatLngs(pts);
    marker.setLatLng(pts[pts.length - 1]);
    if (!seenFirstFix) { map.setView(pts[pts.length - 1], 15); seenFirstFix = true; }
  }
}
setInterval(poll, 1000); poll();
</script>
</body>
</html>
"""


def create_app(store):
    app = Flask(__name__)

    @app.get("/")
    def index():
        return Response(_HTML, mimetype="text/html")

    @app.get("/api/live")
    def live():
        snap = store.snapshot()
        body = {
            "latest": snap["latest"],
            "rx_age_s": snap["rx_age_s"],
            "packets_ok": snap["packets_ok"],
            "packets_bad": snap["packets_bad"],
            "packets_lost": snap["packets_lost"],
            "rssi_dbm": snap["rssi_dbm"],
            "max_alt": snap["max_alt"],
            "track": [list(p[:3]) for p in snap["track"]],
            "events": [list(e) for e in snap["events"][-20:]],
        }
        return Response(json.dumps(body), mimetype="application/json")

    return app


def run_web(store, host="127.0.0.1", port=8080):
    """Start the web server in a daemon thread."""
    app = create_app(store)

    def serve():
        app.run(host=host, port=port, threaded=True, use_reloader=False)

    threading.Thread(target=serve, daemon=True).start()
    return port
