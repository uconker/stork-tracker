#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
unsigned long millis(); void delay(unsigned long); int digitalRead(int); void digitalWrite(int,int); void pinMode(int,int);
unsigned analogReadMilliVolts(int);
struct SerialC { void begin(int){} void printf(const char *f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a);} void println(const char *s){puts(s);} };
extern SerialC Serial;
