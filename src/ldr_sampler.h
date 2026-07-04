#pragma once
// LDR sampler — ดูประวัติแก้ไขใน CHANGELOG.md (v3.35+)

#include <Arduino.h>

/** ตั้ง ADC สำหรับขา LDR — เรียกครั้งเดียวใน setup() */
inline void setupLdrAdc(uint8_t ldr1Pin, uint8_t ldr2Pin) {
  analogReadResolution(12);
  analogSetPinAttenuation(ldr1Pin, ADC_11db);
  analogSetPinAttenuation(ldr2Pin, ADC_11db);
  (void)analogRead(ldr1Pin);
  (void)analogRead(ldr2Pin);
  delayMicroseconds(500);
}

static const uint8_t LDR_AVG_SAMPLES = 10;
static const unsigned long LDR_SAMPLE_GAP_MS = 4;
/** ช่วงขั้นต่ำระหว่างการอ่าน LDR รอบใหม่ (loop ตรวจ power/end/light) */
static const unsigned long LDR_READ_INTERVAL_MS = 700;
/** ค่าต่ำกว่านี้ถือว่า glitch ADC — ไม่นับใน median / peak */
static const int LDR_GLITCH_FLOOR = 35;

inline int ldrMedianInPlace(int *buf, uint8_t n) {
  if (n == 0) return 0;
  for (uint8_t i = 1; i < n; i++) {
    int key = buf[i];
    int j = (int)i - 1;
    while (j >= 0 && buf[j] > key) {
      buf[j + 1] = buf[j];
      j--;
    }
    buf[j + 1] = key;
  }
  return buf[n / 2];
}

/** พิมพ์สรุปค่า LDR หลังอ่านเสร็จ — ใช้ร่วมกับ context เช่น "Power is on.." */
inline void printLdrSummary(const char *context, uint8_t pin, int avg) {
  Serial.print(context);
  Serial.print(" | LDR pin=");
  Serial.print(pin);
  Serial.print(" avg=");
  Serial.println(avg);
}

/** เก็บ sample ทีละตัวระหว่าง loop — ไม่ block MQTT/WiFi; ใช้ median กรอง spike 0 */
struct LdrAvgSampler {
  uint8_t pin = 0;
  uint8_t need = LDR_AVG_SAMPLES;
  uint8_t got = 0;
  bool discardDone = false;
  bool ready = false;
  int result = 0;
  unsigned long lastMs = 0;
  const char *logCtx = nullptr;
  int sampleBuf[16];

  void begin(uint8_t p, uint8_t samples = LDR_AVG_SAMPLES, const char *ctx = nullptr) {
    pin = p;
    need = samples < 2 ? 2 : (samples > 16 ? 16 : samples);
    got = 0;
    discardDone = ready = false;
    result = 0;
    lastMs = 0;
    logCtx = ctx;
  }

  /** คืน true เมื่อได้ค่า median ใน out */
  bool tick(int *out = nullptr) {
    if (ready) {
      if (out) {
        *out = result;
      }
      return true;
    }
    unsigned long now = millis();
    if (lastMs != 0 && (now - lastMs) < LDR_SAMPLE_GAP_MS) {
      return false;
    }
    lastMs = now;

    int raw = analogRead(pin);
    if (!discardDone) {
      discardDone = true;
      return false;
    }

    if (raw >= LDR_GLITCH_FLOOR) {
      sampleBuf[got++] = raw;
    }
    if (got >= need) {
      int work[16];
      uint8_t n = got < need ? got : need;
      if (n == 0) {
        result = 0;
      } else {
        for (uint8_t i = 0; i < n; i++) {
          work[i] = sampleBuf[i];
        }
        result = ldrMedianInPlace(work, n);
      }
      ready = true;
      if (logCtx) {
        printLdrSummary(logCtx, pin, result);
      }
      if (out) {
        *out = result;
      }
      return true;
    }
    return false;
  }
};

/** ติดตามค่าสูงสุดในหน้าต่างล่าสุด — จับไฟเครื่องที่กระพริบ (Mode 1 หลัง Power) */
struct LdrPeakWindow {
  static const uint8_t CAP = 24;
  int buf[CAP];
  uint8_t idx = 0;
  uint8_t count = 0;

  void reset() {
    idx = count = 0;
  }

  void push(int v) {
    if (v < LDR_GLITCH_FLOOR) {
      return;
    }
    buf[idx] = v;
    idx = (uint8_t)((idx + 1) % CAP);
    if (count < CAP) {
      count++;
    }
  }

  int peak() const {
    int m = 0;
    for (uint8_t i = 0; i < count; i++) {
      if (buf[i] > m) {
        m = buf[i];
      }
    }
    return m;
  }
};

/** อ่าน LDR ครั้งเดียว (ไม่เฉลี่ย) — ใช้ Mode 1 ตรวจไฟเครื่องที่กระพริบ */
inline int readLDRInstant(int pin, const char *logCtx = nullptr) {
  int val = analogRead(pin);
  if (logCtx) {
    printLdrSummary(logCtx, (uint8_t)pin, val);
  }
  return val;
}

/** อ่าน LDR แบบ median — blocking สั้น (~4 ms) สำหรับจุดที่เรียกไม่บ่อย */
inline int readLDRAverage(int pin, int samples = LDR_AVG_SAMPLES, const char *logCtx = nullptr) {
  if (samples < 2) {
    samples = 2;
  }
  if (samples > 16) {
    samples = 16;
  }
  (void)analogRead(pin);
  delayMicroseconds(300);

  int buf[16];
  uint8_t n = 0;
  for (int i = 0; i < samples; i++) {
    int raw = analogRead(pin);
    if (raw >= LDR_GLITCH_FLOOR) {
      buf[n++] = raw;
    }
    if (i + 1 < samples) {
      delayMicroseconds(400);
    }
  }
  int avg = 0;
  if (n > 0) {
    int work[16];
    for (uint8_t i = 0; i < n; i++) {
      work[i] = buf[i];
    }
    avg = ldrMedianInPlace(work, n);
  }
  if (logCtx) {
    printLdrSummary(logCtx, (uint8_t)pin, avg);
  }
  return avg;
}
