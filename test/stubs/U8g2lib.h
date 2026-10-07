#pragma once
#include "Arduino.h"
extern "C" {
#include "u8g2.h"
}
struct U8G2_SSD1306_128X64_NONAME_F_HW_I2C {
  u8g2_t u;
  static uint8_t cb(u8x8_t *, uint8_t, uint8_t, void *) { return 1; }
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C(const u8g2_cb_t *r, int, int, int) { u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u, r, cb, cb); }
  void begin() { u8g2_InitDisplay(&u); u8g2_SetPowerSave(&u, 0); }
  void enableUTF8Print() {}
  void clearBuffer() { u8g2_ClearBuffer(&u); }
  void sendBuffer() {}
  void setFont(const uint8_t *f) { u8g2_SetFont(&u, f); }
  void setPowerSave(int v) { (void)v; }
  void setContrast(int) {}
  void setDrawColor(int c) { u8g2_SetDrawColor(&u, c); }
  int drawStr(int x, int y, const char *s) { return u8g2_DrawStr(&u, x, y, s); }
  int drawUTF8(int x, int y, const char *s) { return u8g2_DrawUTF8(&u, x, y, s); }
  int getUTF8Width(const char *s) { return u8g2_GetUTF8Width(&u, s); }
  void drawXBM(int x, int y, int w, int h, const uint8_t *b) { u8g2_DrawXBM(&u, x, y, w, h, b); }
  void drawXBMP(int x, int y, int w, int h, const uint8_t *b) { u8g2_DrawXBM(&u, x, y, w, h, b); }
  void drawLine(int a, int b, int c, int d) { u8g2_DrawLine(&u, a, b, c, d); }
  void drawCircle(int x, int y, int r) { u8g2_DrawCircle(&u, x, y, r, U8G2_DRAW_ALL); }
  void drawDisc(int x, int y, int r) { u8g2_DrawDisc(&u, x, y, r, U8G2_DRAW_ALL); }
  void drawFrame(int x, int y, int w, int h) { u8g2_DrawFrame(&u, x, y, w, h); }
  void drawBox(int x, int y, int w, int h) { u8g2_DrawBox(&u, x, y, w, h); }
  void drawHLine(int x, int y, int w) { u8g2_DrawHLine(&u, x, y, w); }
  void drawVLine(int x, int y, int h) { u8g2_DrawVLine(&u, x, y, h); }
  void drawPixel(int x, int y) { u8g2_DrawPixel(&u, x, y); }
};
