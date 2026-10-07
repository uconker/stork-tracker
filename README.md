# Storch-Reise: stork viewer (Heltec WiFi LoRa 32 V3)

Two modes on a pixel map of Europe/Africa. One button (PRG): tap = next stork, hold = switch mode.
Screen off after 3 min without a press (any press wakes it). Battery icon top right of the info panel.

* **LIVE** - the last ~30 days of each tracked stork, a blinking dot where it was last seen, "vor 3 h", coordinates, km in 7 days.
  Cycles through the animals by itself every 8 s. Needs WiFi and the GitHub workflow below.
* **REPLAY** - animated migration of past tracks (works offline; tracks.h, see "Replay data").

Without `board/src/secrets.h` the board runs REPLAY only.

## Flash
    cd board
    copy src\secrets.h.example src\secrets.h      (then edit WiFi + DATA_URL)
    python -m platformio run -t upload

## Live data (GitHub Actions + Movebank)
Study used: "LifeTrack White Stork Bavaria" (Movebank study ID 24442409, PI Wolfgang Fiedler), licence CC BY.
Please credit: Fiedler W, Leppelsack E, Leppelsack H, Stahl T, Wieding O, Wikelski M. 2024. Data from: Study "LifeTrack White Stork Bavaria" (2014-2023). Movebank Data Repository. https://doi.org/10.5441/001/1.v1cs4nn0_2
1. Open the study in Movebank while logged in and accept its terms once (if it asks). Positions are rounded to ~11 km and delayed 24 h (`--delay-hours`); the board shows the 20 most recently seen storks.
2. Put this folder in a GitHub repo. Repo > Settings > Secrets and variables > Actions: add `MOVEBANK_USER` and `MOVEBANK_PASSWORD` (your own login, typed by you).
3. Actions tab > "update stork live data" > Run workflow. It writes `data/live.txt` every 3 h.
4. DATA_URL in secrets.h = `https://raw.githubusercontent.com/<user>/<repo>/main/data/live.txt`
Test without Movebank: `python tools/fetch_live.py --input some_events.csv --out data/live.txt` (data/live_example.txt is an invented example).
Note: the download call follows Movebank's documented API but could not be tested from here; if the workflow log shows an error, send it to me.

## Replay data
    python tools/make_tracks.py --movebank "...gps.csv" --animals 4      real data from a Movebank GPS csv (check the study's terms)
    python tools/make_tracks.py --demo                                    invented tracks
Then flash again. `python tools/make_assets.py` regenerates map and stork picture (already in board/src/assets.h).
Preview on the PC: test/sim.cpp runs the real firmware against the real display library (screenshots in preview/).
