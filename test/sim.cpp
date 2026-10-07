// host preview: runs the real main.cpp against the real U8g2 C library and dumps frames as raw buffers
#include "Arduino.h"
#include "U8g2lib.h"
static unsigned long g_ms = 1000; static int g_pin = 1; SerialC Serial;
unsigned long millis() { return g_ms; } void delay(unsigned long) {}
int digitalRead(int) { return g_pin; } void digitalWrite(int,int) {} void pinMode(int,int) {}
unsigned analogReadMilliVolts(int) { return 780; }       // 780 mV * 4.9 = 3.82 V
#include "../board/src/main.cpp"
static void dump(const char *name) {
  FILE *f = fopen(name, "wb"); fwrite(u8g2_GetBufferPtr(&u8g2.u), 1, 1024, f); fclose(f);
}
static void run_until(unsigned long t) { while (g_ms < t) { g_ms += 20; loop(); } }
int main() {
  setup(); run_until(1000 + 1500); dump("../preview/f_splash.bin");
  g_pin = 0; run_until(g_ms + 80); g_pin = 1; run_until(g_ms + 100);           // tap -> play
  unsigned long t0 = g_ms; run_until(t0 + 2000);  dump("../preview/f_play1.bin");
  run_until(t0 + 11000); dump("../preview/f_play2.bin");
  run_until(t0 + 20500); dump("../preview/f_play3.bin");
  // tap = next stork
  g_pin = 0; run_until(g_ms + 80); g_pin = 1; run_until(g_ms + 100); unsigned long t1 = g_ms;
  run_until(t1 + 12000); dump("../preview/f_next.bin");
  // live mode with mock data
  { FILE *f = fopen("../data/live_example.txt", "rb"); static char buf[20000]; size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f); printf("liveParse: %d\n", liveParse(buf)); }
  g_pin = 0; run_until(g_ms + 900); g_pin = 1; run_until(g_ms + 100);                // hold -> switch mode
  printf("state after hold = %d (1=replay,2=live)\n", gState);
  if (gState != S_LIVE) { g_pin = 0; run_until(g_ms + 900); g_pin = 1; run_until(g_ms + 100); }
  printf("state = %d\n", gState); run_until(g_ms + 1000); dump("../preview/f_live1.bin");
  g_pin = 0; run_until(g_ms + 80); g_pin = 1; run_until(g_ms + 400); dump("../preview/f_live2.bin");
  // sleep: nothing pressed for 3 min
  run_until(g_ms + 200000); printf("screen on after idle: %d\n", gScreenOn);
  g_pin = 0; run_until(g_ms + 80); g_pin = 1; run_until(g_ms + 100); printf("screen on after press: %d, state=%d track=%d\n", gScreenOn, gState, gTrack);
  return 0;
}
