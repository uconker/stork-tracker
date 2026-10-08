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

STUDY_ID = "24442409"             # LifeTrack White Stork Bavaria (CC BY, W. Fiedler et al.)
API = "https://www.movebank.org/movebank/service/direct-read"
MAXA = 20                         # animals on the board
MAXP = 64                         # points per animal on the board

def dist_km(a, b):
    p = math.pi / 180
    h = math.sin((b[1]-a[1])*p/2)**2 + math.cos(a[1]*p)*math.cos(b[1]*p)*math.sin((b[0]-a[0])*p/2)**2
    return 12742 * math.asin(math.sqrt(h))

def download(study, days, user, pw):
    start = (datetime.now(timezone.utc) - timedelta(days=days)).strftime("%Y%m%d%H%M%S000")
    q = urllib.parse.urlencode({"entity_type": "event", "study_id": study, "sensor_type_id": 653,
                                "timestamp_start": start,
                                "attributes": "timestamp,location_long,location_lat,individual_local_identifier,visible"})
    req = urllib.request.Request(API + "?" + q)
    req.add_header("Authorization", "Basic " + base64.b64encode(("%s:%s" % (user, pw)).encode()).decode())
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            body = r.read().decode("utf-8-sig", "replace")
            print("Movebank: HTTP %s, %s, %d bytes" % (r.status, r.headers.get("Content-Type"), len(body)))
    except urllib.error.HTTPError as e:
        sys.exit("Movebank: HTTP %s %s - %s" % (e.code, e.reason, e.read()[:300].decode("utf-8", "replace")))
    print("First 300 characters of the answer:\n" + body[:300].replace(pw, "***"))
    if body.lstrip().lower().startswith(("<html", "<!doctype", "<?xml")) or "license" in body[:300].lower() and "timestamp" not in body[:300].lower():
        sys.exit("Movebank answered with its licence/terms page instead of data. Open the study in your browser while logged in, "
                 "accept the terms once, then try again.")
    return body

def get(params, user, pw, limit=600):
    req = urllib.request.Request(API + "?" + urllib.parse.urlencode(params))
    req.add_header("Authorization", "Basic " + base64.b64encode(("%s:%s" % (user, pw)).encode()).decode())
    try:
        with urllib.request.urlopen(req, timeout=120) as r: b = r.read().decode("utf-8-sig", "replace")
    except urllib.error.HTTPError as e: return "HTTP %s %s" % (e.code, e.reason)
    return "%d lines | %s" % (b.count("\n"), b[:limit].replace(pw, "***"))

def probe(study, days, user, pw):
    print("--- no data came back, probing the study ---")
    print("study info:", get({"entity_type": "study", "study_id": study,
          "attributes": "name,i_can_see_data,i_have_download_access,there_are_data_which_i_cannot_see,license_type,license_terms,main_location_lat,timestamp_last_deployed_location"}, user, pw, 900))
    print("sensors in study:", get({"entity_type": "study_attribute", "study_id": study}, user, pw, 400))
    print("sensor types used:", get({"entity_type": "sensor", "study_id": study}, user, pw, 600))
    start = (datetime.now(timezone.utc) - timedelta(days=days)).strftime("%Y%m%d%H%M%S000")
    print("events, any sensor:", get({"entity_type": "event", "study_id": study, "timestamp_start": start,
          "attributes": "timestamp,sensor_type_id,location_long,location_lat,individual_local_identifier"}, user, pw, 600))
    print("events, no time limit:", get({"entity_type": "event", "study_id": study, "sensor_type_id": 653,
          "attributes": "timestamp,location_long,location_lat,individual_local_identifier"}, user, pw, 400))

def parse(text, delay_h):
    r = csv.DictReader(io.StringIO(text)); per = {}
    print("CSV columns:", r.fieldnames); seen = bad = late = 0
    cut = datetime.now(timezone.utc).replace(tzinfo=None) - timedelta(hours=delay_h)
    for row in r:
        seen += 1
        try:
            lon = float(row.get("location-long") or row["location_long"]); lat = float(row.get("location-lat") or row["location_lat"])
            ts = datetime.fromisoformat(row["timestamp"].replace("Z", "").split(".")[0])
            k = (row.get("individual-local-identifier") or row["individual_local_identifier"]).strip()
            if not k: bad += 1; continue                       # fixes without an animal name would mix animals
        except (ValueError, KeyError, TypeError):
            bad += 1
            if bad == 1: print("first unreadable row:", dict(row))
            continue
        if (row.get("visible") or "true").strip().lower() == "false": continue      # Movebank-marked outliers
        if ts > cut: late += 1; continue
        per.setdefault(k, []).append((ts, lon, lat))
    print("rows: %d read, %d unreadable, %d newer than the delay, %d kept (%d animals)" % (seen, bad, late, sum(map(len, per.values())), len(per)))
    return per, cut

def short(name):
    nm = re.split(r"\s[+/(]", str(name))[0].strip().encode("ascii", "replace").decode().replace("|", "").replace('"', "")
    w = nm.split()
    if len(w) > 1 and w[1][:1].isdigit():                  # "Frensdorf 2-D4523" -> "Frensdorf2" (keeps the number that tells siblings apart)
        tag = re.match(r"\d+", w[1]).group(0)[:2]; nm = w[0][:10 - len(tag)] + tag
    return nm[:10] or "?"

def build(per, now, delay_h, step_h=12):
    out = ["STORKLIVE1", "gen=%s" % now.strftime("%Y-%m-%dT%H:%MZ")]
    n_animals = 0
    keep = sorted(per, key=lambda k: max(p[0] for p in per[k]), reverse=True)[:MAXA]   # most recently seen
    base = {k: short(k) for k in keep}
    cnt = {}
    for k in sorted(keep): cnt[base[k]] = cnt.get(base[k], 0) + 1
    seen = {}
    for k in sorted(keep):
        nm = base[k]
        if cnt[nm] > 1:                                        # same short name: Frensdor#1, Frensdor#2 ...
            seen[nm] = seen.get(nm, 0) + 1
            nm = nm.strip()[:8] + "#" + str(seen[nm])
        pts = sorted(per[k]); thin, last = [], None
        for p in pts:
            if last is None or (p[0]-last).total_seconds() >= step_h*3600: thin.append(p); last = p[0]
        if pts[-1] is not thin[-1]: thin.append(pts[-1])        # always include the newest fix
        thin = thin[-MAXP:]
        age_min = int((now - pts[-1][0]).total_seconds() // 60)
        out.append("A|%s|%d" % (nm, age_min))
        for t, lon, lat in thin:
            out.append("P|%d|%d|%d" % (round(lon*10), round(lat*10), int((now - t).total_seconds() // 3600)))
        n_animals += 1
    return "\n".join(out) + "\n", n_animals

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--input"); ap.add_argument("--study", default=STUDY_ID)
    ap.add_argument("--days", type=int, default=10); ap.add_argument("--delay-hours", type=float, default=24)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "live.txt"))
    a = ap.parse_args()
    if a.input: text = open(a.input, encoding="utf-8-sig").read()
    else:
        u, p = os.environ.get("MOVEBANK_USER"), os.environ.get("MOVEBANK_PASSWORD")
        if not u or not p: sys.exit("set MOVEBANK_USER and MOVEBANK_PASSWORD (your Movebank login) in the environment")
        text = download(a.study, a.days, u, p)
    per, cut = parse(text, a.delay_hours)
    body, n = build(per, datetime.now(timezone.utc).replace(tzinfo=None), a.delay_hours)
    if n < 1 and not a.input: probe(a.study, a.days, u, p)
    if n < 1: sys.exit("no GPS positions in the data - keeping the old file")
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    open(a.out, "w", newline="\n").write(body)
    print("wrote %s: %d animals, %d bytes" % (a.out, n, len(body)))
