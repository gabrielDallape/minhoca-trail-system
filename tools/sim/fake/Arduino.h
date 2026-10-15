#pragma once
// SIMULADOR: o "Arduino" do PC. So TIPOS e constantes, globais. Tudo que depende
// do relogio ou do estado de UMA tela (millis, Serial, Serial1, delay, ISR) mora
// em node_env.h, DENTRO do namespace de cada no - cada tela tem o seu.
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdarg>
#include <string>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define IRAM_ATTR
#define RISING 1
#define FALLING 2
#define SERIAL_8N1 0
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define digitalPinToInterrupt(p) (p)
inline void* ps_malloc(size_t n) { return malloc(n); }
class String {
  std::string s_;
public:
  String(const char* c = "") : s_(c ? c : "") {}
  const char* c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
};
