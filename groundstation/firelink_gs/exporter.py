"""Post-flight export: GPX and KML track files."""

import html
import time


def export_gpx(path, track):
    """track: iterable of (lat, lon, alt_m, epoch_s)."""
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<gpx version="1.1" creator="firelink-gs" '
        'xmlns="http://www.topografix.com/GPX/1/1">',
        '  <trk><name>Firelink flight</name><trkseg>',
    ]
    for lat, lon, alt, ts in track:
        t = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(ts))
        lines.append(
            f'    <trkpt lat="{lat:.7f}" lon="{lon:.7f}">'
            f'<ele>{alt}</ele><time>{t}</time></trkpt>')
    lines.append('  </trkseg></trk>')
    lines.append('</gpx>')
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return path


def export_kml(path, track, name="Firelink flight"):
    coords = " ".join(f"{lon:.7f},{lat:.7f},{alt}" for lat, lon, alt, _ in track)
    doc = f"""<?xml version="1.0" encoding="UTF-8"?>
<kml xmlns="http://www.opengis.net/kml/2.2">
  <Placemark>
    <name>{html.escape(name)}</name>
    <Style><LineStyle><color>ff0000ff</color><width>3</width></LineStyle></Style>
    <LineString><altitudeMode>absolute</altitudeMode>
      <coordinates>{coords}</coordinates>
    </LineString>
  </Placemark>
</kml>
"""
    with open(path, "w") as f:
        f.write(doc)
    return path
