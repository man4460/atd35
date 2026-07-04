#pragma once
// Run session — กู้คืนรอบซัก/อบหลัง ESP reboot (NVS namespace แยกจาก config)
// ใช้คู่กับ ATD35_Melody_V3 — แก้ทั้งสองโปรเจกต์พร้อมกัน

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include "ldr_sampler.h"

#define RUN_SESSION_MAGIC     0x52554E31u
#define RUN_SESSION_NS        "runSession"
#define RECOVERY_GRACE_MS     10000u
#define CH_RECOVERY           99
#define RUN_SESSION_DRY_SAVE_MS 600000u  // Mode 2: autosave timer ทุก 10 นาที

enum RunSessionPhase : uint8_t {
  RS_IDLE = 0,
  RS_WASH_PREPARE = 1,
  RS_WASH_STARTUP = 2,
  RS_WASH_RUNNING = 3,
  RS_DRY_PREPARE = 4,
  RS_DRY_RUNNING = 5,
};

enum RunRecoveryAction : uint8_t {
  RUN_RECOVERY_NONE = 0,
  RUN_RECOVERY_CONTINUE,
  RUN_RECOVERY_RESUMED,
  RUN_RECOVERY_ABORTED,
};

struct RunSessionSnapshot {
  uint32_t magic;
  uint8_t active;
  uint8_t phase;
  uint8_t mode;
  uint8_t program;
  uint8_t step;
  uint8_t stepHier;
  uint8_t state_step2;
  uint8_t state_step3;
  uint8_t pause_timer;
  uint8_t drain_water;
  int16_t hrs;
  int16_t minn;
  int16_t second;
  uint16_t crc;
};

struct RunRecoveryResult {
  RunRecoveryAction action;
  RunSessionSnapshot snap;
};

// --- globals จาก varable.h / main ---
extern int Mode;
extern int program;
extern int step;
extern int stepHier;
extern int hrs;
extern int minn;
extern int second;
extern bool state_step2;
extern bool state_step3;
extern bool pause_timer;
extern bool drain_water;
extern bool status_machine_run;
extern bool status_machine_prepare;
extern int ldr_set;
extern int ldrMinus;
extern int ldrPin;

static bool g_runRecoveryPending = false;
static unsigned long g_runRecoveryStartMs = 0;
static RunSessionSnapshot g_runRecoverySnap = {};
static uint8_t g_runRecoveryLdrFails = 0;

struct RunSessionCheckpointKey {
  uint8_t phase;
  uint8_t step;
  uint8_t stepHier;
  uint8_t state_step2;
  uint8_t state_step3;
  uint8_t pause_timer;
  uint8_t drain_water;
};

static RunSessionCheckpointKey g_runSessionLastKey = {};
static bool g_runSessionHasLastKey = false;

static RunSessionCheckpointKey runSessionCheckpointKey(RunSessionPhase phase) {
  RunSessionCheckpointKey k = {};
  k.phase = static_cast<uint8_t>(phase);
  k.step = static_cast<uint8_t>(step);
  k.stepHier = static_cast<uint8_t>(stepHier);
  k.state_step2 = state_step2 ? 1 : 0;
  k.state_step3 = state_step3 ? 1 : 0;
  k.pause_timer = pause_timer ? 1 : 0;
  k.drain_water = drain_water ? 1 : 0;
  return k;
}

static bool runSessionKeyEquals(const RunSessionCheckpointKey &a,
                                const RunSessionCheckpointKey &b) {
  return memcmp(&a, &b, sizeof(a)) == 0;
}

static uint16_t runSessionCalcCrc(const RunSessionSnapshot *s) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(s);
  const size_t n = offsetof(RunSessionSnapshot, crc);
  uint16_t c = 0x5A5Au;
  for (size_t i = 0; i < n; i++) {
    c = static_cast<uint16_t>((c << 1) ^ p[i]);
  }
  return c;
}

inline void runSessionCaptureSnapshot(RunSessionSnapshot *s, RunSessionPhase phase) {
  memset(s, 0, sizeof(*s));
  s->magic = RUN_SESSION_MAGIC;
  s->active = 1;
  s->phase = static_cast<uint8_t>(phase);
  s->mode = static_cast<uint8_t>(Mode);
  s->program = static_cast<uint8_t>(program);
  s->step = static_cast<uint8_t>(step);
  s->stepHier = static_cast<uint8_t>(stepHier);
  s->state_step2 = state_step2 ? 1 : 0;
  s->state_step3 = state_step3 ? 1 : 0;
  s->pause_timer = pause_timer ? 1 : 0;
  s->drain_water = drain_water ? 1 : 0;
  s->hrs = static_cast<int16_t>(hrs);
  s->minn = static_cast<int16_t>(minn);
  s->second = static_cast<int16_t>(second);
  s->crc = runSessionCalcCrc(s);
}

inline void runSessionApplySnapshot(const RunSessionSnapshot *s) {
  program = s->program;
  step = s->step;
  stepHier = s->stepHier;
  state_step2 = s->state_step2 != 0;
  state_step3 = s->state_step3 != 0;
  pause_timer = s->pause_timer != 0;
  drain_water = s->drain_water != 0;
  hrs = s->hrs;
  minn = s->minn;
  second = s->second;
}

inline void runSessionSavePhase(RunSessionPhase phase, bool force = false) {
  if (phase == RS_IDLE) {
    return;
  }
  const RunSessionCheckpointKey key = runSessionCheckpointKey(phase);
  if (!force && g_runSessionHasLastKey && runSessionKeyEquals(key, g_runSessionLastKey)) {
    return;
  }
  g_runSessionLastKey = key;
  g_runSessionHasLastKey = true;
  RunSessionSnapshot s;
  runSessionCaptureSnapshot(&s, phase);
  Preferences prefs;
  if (!prefs.begin(RUN_SESSION_NS, false)) {
    return;
  }
  prefs.putBytes("snap", &s, sizeof(s));
  prefs.end();
  Serial.print(F("[RunSession] save phase="));
  Serial.println(static_cast<int>(phase));
}

inline void runSessionMaybeDryAutosave() {
  static unsigned long lastDrySaveMs = 0;
  if (Mode != 2 || !status_machine_run) {
    lastDrySaveMs = 0;
    return;
  }
  const unsigned long now = millis();
  if (lastDrySaveMs != 0 && (now - lastDrySaveMs) < RUN_SESSION_DRY_SAVE_MS) {
    return;
  }
  lastDrySaveMs = now;
  runSessionSavePhase(RS_DRY_RUNNING, true);
}

inline void runSessionClear() {
  Preferences prefs;
  if (prefs.begin(RUN_SESSION_NS, false)) {
    prefs.clear();
    prefs.end();
  }
  g_runRecoveryPending = false;
  g_runRecoveryLdrFails = 0;
  g_runSessionHasLastKey = false;
  memset(&g_runSessionLastKey, 0, sizeof(g_runSessionLastKey));
  Serial.println(F("[RunSession] cleared"));
}

inline bool runSessionLoadSnapshot(RunSessionSnapshot *out) {
  Preferences prefs;
  if (!prefs.begin(RUN_SESSION_NS, true)) {
    return false;
  }
  size_t len = prefs.getBytesLength("snap");
  if (len != sizeof(RunSessionSnapshot)) {
    prefs.end();
    return false;
  }
  prefs.getBytes("snap", out, sizeof(*out));
  prefs.end();
  if (out->magic != RUN_SESSION_MAGIC || !out->active) {
    return false;
  }
  const uint16_t expect = runSessionCalcCrc(out);
  if (out->crc != expect) {
    Serial.println(F("[RunSession] CRC mismatch"));
    return false;
  }
  if (out->phase == RS_IDLE || out->phase > RS_DRY_RUNNING) {
    return false;
  }
  return true;
}

inline bool runSessionLdrBrightSample() {
  const int val = readLDRAverage(static_cast<uint8_t>(ldrPin), LDR_AVG_SAMPLES, "runSession LDR");
  if (Mode == 1) {
    if (OldBoard == 1) {
      return val > ldr_set;
    }
    return val <= ldr_set;
  }
  if (OldBoard == 1) {
    return val > ldr_set;
  }
  return val <= ldr_set;
}

inline bool runSessionLdrMachineRunning() {
  int bright = 0;
  for (int i = 0; i < 3; i++) {
    if (runSessionLdrBrightSample()) {
      bright++;
    }
    if (i < 2) {
      delay(LDR_READ_INTERVAL_MS);
    }
  }
  return bright >= 2;
}

inline bool runSessionBeginRecovery(int stateShutdown) {
  if (stateShutdown == 1) {
    runSessionClear();
    return false;
  }
  RunSessionSnapshot s;
  if (!runSessionLoadSnapshot(&s)) {
    return false;
  }
  if (s.phase == RS_DRY_PREPARE && s.minn <= 0 && s.hrs <= 0) {
    runSessionClear();
    return false;
  }
  g_runRecoverySnap = s;
  g_runRecoveryPending = true;
  g_runRecoveryStartMs = millis();
  g_runRecoveryLdrFails = 0;
  Serial.print(F("[RunSession] recovery pending mode="));
  Serial.print(s.mode);
  Serial.print(F(" phase="));
  Serial.println(s.phase);
  return true;
}

inline bool runSessionIsRecoveryPending() {
  return g_runRecoveryPending;
}

inline RunRecoveryResult runSessionTickRecovery() {
  RunRecoveryResult r = {};
  r.action = RUN_RECOVERY_NONE;
  if (!g_runRecoveryPending) {
    return r;
  }

  if (millis() - g_runRecoveryStartMs < RECOVERY_GRACE_MS) {
    r.action = RUN_RECOVERY_CONTINUE;
    return r;
  }

  const RunSessionSnapshot &s = g_runRecoverySnap;
  r.snap = s;

  if (s.mode == 2) {
    g_runRecoveryPending = false;
    r.action = RUN_RECOVERY_RESUMED;
    Serial.println(F("[RunSession] DRY resume after grace"));
    return r;
  }

  if (!runSessionLdrMachineRunning()) {
    g_runRecoveryLdrFails++;
    const esp_reset_reason_t rr = esp_reset_reason();
    const bool powerLoss =
        rr == ESP_RST_POWERON || rr == ESP_RST_BROWNOUT || rr == ESP_RST_SDIO;
    if (powerLoss || g_runRecoveryLdrFails >= 5) {
      runSessionClear();
      r.action = RUN_RECOVERY_ABORTED;
      Serial.println(F("[RunSession] WASH abort — LDR dark"));
      return r;
    }
    r.action = RUN_RECOVERY_CONTINUE;
    return r;
  }

  g_runRecoveryPending = false;
  r.action = RUN_RECOVERY_RESUMED;
  Serial.println(F("[RunSession] WASH resume — LDR bright"));
  return r;
}

