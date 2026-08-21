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

/** หน้าต่างสั้นจับไฟกระพริบหลัง Power (Mode 1) — peak=สูงสุด, trough=ต่ำสุด */
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

  /** สูงสุด — บอร์ดเก่า (สว่าง=ค่าสูง) */
  int peak() const {
    int m = 0;
    for (uint8_t i = 0; i < count; i++) {
      if (buf[i] > m) {
        m = buf[i];
      }
    }
    return m;
  }

  /** ต่ำสุด — บอร์ดใหม่ (สว่าง=ค่าต่ำ); ว่าง = 4095 */
  int trough() const {
    if (count == 0) {
      return 4095;
    }
    int m = buf[0];
    for (uint8_t i = 1; i < count; i++) {
      if (buf[i] < m) {
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

// --- Mode 1 light profile (learn blink period + levels) ---
struct LdrLightProfile {
  bool valid = false;      // มีอย่างน้อย BLINK
  bool hasOn = false;
  bool hasOff = false;
  uint16_t periodMs = 500; // สว่างสุด → มืดสุด
  int brightLevel = 0;
  int darkLevel = 0;
  int onLevel = 0;
  int offLevel = 0;
};

/** brightness สูง = สว่าง (normalize polarity) */
inline int ldrBrightnessScore(int raw, int oldBoard) {
  if (oldBoard == 1) {
    return raw;
  }
  return 4095 - raw;
}

inline bool ldrNearLevel(int val, int level, int tol) {
  if (tol < 100) {
    tol = 100;
  }
  int d = val - level;
  if (d < 0) {
    d = -d;
  }
  return d <= tol;
}

/** เรียนรู้คาบกระพริบ — อ่านถี่ ~4 วิ; คืน true ถ้าวัด period ได้ */
inline bool learnLdrBlinkProfile(LdrLightProfile *p, int pin, int oldBoard) {
  if (!p) {
    return false;
  }
  const unsigned long sampleGapMs = 80;
  const unsigned long captureMs = 4000;
  const int glitchFloor = LDR_GLITCH_FLOOR;

  int periods[16];
  uint8_t periodN = 0;
  int peakBright = -1;
  int troughBright = -1;
  int peakRaw = 0;
  int troughRaw = 0;
  unsigned long peakMs = 0;
  bool havePeak = false;

  int maxBright = -1;
  int minBright = 99999;
  int maxRaw = 0;
  int minRaw = 4095;

  unsigned long start = millis();
  while ((millis() - start) < captureMs) {
    int raw = analogRead(pin);
    if (raw < glitchFloor) {
      delay(sampleGapMs);
      continue;
    }
    int b = ldrBrightnessScore(raw, oldBoard);
    if (b > maxBright) {
      maxBright = b;
      maxRaw = raw;
    }
    if (b < minBright) {
      minBright = b;
      minRaw = raw;
    }

    // peak = local high then drop; trough = local low after peak
    if (!havePeak) {
      if (peakBright < 0 || b >= peakBright) {
        peakBright = b;
        peakRaw = raw;
        peakMs = millis();
      } else if (peakBright >= 0 && (peakBright - b) >= 200) {
        havePeak = true;
        troughBright = b;
        troughRaw = raw;
      }
    } else {
      if (b <= troughBright) {
        troughBright = b;
        troughRaw = raw;
      } else if ((b - troughBright) >= 150) {
        unsigned long dt = millis() - peakMs;
        if (dt >= 80 && dt <= 3000 && periodN < 16) {
          periods[periodN++] = (int)dt;
        }
        havePeak = false;
        peakBright = b;
        peakRaw = raw;
        peakMs = millis();
        troughBright = -1;
      }
    }
    delay(sampleGapMs);
  }

  if (maxBright < 0 || minBright > 9000 || (maxBright - minBright) < 200) {
    Serial.println("learnLdrBlink: span too small / no samples");
    return false;
  }

  uint16_t period = 500;
  if (periodN > 0) {
    int work[16];
    for (uint8_t i = 0; i < periodN; i++) {
      work[i] = periods[i];
    }
    period = (uint16_t)ldrMedianInPlace(work, periodN);
  } else {
    // fallback: ไม่จับ edge ชัด — ประมาณครึ่งคาบจากจังหวะทั่วไป
    period = 400;
  }
  if (period < 100) {
    period = 100;
  }
  if (period > 2500) {
    period = 2500;
  }

  p->periodMs = period;
  p->brightLevel = maxRaw;
  p->darkLevel = minRaw;
  p->valid = true;

  Serial.print("learnLdrBlink: period=");
  Serial.print(p->periodMs);
  Serial.print(" bright=");
  Serial.print(p->brightLevel);
  Serial.print(" dark=");
  Serial.print(p->darkLevel);
  Serial.print(" periodN=");
  Serial.println(periodN);
  return true;
}

inline bool learnLdrOnProfile(LdrLightProfile *p, int pin) {
  if (!p) {
    return false;
  }
  int sum = 0;
  int n = 0;
  for (int i = 0; i < 20; i++) {
    int raw = analogRead(pin);
    if (raw >= LDR_GLITCH_FLOOR) {
      sum += raw;
      n++;
    }
    delay(100);
  }
  if (n < 5) {
    return false;
  }
  p->onLevel = sum / n;
  p->hasOn = true;
  Serial.print("learnLdrOn: ");
  Serial.println(p->onLevel);
  return true;
}

inline bool learnLdrOffProfile(LdrLightProfile *p, int pin) {
  if (!p) {
    return false;
  }
  int sum = 0;
  int n = 0;
  for (int i = 0; i < 20; i++) {
    int raw = analogRead(pin);
    if (raw >= LDR_GLITCH_FLOOR) {
      sum += raw;
      n++;
    }
    delay(100);
  }
  if (n < 5) {
    return false;
  }
  p->offLevel = sum / n;
  p->hasOff = true;
  Serial.print("learnLdrOff: ");
  Serial.println(p->offLevel);
  return true;
}

/**
 * Mode 6: มีแค่เปิด/ปิด (ไม่มีกระพริบ) — ใช้ hasOn + hasOff
 * คืน 0=off 1=uncertain 2=on หรือ -1 = ยังเรียนไม่ครบ
 */
inline int checkLightOnOffProfile(const LdrLightProfile *p, int pin) {
  if (!p || !p->hasOn || !p->hasOff) {
    return -1;
  }

  const unsigned long sampleGapMs = 100;
  const unsigned long winMs = 2000;
  int span = p->onLevel - p->offLevel;
  if (span < 0) {
    span = -span;
  }
  int levelTol = span / 3;
  if (levelTol < 200) {
    levelTol = 200;
  }

  uint8_t n = 0;
  long sum = 0;
  unsigned long start = millis();
  while ((millis() - start) < winMs) {
    int raw = analogRead(pin);
    if (raw >= LDR_GLITCH_FLOOR) {
      sum += raw;
      n++;
    }
    delay(sampleGapMs);
  }

  if (n < 5) {
    Serial.println("checkLightOnOff: not enough samples");
    return 1;
  }

  int avg = (int)(sum / n);
  Serial.print("checkLightOnOff: avg=");
  Serial.print(avg);
  Serial.print(" on=");
  Serial.print(p->onLevel);
  Serial.print(" off=");
  Serial.println(p->offLevel);

  if (ldrNearLevel(avg, p->onLevel, levelTol)) {
    return 2;
  }
  if (ldrNearLevel(avg, p->offLevel, levelTol)) {
    return 0;
  }

  int dOn = avg - p->onLevel;
  if (dOn < 0) dOn = -dOn;
  int dOff = avg - p->offLevel;
  if (dOff < 0) dOff = -dOff;
  if (dOn < dOff) {
    return 2;
  }
  if (dOff < dOn) {
    return 0;
  }
  return 1;
}

/**
 * เช็คด้วยโปรไฟล์ — คืน 0=off 1=blink 2=on หรือ -1 = ใช้ logic เดิม
 */
inline int checkLightWithProfile(const LdrLightProfile *p, int pin, int oldBoard) {
  if (!p || !p->valid || p->periodMs < 50) {
    return -1;
  }

  unsigned long sampleGapMs = p->periodMs / 4;
  if (sampleGapMs < 50) {
    sampleGapMs = 50;
  }
  if (sampleGapMs > 500) {
    sampleGapMs = 500;
  }

  unsigned long winMs = (unsigned long)p->periodMs * 3;
  if (winMs < 1500) {
    winMs = 1500;
  }
  if (winMs > 6000) {
    winMs = 6000;
  }

  int span = p->brightLevel - p->darkLevel;
  if (span < 0) {
    span = -span;
  }
  int darkTol = span / 4;
  if (darkTol < 150) {
    darkTol = 150;
  }
  int levelTol = span / 3;
  if (levelTol < 200) {
    levelTol = 200;
  }

  uint8_t darkHits = 0;
  uint8_t n = 0;
  long sum = 0;
  int winMin = 4095;
  int winMax = 0;

  unsigned long start = millis();
  while ((millis() - start) < winMs) {
    int raw = analogRead(pin);
    if (raw >= LDR_GLITCH_FLOOR) {
      if (n == 0) {
        winMin = winMax = raw;
      } else {
        if (raw < winMin) {
          winMin = raw;
        }
        if (raw > winMax) {
          winMax = raw;
        }
      }
      sum += raw;
      n++;
      if (ldrNearLevel(raw, p->darkLevel, darkTol)) {
        if (darkHits < 255) {
          darkHits++;
        }
      }
    }
    delay(sampleGapMs);
  }

  Serial.print("checkLightProfile: n=");
  Serial.print(n);
  Serial.print(" darkHits=");
  Serial.print(darkHits);
  Serial.print(" min=");
  Serial.print(n > 0 ? winMin : -1);
  Serial.print(" max=");
  Serial.print(n > 0 ? winMax : -1);
  Serial.print(" period=");
  Serial.println(p->periodMs);

  if (n < 3) {
    return 1; // ไม่พอ sample → ไม่ผ่าน
  }

  int avg = (int)(sum / n);

  // มียอดมืดตามโปรไฟล์กระพริบ ≥2 ครั้ง
  if (darkHits >= 2) {
    return 1;
  }

  if (p->hasOff && ldrNearLevel(avg, p->offLevel, levelTol)) {
    return 0;
  }
  if (p->hasOn && ldrNearLevel(avg, p->onLevel, levelTol)) {
    return 2;
  }

  // ไม่มี ON profile — ถ้าไม่เจอมืด และใกล้ bright = ON
  if (darkHits == 0 && ldrNearLevel(avg, p->brightLevel, levelTol)) {
    return 2;
  }

  // OldBoard polarity fallback จาก avg vs mid
  int mid = (p->brightLevel + p->darkLevel) / 2;
  if (oldBoard == 1) {
    if (avg > mid) {
      return 2;
    }
    if (avg < p->darkLevel + darkTol) {
      return 0;
    }
  } else {
    if (avg < mid) {
      return 2;
    }
    if (avg > p->darkLevel - darkTol) {
      return 0;
    }
  }
  return 1;
}

/**
 * จำแนก LDR จากโปรไฟล์ (Power / จบโปรแกรม step3)
 * คืน 2=เปิด/สว่าง, 0=ปิด/มืด, 1=เทา/ไม่ชัด, -1=ไม่มีโปรไฟล์ → ใช้ ldr_set
 */
inline int classifyPowerLdrSample(const LdrLightProfile *p, int val, int oldBoard) {
  if (!p) {
    return -1;
  }

  if (p->hasOn && p->hasOff) {
    int span = p->onLevel - p->offLevel;
    if (span < 0) {
      span = -span;
    }
    int levelTol = span / 3;
    if (levelTol < 200) {
      levelTol = 200;
    }
    if (ldrNearLevel(val, p->onLevel, levelTol)) {
      return 2;
    }
    if (ldrNearLevel(val, p->offLevel, levelTol)) {
      return 0;
    }
    int dOn = val - p->onLevel;
    if (dOn < 0) {
      dOn = -dOn;
    }
    int dOff = val - p->offLevel;
    if (dOff < 0) {
      dOff = -dOff;
    }
    if (dOn < dOff) {
      return 2;
    }
    if (dOff < dOn) {
      return 0;
    }
    return 1;
  }

  if (p->hasOn) {
    const int tol = 400;
    if (ldrNearLevel(val, p->onLevel, tol)) {
      return 2;
    }
    if (oldBoard == 1) {
      if (val < p->onLevel - tol) {
        return 0;
      }
    } else if (val > p->onLevel + tol) {
      return 0;
    }
    return 1;
  }

  if (p->valid && p->periodMs >= 50) {
    int span = p->brightLevel - p->darkLevel;
    if (span < 0) {
      span = -span;
    }
    int levelTol = span / 3;
    if (levelTol < 200) {
      levelTol = 200;
    }
    int darkTol = span / 4;
    if (darkTol < 150) {
      darkTol = 150;
    }
    if (ldrNearLevel(val, p->darkLevel, darkTol)) {
      return 0;
    }
    if (ldrNearLevel(val, p->brightLevel, levelTol)) {
      return 2;
    }
    int mid = (p->brightLevel + p->darkLevel) / 2;
    if (oldBoard == 1) {
      if (val > mid) {
        return 2;
      }
      if (val < p->darkLevel + darkTol) {
        return 0;
      }
    } else {
      if (val < mid) {
        return 2;
      }
      if (val > p->darkLevel - darkTol) {
        return 0;
      }
    }
    return 1;
  }

  return -1;
}
