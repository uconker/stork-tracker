#!/usr/bin/env python3
"""Turns stork tracks into board/src/tracks.h (one point every STEP_DAYS days over one Aug->Jul migration year).

  python make_tracks.py --demo                          invented example tracks (for trying the device out)
  python make_tracks.py --movebank file.csv [--animals 4] [--year 2019]   real GPS data exported from Movebank
Movebank CSV needs the columns: timestamp, location-long, location-lat, individual-local-identifier
(tag-local-identifier works too). Check the study's licence/terms before using or sharing its data."""
import argparse, csv, math, random, re, sys
from datetime import datetime, timedelta, timezone

STEP_DAYS = 2
REACH = 40.0           # an animal must get south of this latitude (Spain/Italy or further) to count as migrating
MAXPTS = 200            # 200 * 2 days = 400 days

def dist_km(a, b):
    p = math.pi / 180
    h = math.sin((b[1]-a[1])*p/2)**2 + math.cos(a[1]*p)*math.cos(b[1]*p)*math.sin((b[0]-a[0])*p/2)**2
    return 12742 * math.asin(math.sqrt(h))

def resample(pts, start):
    """pts: sorted [(datetime, lon, lat)]; returns [(lon, lat)] every STEP_DAYS from `start`."""
    out, j = [], 0
    t = start
    end = start + timedelta(days=365)
    while t < end and len(out) < MAXPTS:
        while j+1 < len(pts) and pts[j+1][0] <= t: j += 1
        a = pts[j]
        if j+1 < len(pts) and a[0] <= t:
            b = pts[j+1]; gap = (b[0]-a[0]).total_seconds()
            if 0 < gap <= 14*86400:
                f = (t-a[0]).total_seconds() / gap
                out.append((a[1]+(b[1]-a[1])*f, a[2]+(b[2]-a[2])*f)); t += timedelta(days=STEP_DAYS); continue
        out.append((a[1], a[2])); t += timedelta(days=STEP_DAYS)
    return out

# ---------------------------------------------------------------- real data
def from_movebank(path, n_animals, year):
    per = {}
    with open(path, newline="", encoding="utf-8-sig") as f:
        r = csv.DictReader(f)
        idc = "individual-local-identifier" if "individual-local-identifier" in r.fieldnames else "tag-local-identifier"
        last = {}; seen_rows = 0
        for row in r:
            if (row.get("visible") or "true").strip().lower() == "false": continue
            if "true" in ((row.get("manually-marked-outlier") or "").lower(), (row.get("import-marked-outlier") or "").lower(), (row.get("algorithm-marked-outlier") or "").lower()): continue
            try:
                lon, lat = float(row["location-long"]), float(row["location-lat"])
                ts = datetime.fromisoformat(row["timestamp"].replace("Z", "+00:00").split(".")[0]).replace(tzinfo=None)
            except (ValueError, KeyError): continue
            k = row[idc]
            if k in last and (ts-last[k]).total_seconds() < 12*3600 and ts >= last[k]: continue      # thin to ~2/day (keeps memory small)
            last[k] = ts; per.setdefault(k, []).append((ts, lon, lat)); seen_rows += 1
    print("read %d GPS fixes from %d animals" % (seen_rows, len(per)))
    if not per: sys.exit("no GPS positions found in this file (columns location-long / location-lat empty?). A file with only acceleration rows cannot be used.")
    cands = []
    for k, pts in per.items():
        pts.sort()
        for y in sorted({p[0].year for p in pts}):
            if year and y != year: continue
            s = datetime(y, 8, 1); e = datetime(y+1, 8, 1)
            sub = [p for p in pts if s <= p[0] < e]
            if len(sub) < 60: continue
            days = len({p[0].date() for p in sub})
            south = min(p[2] for p in sub)
            if sub[0][2] < 35 or south > REACH or sub[0][0] > s + timedelta(days=20): continue   # must start in Europe and reach Africa
            cands.append((0 if south < 28 else 1, -days, k, y, sub))     # animals reaching the Sahel/Africa first, then by coverage
    cands.sort(key=lambda c: (c[0], c[1])); seen, chosen = set(), []
    for _, _, k, y, sub in cands:
        if k in seen: continue
        seen.add(k); chosen.append((str(k), datetime(y, 8, 1), resample(sub, datetime(y, 8, 1))))
        if len(chosen) == n_animals: break
    if not chosen: sys.exit("no suitable tracks (need an animal that starts in Europe in August and reaches Africa). Try --year or another study.")
    return chosen

# ---------------------------------------------------------------- demo data
ROUTES = {
  "Demo West": [(11.5,48.1),(6,45),(-1,40.5),(-5.6,36.2),(-8,31),(-5,24),(-2,16),(-3,13.5)],
  "Demo Ost":  [(11.5,48.1),(19,45.5),(26,42),(29,41),(35,36),(34.5,29),(32.5,21),(33,11),(36.5,3),(37,0)],
  "Demo Mitte":[(11.5,48.1),(8,44),(9,38),(10,34),(9,28),(2,20),(4,13)],
}
def demo(seed=7):
    random.seed(seed); out = []
    for name, wp in ROUTES.items():
        seg = [dist_km(wp[i], wp[i+1]) for i in range(len(wp)-1)]; tot = sum(seg)
        def at(f):                                   # f in 0..1 along the route
            d = f*tot
            for i, s in enumerate(seg):
                if d <= s or i == len(seg)-1:
                    g = min(1, d/s); return (wp[i][0]+(wp[i+1][0]-wp[i][0])*g, wp[i][1]+(wp[i+1][1]-wp[i][1])*g)
                d -= s
        dep, arr, back, home = random.randint(35, 50), random.randint(95, 125), random.randint(235, 255), random.randint(300, 325)
        pts = []
        for day in range(0, 365, STEP_DAYS):
            if day < dep: f = 0
            elif day < arr: f = (day-dep)/(arr-dep)
            elif day < back: f = 1
            elif day < home: f = 1-(day-back)/(home-back)
            else: f = 0
            f = f*f*(3-2*f)*0.0 + f                  # linear is fine; jitter makes it look alive
            x, y = at(f)
            j = 0.6 if 0 < f < 1 else 0.15
            pts.append((x+random.uniform(-j, j), y+random.uniform(-j, j)))
        out.append((name, datetime(2024, 8, 1), pts))
    return out

# ---------------------------------------------------------------- output
MONTHS = None
def emit(tracks, path, source):
    with open(path, "w") as f:
        f.write("// generated by tools/make_tracks.py (%s) - do not edit\n#pragma once\n#include <Arduino.h>\n" % source)
        f.write("#define N_TRACKS %d\n#define STEP_DAYS %d\n" % (len(tracks), STEP_DAYS))
        f.write("struct Track { const char *name; uint16_t n; uint16_t year; uint8_t month, day; const int16_t *lon, *lat; };   // lon/lat in 0.1 degree\n")
        for i, (name, start, pts) in enumerate(tracks):
            f.write("static const int16_t T%d_LON[] = {%s};\n" % (i, ",".join(str(round(p[0]*10)) for p in pts)))
            f.write("static const int16_t T%d_LAT[] = {%s};\n" % (i, ",".join(str(round(p[1]*10)) for p in pts)))
        f.write("static const Track TRACKS[N_TRACKS] = {\n")
        for i, (name, start, pts) in enumerate(tracks):
            nm = re.split(r"\s[+/]", name)[0].strip()                    # "Aldo +  / DER AT881 (eobs 3946)" -> "Aldo"
            nm = nm.encode("ascii", "replace").decode().replace('"', "")[:10]
            f.write('  {"%s", %d, %d, %d, %d, T%d_LON, T%d_LAT},\n' % (nm, len(pts), start.year, start.month, start.day, i, i))
        f.write("};\n")
    print("wrote %s: %s" % (path, ", ".join("%s (%d points)" % (t[0], len(t[2])) for t in tracks)))

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--demo", action="store_true"); ap.add_argument("--movebank")
    ap.add_argument("--animals", type=int, default=4); ap.add_argument("--year", type=int, default=0)
    ap.add_argument("--out", default="../board/src/tracks.h")
    a = ap.parse_args()
    if a.movebank: emit(from_movebank(a.movebank, a.animals, a.year), a.out, "Movebank " + a.movebank)
    else: emit(demo(), a.out, "DEMO data, invented")
