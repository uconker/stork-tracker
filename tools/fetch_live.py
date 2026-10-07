#!/usr/bin/env python3
"""Downloads the recent GPS positions of a Movebank study and writes data/live.txt for the board.

Needs your Movebank login (free account) in the environment: MOVEBANK_USER, MOVEBANK_PASSWORD.
Before first use, open the study once in the browser while logged in and accept its terms/licence.

  python fetch_live.py                          # live download
  python fetch_live.py --input events.csv       # use a CSV you downloaded yourself (testing)
Privacy/permission: positions are rounded to 0.1 degree (~11 km) and delayed by --delay-hours (default 24 h).
Please ask the study's owner before publishing live positions or lowering the delay."""
import argparse, csv, io, math, os, re, sys, urllib.parse, urllib.request, base64
from datetime import datetime, timedelta, timezone

STUDY_ID = "170501269"            # Ciconia ciconia Sudewiesen_2
API = "https://www.movebank.org/movebank/service/direct-read"
MAXP = 64                         # points per animal on the board

def dist_km(a, b):
    p = math.pi / 180
    h = math.sin((b[1]-a[1])*p/2)**2 + math.cos(a[1]*p)*math.cos(b[1]*p)*math.sin((b[0]-a[0])*p/2)**2
    return 12742 * math.asin(math.sqrt(h))

def download(study, days, user, pw):
    start = (datetime.now(timezone.utc) - timedelta(days=days)).strftime("%Y%m%d%H%M%S000")
    q = urllib.parse.urlencode({"entity_type": "event", "study_id": study, "sensor_type_id": 653,
                                "timestamp_start": start,
                                "attributes": "timestamp,location_long,location_lat,individual_local_identifier"})
    req = urllib.request.Request(API + "?" + q)
    req.add_header("Authorization", "Basic " + base64.b64encode(("%s:%s" % (user, pw)).encode()).decode())
    with urllib.request.urlopen(req, timeout=120) as r:
        body = r.read().decode("utf-8-sig")
    if body.lstrip().lower().startswith(("<html", "<!doctype", "<?xml")) or "license" in body[:300].lower() and "timestamp" not in body[:300].lower():
        sys.exit("Movebank answered with its licence/terms page instead of data. Open the study in your browser while logged in, "
                 "accept the terms once, then try again.")
    return body

def parse(text, delay_h):
    r = csv.DictReader(io.StringIO(text)); per = {}
    cut = datetime.now(timezone.utc).replace(tzinfo=None) - timedelta(hours=delay_h)
    for row in r:
        try:
            lon = float(row.get("location-long") or row["location_long"]); lat = float(row.get("location-lat") or row["location_lat"])
            ts = datetime.fromisoformat(row["timestamp"].replace("Z", "").split(".")[0])
            k = row.get("individual-local-identifier") or row["individual_local_identifier"]
        except (ValueError, KeyError, TypeError): continue
        if ts > cut: continue
        per.setdefault(k, []).append((ts, lon, lat))
    return per, cut

def short(name):
    nm = re.split(r"\s[+/(]", name)[0].strip().encode("ascii", "replace").decode().replace("|", "")
    return nm[:10] or "?"

def build(per, now, delay_h, step_h=12):
    out = ["STORKLIVE1", "gen=%s" % now.strftime("%Y-%m-%dT%H:%MZ")]
    n_animals = 0
    for k in sorted(per):
        pts = sorted(per[k]); thin, last = [], None
        for p in pts:
            if last is None or (p[0]-last).total_seconds() >= step_h*3600: thin.append(p); last = p[0]
        if pts[-1] is not thin[-1]: thin.append(pts[-1])        # always include the newest fix
        thin = thin[-MAXP:]
        age_min = int((now - pts[-1][0]).total_seconds() // 60)
        out.append("A|%s|%d" % (short(k), age_min))
        for t, lon, lat in thin:
            out.append("P|%d|%d|%d" % (round(lon*10), round(lat*10), int((now - t).total_seconds() // 3600)))
        n_animals += 1
    return "\n".join(out) + "\n", n_animals

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--input"); ap.add_argument("--study", default=STUDY_ID)
    ap.add_argument("--days", type=int, default=45); ap.add_argument("--delay-hours", type=float, default=24)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "live.txt"))
    a = ap.parse_args()
    if a.input: text = open(a.input, encoding="utf-8-sig").read()
    else:
        u, p = os.environ.get("MOVEBANK_USER"), os.environ.get("MOVEBANK_PASSWORD")
        if not u or not p: sys.exit("set MOVEBANK_USER and MOVEBANK_PASSWORD (your Movebank login) in the environment")
        text = download(a.study, a.days, u, p)
    per, cut = parse(text, a.delay_hours)
    body, n = build(per, datetime.now(timezone.utc).replace(tzinfo=None), a.delay_hours)
    if n < 1: sys.exit("no GPS positions in the data - keeping the old file")
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    open(a.out, "w", newline="\n").write(body)
    print("wrote %s: %d animals, %d bytes" % (a.out, n, len(body)))
