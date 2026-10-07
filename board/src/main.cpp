// Stork viewer for Heltec WiFi LoRa 32 V3 (128x64 OLED, PRG button).
//   LIVE   (needs WiFi + secrets.h): the last weeks of each tracked stork and where it was last seen
//   REPLAY (works offline):          animated migration replay of past tracks (tracks.h)
//   tap PRG = next stork      hold PRG = switch LIVE <-> REPLAY      3 min without a press = screen off (press to wake)
#include <Arduino.h>
#if defined(__has_include)
#  if __has_include("secrets.h") && !defined(NO_NET)
#    include "secrets.h"
#    define HAVE_NET 1
#  endif
#endif
#ifndef HAVE_NET
#  define HAVE_NET 0
#endif
#if HAVE_NET
#  include <WiFi.h>
#  include <WiFiMulti.h>
#  include <WiFiClientSecure.h>
#  include <HTTPClient.h>
#endif
#include <U8g2lib.h>
#include <math.h>
#include "assets.h"      // generated: land mask + stork sprite   (tools/make_assets.py)
#include "tracks.h"      // generated: the animals' tracks         (tools/make_tracks.py)

#define VEXT_PIN 36
#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21
#define BTN_PIN  0
#define VBAT_PIN 1            // battery divider (Heltec V3)
#define VBAT_CTRL 37          // LOW = divider connected
#define BAT_CAL 4.9f          // divider factor; adjust if "[bat]" on Serial differs from a multimeter

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

static const uint32_t HOLD_MS = 700, DEBOUNCE_MS = 30;
static const uint32_t STEP_MS = 110;                   // time per track point (183 points = about 20 s per animal)
static const uint32_t END_HOLD_MS = 3500;              // pause at the end of a track before the next animal
static const uint32_t SPLASH_AUTO_MS = 9000;           // start screen leaves by itself
static const uint32_t SLEEP_AFTER_MS = 3UL * 60 * 1000;
static const int HOME_LON10 = 115, HOME_LAT10 = 481;   // Bavaria (nest area)
static const int MAPX = 0;                              // map occupies x 0..63

enum State { S_SPLASH, S_REPLAY, S_LIVE };
static State gState = S_SPLASH;
static const uint32_t LIVE_ROTATE_MS = 8000;           // live view moves to the next animal by itself
static const uint32_t REFRESH_OK_MS = 3UL * 3600 * 1000, REFRESH_FAIL_MS = 10UL * 60 * 1000;
static int gTrack = 0;
static uint32_t gStateT = 0, gLastActivity = 0;
static bool gScreenOn = true, gSwallow = false;
static uint8_t gMap[(MAP_SIZE / 8) * MAP_SIZE];         // coast + dotted land, built once
static uint16_t gCum[N_TRACKS][220];                    // cumulative km per point
static int gBatPct = -1; static uint32_t gLastBat = 0;

// ---------------------------------------------------------------- setup helpers
static bool landAt(int x, int y) {
  if (x < 0 || y < 0 || x >= MAP_SIZE || y >= MAP_SIZE) return false;
  return (pgm_read_byte(&MAP_LAND[y * (MAP_SIZE / 8) + (x >> 3)]) >> (x & 7)) & 1;
}
static void buildMap() {
  memset(gMap, 0, sizeof(gMap));
  for (int y = 0; y < MAP_SIZE; y++) for (int x = 0; x < MAP_SIZE; x++) {
    if (!landAt(x, y)) continue;
    bool coast = !landAt(x - 1, y) || !landAt(x + 1, y) || !landAt(x, y - 1) || !landAt(x, y + 1);
    bool dot = ((x & 3) == 0) && ((y & 3) == 0);                  // sparse land dots, coast line solid
    if (coast || dot) gMap[y * (MAP_SIZE / 8) + (x >> 3)] |= 1 << (x & 7);
  }
}
static float haversine(float lon1, float lat1, float lon2, float lat2) {
  const float p = 0.0174533f;
  float a = sinf((lat2 - lat1) * p / 2), b = sinf((lon2 - lon1) * p / 2);
  float h = a * a + cosf(lat1 * p) * cosf(lat2 * p) * b * b;
  return 12742.0f * asinf(sqrtf(h));
}
static void buildDistances() {
  for (int t = 0; t < N_TRACKS; t++) {
    float km = 0; gCum[t][0] = 0;
    for (int i = 1; i < TRACKS[t].n && i < 220; i++) {
      km += haversine(TRACKS[t].lon[i - 1] / 10.0f, TRACKS[t].lat[i - 1] / 10.0f, TRACKS[t].lon[i] / 10.0f, TRACKS[t].lat[i] / 10.0f);
      gCum[t][i] = km > 65000 ? 65000 : (uint16_t)km;
    }
  }
}
static void readBattery() {
  pinMode(VBAT_CTRL, OUTPUT); digitalWrite(VBAT_CTRL, LOW); delay(5);
  uint32_t mv = 0; for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(VBAT_PIN);
  pinMode(VBAT_CTRL, INPUT);
  float v = (mv / 8) * BAT_CAL / 1000.0f;
  static const float vt[] = {3.30f, 3.50f, 3.70f, 3.80f, 3.90f, 4.05f, 4.15f};
  static const int   pt[] = {0, 5, 25, 45, 65, 90, 100};
  int pct = 100;
  if (v <= vt[0]) pct = 0;
  else for (int i = 1; i < 7; i++) if (v <= vt[i]) { pct = pt[i-1] + (int)((v - vt[i-1]) / (vt[i] - vt[i-1]) * (pt[i] - pt[i-1])); break; }
  gBatPct = v < 2.0f ? -1 : pct; gLastBat = millis();
  Serial.printf("[bat] %.2f V -> %d %%\n", v, pct);
}
static void drawBattery(int x, int y) {
  if (gBatPct < 0) return;
  u8g2.drawFrame(x, y, 10, 6); u8g2.drawBox(x + 10, y + 2, 2, 2);
  if (gBatPct < 10 && (millis() / 600) % 2) return;
  int w = (gBatPct * 8 + 50) / 100; if (w > 0) u8g2.drawBox(x + 1, y + 1, w, 4);
}

// ---------------------------------------------------------------- live data
// File format (written by tools/fetch_live.py):   STORKLIVE1 / gen=... / A|name|minutes since last fix / P|lon10|lat10|hours ago  (oldest point first)
#define LMAXA 20
#define LMAXP 64
struct LiveA { char name[12]; uint8_t n; uint16_t ageMin; int16_t lon[LMAXP], lat[LMAXP]; uint16_t ageH[LMAXP]; };
static LiveA gLive[LMAXA]; static int gLiveN = 0, gLiveIdx = 0;
static bool gLiveOk = false;                              // have usable live data
static uint32_t gLiveAt = 0, gLiveTry = 0, gLiveRotT = 0; // when downloaded / last attempt / last auto-rotate
static char gLiveErr[32] = "";

static bool liveParse(const char *txt) {                 // returns true if at least one animal was read
  static LiveA tmp[LMAXA]; int n = 0; int cur = -1;
  const char *p = txt; bool head = false;
  while (*p) {
    const char *e = strchr(p, '\n'); size_t len = e ? (size_t)(e - p) : strlen(p);
    char line[96]; if (len >= sizeof line) len = sizeof line - 1; memcpy(line, p, len); line[len] = 0;
    if (len && line[len - 1] == '\r') line[len - 1] = 0;
    p = e ? e + 1 : p + len;
    if (!head) { if (strncmp(line, "STORKLIVE1", 10) != 0) return false; head = true; continue; }
    if (line[0] == 'A' && line[1] == '|' && n < LMAXA) {
      char *nm = line + 2, *bar = strchr(nm, '|'); if (!bar) continue; *bar = 0;
      memset(&tmp[n], 0, sizeof(LiveA)); snprintf(tmp[n].name, sizeof tmp[n].name, "%.10s", nm); tmp[n].ageMin = (uint16_t)atoi(bar + 1); cur = n++;
    } else if (line[0] == 'P' && line[1] == '|' && cur >= 0 && tmp[cur].n < LMAXP) {
      int lo, la, ah; if (sscanf(line + 2, "%d|%d|%d", &lo, &la, &ah) == 3) { int k = tmp[cur].n++; tmp[cur].lon[k] = lo; tmp[cur].lat[k] = la; tmp[cur].ageH[k] = ah; }
    }
  }
  int m = 0; for (int i = 0; i < n; i++) if (tmp[i].n > 0) gLive[m++] = tmp[i];
  if (m == 0) return false;
  gLiveN = m; gLiveIdx = 0; gLiveOk = true; gLiveAt = millis(); return true;
}

#if HAVE_NET
static bool liveFetch() {
  gLiveTry = millis(); gLiveErr[0] = 0;
  static WiFiMulti multi; static bool added = false;
  if (!added) {
    multi.addAP(WIFI_SSID, WIFI_PASSWORD);
#ifdef WIFI_EXTRA
    struct Net { const char *ssid, *pass; }; static const Net extra[] = WIFI_EXTRA;
    for (const Net &x : extra) multi.addAP(x.ssid, x.pass);
#endif
    added = true;
  }
  WiFi.mode(WIFI_STA);
  bool ok = false;
  if (multi.run(20000) != WL_CONNECTED) snprintf(gLiveErr, sizeof gLiveErr, "WLAN nicht verbunden");
  else {
    WiFiClientSecure client; client.setInsecure(); HTTPClient http; http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, DATA_URL)) snprintf(gLiveErr, sizeof gLiveErr, "URL ungueltig");
    else {
      int code = http.GET();
      if (code == 200) { String b = http.getString(); if (liveParse(b.c_str())) ok = true; else snprintf(gLiveErr, sizeof gLiveErr, "Datei ungueltig"); }
      else snprintf(gLiveErr, sizeof gLiveErr, code < 0 ? "Verbindung %d" : "HTTP %d", code);
      http.end();
    }
  }
  WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
  Serial.printf("[live] %s %s\n", ok ? "ok" : "failed", gLiveErr);
  return ok;
}
#endif

// ---------------------------------------------------------------- dates
static void civilFromDays(long z, int &y, int &m, int &d) {      // days since 1970-01-01 -> y/m/d
  z += 719468; long era = (z >= 0 ? z : z - 146096) / 146097; long doe = z - era * 146097;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; y = (int)(yoe + era * 400);
  long doy = doe - (365 * yoe + yoe / 4 - yoe / 100); long mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1); m = (int)(mp < 10 ? mp + 3 : mp - 9); if (m <= 2) y++;
}
static long daysFromCivil(int y, int m, int d) {
  y -= m <= 2; long era = (y >= 0 ? y : y - 399) / 400; long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
static const char *MONTH[] = {"Jan", "Feb", "Mrz", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"};

// ---------------------------------------------------------------- drawing
static void mapXY(int lon10, int lat10, int &x, int &y) {
  x = MAPX + (int)lroundf((lon10 / 10.0f - MAP_LON0) * MAP_PXDEG);
  y = (int)lroundf((MAP_LAT0 - lat10 / 10.0f) * MAP_PXDEG);
}
static void drawSplash(uint32_t now) {
  u8g2.drawXBMP(2, 2, STORK_W, STORK_H, STORK_BMP);
  u8g2.setFont(u8g2_font_10x20_tf);
  u8g2.drawStr(52, 22, "STORCH"); u8g2.drawStr(52, 42, "REISE");
  u8g2.setFont(u8g2_font_5x8_tf);
  if ((now / 600) % 2 == 0) u8g2.drawStr(52, 58, "Taste: Start");
}
static void drawReplay(uint32_t now) {
  const Track &T = TRACKS[gTrack];
  uint32_t el = now - gStateT;
  int idx = (int)(el / STEP_MS); bool done = false;
  if (idx >= T.n - 1) { idx = T.n - 1; done = true; }

  u8g2.drawXBM(MAPX, 0, MAP_SIZE, MAP_SIZE, gMap);
  int hx, hy; mapXY(HOME_LON10, HOME_LAT10, hx, hy);                 // home marker: small cross
  u8g2.drawHLine(hx - 2, hy, 5); u8g2.drawVLine(hx, hy - 2, 5);
  int px, py; mapXY(T.lon[0], T.lat[0], px, py);
  for (int pass = 0; pass < 2; pass++) {                              // pass 0: black halo, pass 1: 2 px wide white trail
    int qx = px, qy = py;
    for (int i = 1; i <= idx; i++) {
      int x, y; mapXY(T.lon[i], T.lat[i], x, y);
      if (pass == 0) {
        u8g2.setDrawColor(0);
        for (int dx = -1; dx <= 2; dx++) for (int dy = -1; dy <= 1; dy++) if (dx || dy) u8g2.drawLine(qx + dx, qy + dy, x + dx, y + dy);
        u8g2.setDrawColor(1);
      } else { u8g2.drawLine(qx, qy, x, y); u8g2.drawLine(qx + 1, qy, x + 1, y); }
      qx = x; qy = y;
    }
    if (pass == 1) { px = qx; py = qy; }
  }
  if (done || (now / 250) % 2 == 0) { u8g2.setDrawColor(0); u8g2.drawDisc(px, py, 3); u8g2.setDrawColor(1); u8g2.drawDisc(px, py, 2); }
  u8g2.drawFrame(MAPX + 0, 0, MAP_SIZE, MAP_SIZE > 64 ? 64 : MAP_SIZE);

  const int X = 68;                                                   // info panel x
  u8g2.setFont(u8g2_font_6x10_tf);
  char b[24]; snprintf(b, sizeof b, "%.10s", T.name);
  u8g2.drawStr(X, 10, b); u8g2.drawStr(X + 1, 10, b);
  u8g2.setFont(u8g2_font_5x8_tf);
  snprintf(b, sizeof b, "%d/%d", gTrack + 1, N_TRACKS); u8g2.drawStr(X, 21, b);
  drawBattery(116, 14);
  int y, m, d; civilFromDays(daysFromCivil(T.year, T.month, T.day) + (long)idx * STEP_DAYS, y, m, d);
  snprintf(b, sizeof b, "%d.%s %d", d, MONTH[m - 1], y % 100 + 2000); u8g2.drawStr(X, 33, b);
  if (el < 6000) u8g2.drawStr(X, 44, "Tippen: weiter");
  else if (done) u8g2.drawStr(X, 44, "Fertig");
  else { snprintf(b, sizeof b, "Tag %d", idx * STEP_DAYS + 1); u8g2.drawStr(X, 44, b); }
  u8g2.setFont(u8g2_font_6x10_tf);
  snprintf(b, sizeof b, "%u km", (unsigned)gCum[gTrack][idx]); u8g2.drawStr(X, 55, b);
  int w = (int)((58L * idx) / (T.n - 1)); u8g2.drawFrame(X, 58, 60, 5); if (w > 0) u8g2.drawBox(X + 1, 59, w, 3);
}

static void fmtAge(uint32_t min, char *b, size_t n) {
  if (min < 90) snprintf(b, n, "vor %u min", (unsigned)min);
  else if (min < 48 * 60) snprintf(b, n, "vor %u h", (unsigned)(min / 60));
  else snprintf(b, n, "vor %u d", (unsigned)(min / 1440));
}
static void drawLive(uint32_t now) {
  if (!gLiveOk) {
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.drawStr(0, 14, "Keine Live-Daten"); u8g2.drawStr(0, 30, gLiveErr[0] ? gLiveErr : "lade...");
    u8g2.setFont(u8g2_font_5x8_tf); u8g2.drawStr(0, 46, "Halten: Replay"); return;
  }
  const LiveA &A = gLive[gLiveIdx];
  u8g2.drawXBM(MAPX, 0, MAP_SIZE, MAP_SIZE, gMap);
  int px = 0, py = 0;
  for (int pass = 0; pass < 2; pass++) {
    int qx = 0, qy = 0;
    for (int i = 0; i < A.n; i++) {
      int x, y; mapXY(A.lon[i], A.lat[i], x, y);
      if (i > 0) {
        if (pass == 0) { u8g2.setDrawColor(0); for (int dx = -1; dx <= 2; dx++) for (int dy = -1; dy <= 1; dy++) if (dx || dy) u8g2.drawLine(qx + dx, qy + dy, x + dx, y + dy); u8g2.setDrawColor(1); }
        else { u8g2.drawLine(qx, qy, x, y); u8g2.drawLine(qx + 1, qy, x + 1, y); }
      }
      qx = x; qy = y;
    }
    px = qx; py = qy;
  }
  if ((now / 300) % 2 == 0) { u8g2.setDrawColor(0); u8g2.drawDisc(px, py, 4); u8g2.setDrawColor(1); u8g2.drawDisc(px, py, 3); }
  else { u8g2.drawCircle(px, py, 3); }
  u8g2.drawFrame(MAPX, 0, MAP_SIZE, MAP_SIZE);

  const int X = 68; char b[24];
  u8g2.setFont(u8g2_font_6x10_tf);
  snprintf(b, sizeof b, "%.10s", A.name); u8g2.drawStr(X, 10, b); u8g2.drawStr(X + 1, 10, b);
  u8g2.setFont(u8g2_font_5x8_tf);
  snprintf(b, sizeof b, "LIVE %d/%d", gLiveIdx + 1, gLiveN); u8g2.drawStr(X, 21, b);
  drawBattery(116, 14);
  fmtAge(A.ageMin + (now - gLiveAt) / 60000UL, b, sizeof b); u8g2.drawStr(X, 33, b);
  int la = A.lat[A.n - 1], lo = A.lon[A.n - 1];
  snprintf(b, sizeof b, "%d.%d%c %d.%d%c", abs(la) / 10, abs(la) % 10, la >= 0 ? 'N' : 'S', abs(lo) / 10, abs(lo) % 10, lo >= 0 ? 'E' : 'W');
  u8g2.drawStr(X, 44, b);
  float km = 0; for (int i = 1; i < A.n; i++) if (A.ageH[i - 1] <= 168) km += haversine(A.lon[i - 1] / 10.0f, A.lat[i - 1] / 10.0f, A.lon[i] / 10.0f, A.lat[i] / 10.0f);
  u8g2.setFont(u8g2_font_6x10_tf); snprintf(b, sizeof b, "7d %d km", (int)(km + 0.5f)); u8g2.drawStr(X, 56, b);
}

// ---------------------------------------------------------------- button
enum Gesture { G_NONE, G_TAP, G_HOLD };
static Gesture pollButton() {
  static bool down = false, holdFired = false; static uint32_t tDown = 0;
  bool p = digitalRead(BTN_PIN) == LOW; uint32_t now = millis();
  if (p) gLastActivity = now;
  if (p && !down) { down = true; tDown = now; holdFired = false; }
  else if (p && down && !holdFired && now - tDown >= HOLD_MS) { holdFired = true; return G_HOLD; }
  else if (!p && down) { down = false; if (!holdFired && now - tDown >= DEBOUNCE_MS) return G_TAP; }
  return G_NONE;
}

// ---------------------------------------------------------------- Arduino
void setup() {
  Serial.begin(115200);
  pinMode(VEXT_PIN, OUTPUT); digitalWrite(VEXT_PIN, LOW);
  pinMode(BTN_PIN, INPUT_PULLUP);
  delay(100);
  u8g2.begin(); u8g2.enableUTF8Print();
  buildMap(); buildDistances(); readBattery();
  gStateT = gLastActivity = millis();
#if HAVE_NET
  u8g2.clearBuffer(); drawSplash(millis()); u8g2.setDrawColor(0); u8g2.drawBox(50, 50, 78, 14); u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_5x8_tf); u8g2.drawStr(52, 58, "Lade Live-Daten..."); u8g2.sendBuffer();
  liveFetch(); gStateT = millis();
#endif
}

void loop() {
  uint32_t now = millis();
  Gesture g = pollButton();

  bool wantOn = (now - gLastActivity < SLEEP_AFTER_MS);               // screen off after 3 min without a press
  if (wantOn != gScreenOn) { gScreenOn = wantOn; u8g2.setPowerSave(gScreenOn ? 0 : 1); if (gScreenOn) gSwallow = true; }
  if (g != G_NONE && gSwallow) { g = G_NONE; gSwallow = false; }      // the press that wakes it is not a command
  if (!gScreenOn) { delay(50); return; }

  if (now - gLastBat > 30000) readBattery();

  if (gState == S_SPLASH) {
    if (g != G_NONE || now - gStateT > SPLASH_AUTO_MS) { gState = gLiveOk ? S_LIVE : S_REPLAY; gTrack = 0; gStateT = gLiveRotT = now; }
  } else if (gState == S_REPLAY) {
    if (g == G_TAP) { gTrack = (gTrack + 1) % N_TRACKS; gStateT = now; }
    else if (g == G_HOLD) { gState = S_LIVE; gLiveRotT = now; }                       // hold = switch mode
    else if (now - gStateT > (uint32_t)(TRACKS[gTrack].n - 1) * STEP_MS + END_HOLD_MS) { gTrack = (gTrack + 1) % N_TRACKS; gStateT = now; }
  } else {   // S_LIVE
    if (g == G_TAP && gLiveN) { gLiveIdx = (gLiveIdx + 1) % gLiveN; gLiveRotT = now; }
    else if (g == G_HOLD) { gState = S_REPLAY; gStateT = now; }
    else if (gLiveN > 1 && now - gLiveRotT > LIVE_ROTATE_MS) { gLiveIdx = (gLiveIdx + 1) % gLiveN; gLiveRotT = now; }
  }
#if HAVE_NET
  if (now - gLastActivity > 10000 && now - gLiveTry > (gLiveOk ? REFRESH_OK_MS : REFRESH_FAIL_MS)) { liveFetch(); gLiveRotT = millis(); }
#endif

  u8g2.clearBuffer();
  if (gState == S_SPLASH) drawSplash(now); else if (gState == S_LIVE) drawLive(now); else drawReplay(now);
  u8g2.sendBuffer();
  delay(20);
}
