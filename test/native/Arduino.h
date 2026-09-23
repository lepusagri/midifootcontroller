#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <vector>
using byte = uint8_t;
#define PROGMEM
#define memcpy_P memcpy
#define F(x) x
extern unsigned long testMillis;
inline unsigned long millis() { return testMillis; }
inline void delay(unsigned long ms) { testMillis += ms; }
class Print {
public:
  template<class T> void print(T) {}
  template<class T> void println(T) {}
  void println() {}
};
class HardwareSerial : public Print {
public:
  std::deque<byte> input;
  std::vector<byte> output;
  void begin(unsigned) {}
  int available() { return int(input.size()); }
  byte read() { byte value = input.front(); input.pop_front(); return value; }
  size_t write(byte value) { output.push_back(value); return 1; }
};
