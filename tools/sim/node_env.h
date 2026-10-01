// ============================================================================
//  NODE_ENV - o "hardware" de UMA tela. Incluido DENTRO do namespace do no,
//  antes do firmware: cada tela tem seu relogio, sua serial, sua NVS, seu GPS.
// ============================================================================
// (sem #pragma once de proposito: cada no.cpp inclui uma vez, no seu namespace)

// ------------------------------------------------------------- relogio
static int      s_no = -1;
static uint64_t s_bootG = 0;      // instante GLOBAL em que este no ligou
static double   s_ppm = 0;        // deriva do cristal deste no
static uint64_t s_us = 0;         // relogio LOCAL (o esp_timer), us desde o boot
static uint64_t s_isrUs = 0;      // durante uma ISR: o instante exato da borda
static esp_reset_reason_t s_motivo = ESP_RST_POWERON;

inline uint64_t simGlobal(uint64_t local) { return s_bootG + (uint64_t)((double)local / (1.0 + s_ppm * 1e-6)); }
inline uint64_t simLocal(uint64_t g) { return g <= s_bootG ? 0 : (uint64_t)((double)(g - s_bootG) * (1.0 + s_ppm * 1e-6)); }

// O CODIGO LEVA TEMPO. Sem isto o firmware rodaria em tempo zero entre dois
// passos do simulador - e o bug do icone de 2026-09-30 (um carimbo POSTERIOR ao
// 'agora' do servico, core/tempo.h) NAO aparecia: o simulador passava verde com
// o bug dentro. Cada leitura do relogio custa um pouco (o laco real nao para no
// tempo), e as operacoes caras do radio custam o que custam (fake_hal/radio.h).
static const uint64_t SIM_CUSTO_RELOGIO_US = 3;
inline int64_t  esp_timer_get_time()
{
  if (s_isrUs) return (int64_t)s_isrUs;      // dentro da ISR: o instante da borda, fixo
  s_us += SIM_CUSTO_RELOGIO_US;
  return (int64_t)s_us;
}
inline uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000); }
inline uint32_t micros() { return (uint32_t)esp_timer_get_time(); }
static void simAvanca(uint64_t us);          // node_api.h: anda o relogio E processa o que acontece
inline void delay(uint32_t ms) { simAvanca((uint64_t)ms * 1000); }
inline void delayMicroseconds(uint32_t us) { simAvanca(us); }
inline void yield() {}
inline void noInterrupts() {}
inline void interrupts() {}
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int  digitalRead(int) { return 0; }
typedef void (*SimIsr)();
static SimIsr s_isrPps = nullptr;
inline void attachInterrupt(int, SimIsr f, int) { s_isrPps = f; }
inline esp_reset_reason_t esp_reset_reason() { return s_motivo; }
static uint32_t s_rand = 12345;
inline uint32_t esp_random() { s_rand = s_rand * 1664525u + 1013904223u; return s_rand; }

// ------------------------------------------------------------- serial (console)
static FILE* s_console = nullptr;
struct SimSerial {
  void begin(unsigned long) {}
  explicit operator bool() const { return true; }
  int printf(const char* f, ...) {
    if (!s_console) return 0;
    va_list a; va_start(a, f); int n = vfprintf(s_console, f, a); va_end(a); return n;
  }
  void print(const char* s) { if (s_console) fputs(s, s_console); }
  void println(const char* s = "") { if (s_console) { fputs(s, s_console); fputc('\n', s_console); } }
  void flush() {}
  int available() { return 0; }
  int read() { return -1; }
};
static SimSerial Serial;

// ------------------------------------------------------------- Serial1 = o GPS
struct SimSerial1 {
  std::deque<char> q;
  void begin(unsigned long, int, int, int) {}
  void setRxBufferSize(size_t) {}
  int available() { return (int)q.size(); }
  int read() { if (q.empty()) return -1; char c = q.front(); q.pop_front(); return (uint8_t)c; }
};
static SimSerial1 Serial1;

// ------------------------------------------------------------- NVS (Preferences)
// Sobrevive a um RELIGAR do no, como a flash de verdade.
static std::map<std::string, std::map<std::string, std::string>> s_nvs;
class Preferences {
  std::string ns_;
  template <typename T> T get_(const char* k, T def) {
    auto& m = s_nvs[ns_]; auto it = m.find(k);
    if (it == m.end() || it->second.size() != sizeof(T)) return def;
    T v; memcpy(&v, it->second.data(), sizeof(T)); return v;
  }
  template <typename T> size_t put_(const char* k, T v) { s_nvs[ns_][k] = std::string((const char*)&v, sizeof(T)); return sizeof(T); }
public:
  bool begin(const char* ns, bool = false) { ns_ = ns; return true; }
  void end() {}
  bool isKey(const char* k) { return s_nvs[ns_].count(k) > 0; }
  uint8_t  getUChar(const char* k, uint8_t d = 0)   { return get_<uint8_t>(k, d); }
  int8_t   getChar(const char* k, int8_t d = 0)     { return get_<int8_t>(k, d); }
  uint32_t getUInt(const char* k, uint32_t d = 0)   { return get_<uint32_t>(k, d); }
  bool     getBool(const char* k, bool d = false)   { return get_<uint8_t>(k, d ? 1 : 0) != 0; }
  size_t putUChar(const char* k, uint8_t v)  { return put_(k, v); }
  size_t putChar(const char* k, int8_t v)    { return put_(k, v); }
  size_t putUInt(const char* k, uint32_t v)  { return put_(k, v); }
  size_t putBool(const char* k, bool v)      { return put_<uint8_t>(k, v ? 1 : 0); }
  String getString(const char* k, const char* d = "") {
    auto& m = s_nvs[ns_]; auto it = m.find(k);
    return it == m.end() ? String(d) : String(it->second.c_str());
  }
  size_t putString(const char* k, const char* v) { s_nvs[ns_][k] = v; return strlen(v); }
};
