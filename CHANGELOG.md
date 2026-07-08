# ESP Firmware Changelog — ATD35_Melody_V3 (ESP32-S3)

รูปแบบเวอร์ชัน: `Version X.YY` ใน `varable.h` → `fwversion[1]` (OTA / presence MQTT)  
**ใช้เลขเวอร์ชันเดียวกับ ATD_TM_V3_New_Hier** (บอร์ด TM1637 vs ESP32-S3 แยกกันที่ `userID` / โฟลเดอร์ OTA เช่น `ai_new` vs `ai_touch`)

---

## Version 3.97 (2026-07-08) — กัน setRelayType ทับเวลาโปรแกรมเป็น 31

### TimeCountdown sync

- หลัง `setRelayType()` เรียก `applyMelodyProgramDurations(timerDry…)` อีกครั้งใน `commitMelodyPreferencesToNvs` / HTTP getdata / factory defaults
- ซัก P1–P3 ใช้เวลาตาม Melody (`timedry`/`duration`) ไม่ถูกทับกลับเป็น `0:31`
- ไม่แก้ logic โปรแกรม 4/5/6
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อน **3.96**

---

## Version 3.96 (2026-07-08) — แก้บันทึก config จาก Melody (setup/getdata)

### GetSetupData / boot sync

- setup จาก V{gid} ตอน boot → debounce คู่ configResponse (ไม่ชน flush ทับ)
- `flushBootMelodySyncToNvs` ข้ามเมื่อ buffer ว่าง (กัน reset โรงงานทับค่าที่บันทึกแล้ว)
- `GetSetupData` ไม่เรียก `applyFactoryDefaultsConfig` เมื่อไม่มี payload
- pump MQTT ก่อนประมวลผล setup; เพิ่ม boot grace clear `firstGetdata` (sync TM)
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อน **3.95**

---

## Version 3.95 (2026-07-08) — fault 01 จอ TM แสดง -01- (sync TM)

### machineRuning() — ซักค้าง 0:01

- ATD35 มีข้อความ fault บนจออยู่แล้ว — bump เวอร์ชันคู่ TM
- ไฟล์: `src/varable.h`

### Rollback

- ย้อน **3.94**

---

## Version 3.94 (2026-07-08) — เวลาโปรแกรม Melody → ซัก P1–P3 (sync TM)

### sync duration1–3 (timedry1–3) → TimeCountdown1–3

- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อน **3.93**

---

## Version 3.93 (2026-07-08) — boot MQTT sync บันทึก NVS รอบเดียว (sync TM)

### configResponse + setPromoSlots หลัง debounce

- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อน **3.92**

---

## Version 3.92 (2026-07-07) — OTA โฟลเดอร์ v4 WSS

### แยก OTA folder สำหรับ Melody v4 (MQTT_USE_WEBSOCKET)

- `userID = wss_touch` → `fw/wss_touch/` (แยกจาก TM classic ใน wss_new)
- TCP เก่า ยังใช้ `ai_touch`
- ไฟล์: `src/varable.h`

### Rollback

- ย้อน **3.91** หรือตั้ง `MQTT_USE_WEBSOCKET 0`

---

## Version 3.91 (2026-07-07) — Melody protocol v4 (mv:4 + WSS)

### Melody v4 — MQTT over WebSocket (sync กับ TM)

- `MELODY_PROTOCOL_VERSION 4` + presence `mv:4`, `transport:wss` เมื่อ `MQTT_USE_WEBSOCKET`
- `MqttWsWifiClient` adapter + `links2004/WebSockets` — pump `_ws.loop()` ใน read/available
- `varable.h` จัด DEPLOY CONFIG ด้านบน; `userID = ai_touch` (OTA S3)
- ไฟล์: `src/main.cpp`, `src/varable.h`, `src/mqtt_ws_client.h`, `platformio.ini`

### Rollback

- ตั้ง `MQTT_USE_WEBSOCKET 0`, `mv:3` หรือย้อน **3.87**

---

## Version 3.87 (2026-07-07)

### MQTT Uptime + log UpdateState (sync กับ TM)

- ส่ง Uptime คู่ UpdateState + log publish สำเร็จ
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 3.86**

---

## Version 3.86 (2026-07-07)

### MQTT reconnect เร็วขึ้น — 5-5-5-5 ก่อนหมุนพอร์ต (sync กับ TM)

- หลุด edge → reconnect ทันที; connect fail → retry 5s คงที่ ×4 แล้วหมุนพอร์ต
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 3.85**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเวอร์ชันเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp`, `src/varable.h`

---

## Version 3.85 (2026-07-07)

### Serial log วินิจฉัย MQTT loop + หลุด (uptime + RTC) — sync กับ TM

- **`mqttPumpLoopLocked()`** — log ทุก 30s: `tag`, `rounds`, `total` + uptime + RTC
- **`logMqttDropped(reason)`** — log ตอนหลุดพร้อม `rc`, WiFi, RSSI, `failStreak`
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 3.84**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเวอร์ชันเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp`, `src/varable.h`

---

## Version 3.84 (2026-07-07)

### กลับไปโมเดลเน็ต v3.78 (ตอบสนองดี) + เก็บเฉพาะ HTTP revenue (sync กับ TM)

- **ทำไม:** v3.80–3.83 (keepAlive 90s + ย้าย `mqclient.loop()` เข้า `loop()` single-thread + no-op mutex) ทำให้สั่งการ MQTT ไม่ตอบสนอง — ยืนยันว่า v3.78 ตอบสนองดีกว่า
- **ทำอะไร:** revert WiFi/MQTT servicing กลับเป็น **v3.78 เป๊ะ** (`taskWifiMqtt` เดิม, `gNetMutex` recursive, keepAlive 60s) + เติมกลับเฉพาะ **HTTP revenue** จาก v3.79
- **ตัดออกจาก v3.79:** `netLockTryEnter()` helper + pump `mqclient.loop()` ต้นรอบ `taskWifiMqtt` → cadence keepalive เท่า v3.78
- **เก็บไว้:** `sendRevenueHttp()` + `txnId` idempotent, persist NVS `revenue`, `revenueRestore()` boot, throttle 4s, endpoint `POST /public/machines/device-revenue`
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป **v3.78** = `4c0b028` หรือ **v3.79** (มี pump/tryEnter) = `c4cecc4`

---

## Version 3.79 (2026-07-06)

### รายรับส่งทาง HTTP (idempotent) แทน MQTT postSQL + กัน MQTT flap ทำ loop() ขาด (sync กับ TM)

- **อาการ:** เครื่องออนไลน์/ออฟไลน์ตลอด — MQTT `dropped rc=-4` วนซ้ำ (RSSI ดี, connect สำเร็จ = ping timeout) → รายรับผ่าน MQTT `postSQL` (QoS0) เสี่ยงหาย
- **A — รายรับผ่าน HTTP:**
  - Backend (ร่วมกับ TM): `POST /public/machines/device-revenue` — reuse `recordDeviceRevenue()` (dedup เดิม) + idempotency key `txnId` (`description="txn:<id>"`)
  - Firmware: รายรับส่ง HTTP เป็นหลัก (`sendRevenueHttp`) buffer+retry จน 2xx + `txnId` persistent + persist NVS namespace `revenue` (กู้หลัง reboot) — เลิกใช้ MQTT postSQL/UpdateBalanceV3 สำหรับรายรับ
- **B — กัน flap แย่ลง (ไม่แตะ keepAlive/socketTimeout/port):**
  - throttle ส่งรายรับ HTTP ทุก 4s + pump `mqclient.loop()` ต้นรอบด้วย `netLockTryEnter(30ms)` รับประกัน cadence keepalive
- ไฟล์: `src/main.cpp`, `src/varable.h`
- **ต้อง deploy backend คู่กัน** (endpoint ใหม่)

### Rollback

> **บันทึกสำคัญ:** ถ้า v3.79 ใช้งานไม่ดี **ย้อนกลับไป v3.78 ได้ทันที** — ทุก repo push v3.78 ไว้เป็นจุดย้อนกลับแล้ว

- ย้อนไป: **Version 3.78**
- Commit อ้างอิง:

  | Repo | v3.79 (ปัจจุบัน) | v3.78 (จุดย้อนกลับ) |
  |---|---|---|
  | ATD35 | `c4cecc4` | `4c0b028` |
  | ATD_TM | `ff3ed1e` | `f34a4f2` |
  | MelodyWebapp (backend) | `1d7c1f1` | `34b6f21` |

- วิธีย้อน (ต่อ repo): `git revert <v3.79 commit>` หรือ `git checkout 4c0b028 -- src/main.cpp src/varable.h` แล้ว build/OTA เวอร์ชัน 3.78
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน (endpoint backend เป็น additive — revert firmware อย่างเดียวก็ได้ backend เก่ายังรับ MQTT postSQL)
- หมายเหตุ: NVS namespace `revenue` ไม่ต้องล้าง; firmware 3.78 กลับไปส่งรายรับทาง MQTT postSQL เหมือนเดิม

---

## Version 3.78 (2026-07-05)

### sync version กับ TM (ไม่มีการแก้โค้ด ATD35)

- TM: Mode 3/4 hier อ่าน LDR แบบ blocking ให้จังหวะเปลี่ยนรีเลย์สม่ำเสมอ
- ATD35 ไม่มี SetFirstHier — bump เวอร์ชันให้ตรงกันเท่านั้น

### Rollback

- ย้อนไป: **Version 3.77**

---

## Version 3.77 (2026-07-05)

### sync version กับ TM (ไม่มีการแก้โค้ด ATD35)

- TM: Mode 3/4 hier เดินเร็วขึ้น (SetFirstHier actionGap 500→100, LDR 10→4 ค่า)
- ATD35 ไม่มี SetFirstHier — bump เวอร์ชันให้ตรงกันเท่านั้น

### Rollback

- ย้อนไป: **Version 3.76**

---

## Version 3.76 (2026-07-04)

### แก้ crash `pbuf_free: p->ref > 0` (recv ซ้อนข้าม task) + คืน keepAlive 60 (sync TM)

- **สาเหตุ:** `updateWiFiIcon()` (taskDisplay/LVGL) เรียก `mqclient.connected()` → `recv()` พร้อมกับ `loop()` ใน taskWifiMqtt = อ่าน socket เดียวกัน 2 task → pbuf double-free → รีบูต
- **แก้:** cache `volatile bool g_mqttOnline` (อัปเดตใน taskWifiMqtt); จอ + LVGL config อ่าน cache แทน + คืน `setKeepAlive(60)`
- ไฟล์: `src/main.cpp`

### Rollback

- ย้อนไป: **Version 3.74**

---

## Version 3.75 (2026-07-04)

### ทดลอง keepAlive 60→15 (แก้ MQTT หลุด rc=-4 ตอน idle, sync TM)

- `setKeepAlive(15)` — ping ทุก 15s กัน broker/NAT ปิด TCP ตอน idle (rc=-4)
- อยู่ระหว่างทดสอบหน้างาน; ถ้าไม่ดีขึ้นย้อนกลับ 60
- ไฟล์: `src/main.cpp`

### Rollback

- ย้อนไป: **Version 3.74** (keepAlive 60)

---

## Version 3.74 (2026-07-04)

### 5 นาทีสุดท้าย ส่ง UpdateState ทุก 1 นาที (sync TM)

- เมื่อ `hrs==0 && minn<=5` → ส่งทุก 1 นาที (จากเดิมทุก 5 นาที) ให้เวลา ESP↔server ตรงกันสุด
- ไฟล์: `src/main.cpp` (`machineRuning`)

### Rollback

- ย้อนไป: **Version 3.73**

---

## Version 3.73 (2026-07-04)

### log RunSession save แสดงเวลาที่บันทึก (sync TM)

- `[RunSession] save phase=X time=H:M:S` — เพิ่มเวลาจาก snapshot จริง
- ไฟล์: `src/run_session.h`

### Rollback

- ย้อนไป: **Version 3.72**

---

## Version 3.72 (2026-07-04)

### แก้ MQTT หลุดซ้ำทั้งที่ WiFi ยังต่อ (socket timeout 6→15 = ตรง 3.00) + log สาเหตุ (sync TM)

- `setSocketTimeout(15)` + `client.setTimeout(15000)` — 6s ตัด socket เร็วไปตอน WiFi jitter → หลุดทั้งที่ยังต่อ (3.00 ใช้ default 15s)
- เพิ่ม log `[MQTT] dropped rc=.. wifi=.. rssi=..` ตอน connected→disconnected เพื่อยืนยันสาเหตุหน้างาน

### Rollback

- ย้อนไป: **Version 3.71**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.71 (2026-07-04)

### MQTT reconnect — backoff เบา 5→15s + เปลี่ยนพอร์ตหลัง fail 4 ครั้ง (sync TM)

- อยู่พอร์ตเดิม fail ครบ 4 ครั้งค่อยหมุนพอร์ต; backoff 5→15s (cap 15s)
- คงไว้: keepalive 60s, single-close (3.69)

### Rollback

- ย้อนไป: **Version 3.70**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.70 (2026-07-04)

### MQTT reconnect กลับเป็น 3.00-style (sync TM)

- `mqttreconnect()` retry คงที่ **5s** + หมุน port ทุก fail — ตัด exponential backoff (5→60s) ที่ทำให้ต่อกลับช้า
- คงไว้: keepalive 60s, single-close (3.69)

### Rollback

- ย้อนไป: **Version 3.69**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.69 (2026-07-04)

### แก้ crash lwIP `pbuf_free: p->ref > 0` (double-close socket) — sync TM

- ปิด socket ครั้งเดียวใน `mqttreconnect()`, `teardownMqttOnWifiDown()`, `pauseMqttForOta()`: `if (connected) mqclient.disconnect(); else client.stop();`
- กัน pbuf refcount เพี้ยน -> รีบูตกลางงาน

### Rollback

- ย้อนไป: **Version 3.68**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.68 (2026-07-04)

### กู้รอบงานเฉพาะไฟดับเท่านั้น (sync TM)

- `runSessionBeginRecovery()` กู้เฉพาะ `ESP_RST_POWERON` / `ESP_RST_BROWNOUT`; software/watchdog/crash/OTA → ล้าง snapshot ไม่กู้
- ไฟล์: `src/run_session.h`

### Rollback

- ย้อนไป: **Version 3.67**
- ไฟล์: `src/run_session.h`, `src/varable.h`

---

## Version 3.67 (2026-07-04)

### watchdog เช็คห่างขึ้น 10 วิ (sync TM)

- `loop()` เรียก `checkTaskHang()` ทุก **10 วิ** (เดิม 1 วิ)
- ตอนทำงาน/เตรียม ไม่รีบูทเอง (3.66)

### Rollback

- ย้อนไป: **Version 3.66**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.66 (2026-07-04)

### กันรีบูทเองระหว่างทำงาน (sync TM — เทียบ 3.00)

- `checkTaskHang()` ข้ามการรีบูทเมื่อ `status_machine_run || status_machine_prepare` + feed heartbeat — ตอนรันไม่รีบูทเอง, idle ยังกู้ได้
- คงไว้: boot grace, low-heap guard, taskWifiMqtt core 1

### Rollback

- ย้อนไป: **Version 3.65**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.65 (2026-07-04)

### Sync TM — bump คู่ (TM คืนส่วนโปรแกรม/จอ ตาม 3.32)

- ATD35 ไม่มี logic จอ TM1637 — bump เวอร์ชันคู่ให้ตรง

### Rollback

- ย้อนไป: **Version 3.64**
- ไฟล์: `src/varable.h`

---

## Version 3.64 (2026-07-04)

### Sync TM — bump คู่ (จอ TM คืนดีไซน์ 3.51)

- ATD35 ไม่มี logic จอ TM1637 — bump เวอร์ชันคู่ให้ตรง

### Rollback

- ย้อนไป: **Version 3.63**
- ไฟล์: `src/varable.h`

---

## Version 3.63 (2026-07-04)

### Sync TM — bump คู่ (fix จอ TM กระพริบสลับ standby)

- ATD35 ไม่มี logic จอ TM1637 — bump เวอร์ชันคู่ให้ตรง

### Rollback

- ย้อนไป: **Version 3.62**
- ไฟล์: `src/varable.h`

---

## Version 3.62 (2026-07-04)

### Sync TM — bump คู่ (fix จอ TM1637 regression 3.61)

- ATD35 ไม่มี logic จอ TM1637 — bump เวอร์ชันคู่ให้ตรง

### Rollback

- ย้อนไป: **Version 3.61**
- ไฟล์: `src/varable.h`

---

## Version 3.61 (2026-07-04)

### Sync TM — ออกจาก setting ตอน start program + จอ timer อบ

### Rollback

- ย้อนไป: **Version 3.60**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.60 (2026-07-04)

### Sync TM — taskWifiMqtt core 1 / TWDT fix หลัง OTA

### Rollback

- ย้อนไป: **Version 3.59**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.59 (2026-07-04)

### OTA — แก้ "Written only : 16/…" (sync TM)

- `suspendMachineTasksForOta()` + `pauseMqttForOta()` หลัง OtaStatus start + `otaWriteStreamWithRetry()`

### MQTT — backoff หลังครบ 4 port + timeout 8s (sync TM)

### Rollback

- ย้อนไป: **Version 3.58**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.58 (2026-07-04)

### Sync — MQTT/WiFi stability v3.58 (warmup/hysteresis/port rotate)

- Bump เวอร์ชันคู่ ATD_TM — logic เดียวกันใน `main.cpp`

### Rollback

- ย้อนไป: **Version 3.57**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.57 (2026-07-04)

### Sync — TM classic เท่านั้น, S3 อยู่ ATD35

- Bump เวอร์ชันคู่ ATD_TM (ลบ esp32s3_tm / ai_new_s3 ฝั่ง TM1637)

### Rollback

- ย้อนไป: **Version 3.56**
- ไฟล์: `src/varable.h`

---

## Version 3.56 (2026-07-04)

### Sync — บอร์ดใหม่ TM รองรับ ESP32 classic + S3 แยก env

- Bump เวอร์ชันคู่ ATD_TM (default `esp32dev`; S3 ใช้ `esp32s3_tm`)

### Rollback

- ย้อนไป: **Version 3.55**
- ไฟล์: `src/varable.h`

---

## Version 3.55 (2026-07-04)

### Sync — บอร์ดใหม่ TM บังคับ OTA ai_new_s3

- Bump เวอร์ชันคู่ ATD_TM (OldBoard 0 = S3 + ai_new_s3)

### Rollback

- ย้อนไป: **Version 3.54**
- ไฟล์: `src/varable.h`

---

## Version 3.54 (2026-07-04)

### Sync เวอร์ชัน — OTA ai_new_s3 สำหรับ TM1637 บน ESP32-S3 (ฝั่ง ATD_TM)

- Bump เวอร์ชันคู่ ATD_TM; โฟลเดอร์ OTA `ai_new_s3` + chip validation บน Melody backend
- ATD35 ยังใช้ `ai_touch` เหมือนเดิม

### Rollback

- ย้อนไป: **Version 3.53**
- ไฟล์: `src/varable.h`

---

## Version 3.53 (2026-07-04)

### Task-hang WDT — boot grace + กัน millis underflow false trigger

- **`hangElapsedMs()`** + **`BOOT_HANG_GRACE_MS` 60s** — งดเช็ค hang หลัง boot (WiFi/MQTT ต่อได้ก่อน)
- ต้องมี `taskWifiMqtt_handle` ก่อนเช็ค wifi hang; log hb ตอน restart เพื่อ debug
- Sync เวอร์ชันกับ ATD_TM (รวม env `esp32s3_tm` ฝั่ง TM)

### Rollback

- ย้อนไป: **Version 3.52**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.52 (2026-07-04)

### Sync เวอร์ชันกับ ATD_TM — แก้จอ TM1637 ค้าง 0000 (เฉพาะบอร์ด TM)

- การแก้ `primeBootStandbyDisplay` / handoff หลัง boot อยู่ที่ **ATD_TM_V3_New_Hier** (`main.cpp`) — ATD35 ใช้ LVGL ไม่ได้รับ diff นี้
- Bump `fwversion[1]` คู่กันเพื่อ OTA ร่วม

### Rollback

- ย้อนไป: **Version 3.51**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเวอร์ชันเดียวกัน
- ไฟล์ ATD35: `src/varable.h` (เวอร์ชันอย่างเดียว)

---

## Version 3.51 (2026-07-03)

### กู้รอบเครื่องซัก — เข้า step ที่บันทึก โชว์ timer อยู่ chanel 0

- state machine เครื่องซัก: countdown จริงอยู่ที่ `case 0`/chanel 0 กับ step 1/2/3 (chanel อื่นเป็น action ชั่วคราวที่วนกลับ case 0)
- RESUMED handler สำหรับ wash: เข้า chanel 0 ที่ step เดิมจาก snapshot → `PENDING_UI_RESUME_RUN` โชว์หน้า run + timer จาก snapshot
- **กันค้าง startup:** ถ้า reboot ช่วง `RS_WASH_STARTUP` (step ยังเป็น 0) จะกู้ที่ `chanel = 1` เพื่อรัน Power→Start ใหม่จน step ถูกตั้ง (กันค้างที่ chanel 0/step 0)
- อบ (Mode 2) คง `chanel = 0` เหมือนเดิม

### Rollback

- ย้อนไป: **Version 3.50**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเวอร์ชันเดียวกัน
- ไฟล์: `src/main.cpp` (handleRunSessionRecoveryChannel), `src/varable.h`

---

## Version 3.50 (2026-07-03)

### WiFi/MQTT — กัน pbuf_free crash ตอน presence + UpdateState ชนกัน

- **`teardownMqttOnWifiDown()`** — เมื่อ WiFi หลุด ตัด MQTT/TCP ทันที
- **`UpdateState`** — คิวไป `processDeferredMqttWork` (publish+pump ครั้งเดียวต่อรอบ)
- **`wifiLinkUsable()`** ก่อนส่ง MQTT status/config
- **`postSQL`** — ลด pump เหลือ 1 รอบ

### Task-hang watchdog — รีบูทกู้ตัวเองเมื่อ task ค้าง

- heartbeat ต้นลูปทุก task; `loop()` เฝ้า: display/program > 2 นาที, wifi > 6 นาที (เผื่อ OTA) → `ESP.restart()`
- **low-heap guard:** free heap < 10 KB ต่อเนื่อง 1 นาที → `ESP.restart()`
- ข้ามระหว่าง OTA (`otaInProgress`); กู้รอบซัก/อบต่อด้วย run_session

### กู้รอบหลังรีบูท — โชว์ timer ไม่ใช่ standby

- เพิ่ม `PENDING_UI_RESUME_RUN` — recovery resume โชว์หน้า run (conS6) โดยไม่ `setStartMachine` (คง timer จาก snapshot)

### Build fix — ให้คอมไพล์ผ่าน

- เพิ่ม forward declaration: `tryLoadIdentityFromEeprom()`, `wifiLinkUsable()`
- `varable.h`: `#define OldBoard 0` (บอร์ดใหม่) — `run_session.h` ใช้เลือก logic LDR

### Rollback

- ย้อนไป: **Version 3.49**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเวอร์ชันเดียวกัน
- ไฟล์: `src/main.cpp`, `src/varable.h`, `STABILITY_NOTES.md`

---

## Version 3.49 (2026-06-29)

### WiFi boot — non-blocking + `WiFi.disconnect(true)` หลัง fail

- ลบ `WiFi_ini()` จาก setup; `connectwifi()` ใน `taskWifiMqtt` เท่านั้น
- กัน stack ค้างหลัง connect timeout (TM + ATD35)

### Rollback

- ย้อนไป: **Version 3.48**
- ไฟล์: `src/main.cpp`, `src/main.h`, `src/varable.h`

---

## Version 3.48 (2026-06-29)

### Mode 2 (อบ) — นาทีส่วนเกินเมื่อหยอดครั้งแรกเกินราคาแพ็กสูงสุด

- แพ็กสูงสุด + ยอดเกิน × อัตราต่อเวลา (10 บาท = 10 นาที ตาม `DRY_EXTEND_MIN_PER_COIN`)
- `setStartMachine(dryFirstPaymentBaht)` — TM + ATD35 logic เดียวกัน

### Rollback

- ย้อนไป: **Version 3.47**
- ไฟล์: `src/main.cpp`, `src/varable.h`

---

## Version 3.47 (2026-06-29)

### Boot — ย้าย ID จาก EEPROM ไป NVS ครั้งแรก (อัปเกรดจาก firmware เก่า)

- ถ้า NVS ยังไม่มี `Noserial` → อ่านจาก EEPROM (layout เดิม addr 70/82/106/138) ก่อน
- พบค่าใน EEPROM → ใช้ `Noserial`, `ssid`, `password`, `gid` แล้วบันทึก NVS
- ไม่พบ → ใช้ค่า default จาก `varable.h` แล้วบันทึก NVS
- ฟังก์ชัน: `tryLoadIdentityFromEeprom()` ใน `main.cpp` (TM + ATD35)

### Rollback

- ย้อนไป: **Version 3.46**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเวอร์ชันเดียวกัน
- ไฟล์: `src/main.cpp`, `src/varable.h`
- NVS: ไม่ต้องล้าง

---

## Version 3.46 (2026-06-27)

### Run session — Mode 2 autosave timer ทุก 10 นาที

- `runSessionMaybeDryAutosave()` — โหมดอบบันทึก NVS ทุก 10 นาที
- โหมดซัก: checkpoint เท่านั้น (v3.45)

### Rollback

- ย้อนไป: **Version 3.45**
- ไฟล์: `src/run_session.h`, `src/main.cpp`, `src/varable.h`

---

## Version 3.45 (2026-06-27)

### Run session — NVS เขียนเฉพาะเมื่อ step/phase เปลี่ยน

- ลบ autosave ทุก 2 นาที — dedupe checkpoint ใน `runSessionSavePhase`
- save ตอนเปลี่ยน step ใน `taskProgram` (drain/rin/spin, case 8/9)
- อบ: timer ใน NVS อัปเดตตอนเริ่มอบเท่านั้น

### Rollback

- ย้อนไป: **Version 3.44**
- ไฟล์: `src/run_session.h`, `src/main.cpp`, `src/varable.h`

---

## Version 3.44 (2026-06-27)

### MQTT — ลด pbuf_free crash หลัง presence online

- รวม deferred MQTT publish + pump ครั้งเดียวต่อรอบ `taskWifiMqtt`
- ลบ `mqclient.loop()` ท้าย task
- Run session autosave NVS 30s → 120s

### Rollback

- ย้อนไป: **Version 3.43**
- ไฟล์: `src/main.cpp`, `src/run_session.h`, `src/varable.h`

---

## Version 3.43 (2026-06-27)

### Run session — grace หลัง reboot 10 วิ (เดิม 5 วิ)

- `RECOVERY_GRACE_MS` 5000 → **10000** ใน `src/run_session.h`

### Rollback

- ย้อนไป: **Version 3.42**
- ไฟล์: `src/run_session.h`, `src/varable.h` (`fwversion` → 3.42)
- NVS: ไม่ต้องล้าง

---

## Version 3.42 (2026-06-27)

### Run session — กู้คืนรอบซัก/อบหลัง ESP reboot

- **`src/run_session.h` (ใหม่):** บันทึกสถานะรอบลง NVS namespace `runSession`
- Grace **5 วิ** ก่อนตรวจ LDR / สั่ง relay
- **เครื่องซัก:** LDR สว่าง → resume step + timer (ไม่ Power/Start ซ้ำ)
- **เครื่องอบ:** ไม่อ่าน LDR — กู้ timer + **`Dry(1)`** ค้าง relay (มีอยู่แล้วใน loop)
- `chanel=99` recovery ใน `taskProgram`

### Rollback

- ย้อนไป: **Version 3.41**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์: `src/run_session.h` (ลบ), `src/main.cpp`, `src/varable.h`
- NVS: ลบ `runSession` ได้ถ้าต้องการ

---

## Version 3.41 (2026-06-22)

### แก้ WiFi reconnect ค้าง warmup — MQTT ไม่กลับมาหลัง WiFi ต่อใหม่

- เพิ่ม `noteWifiLinkUp()` ตั้ง `wifiConnectedSinceMs` เมื่อ WiFi กลับมา `WL_CONNECTED`
- **`taskWifiMqtt`:** ถ้า `WiFi.isConnected()` แต่ `wifiConnectedSinceMs == 0` ให้เริ่ม warmup ทันที (เดิมเรียก `connectwifi()` เฉพาะตอน WiFi หลุด)
- **`connectwifi()`:** ตั้ง warmup เมื่อต่อสำเร็จแม้ state ไม่ใช่ `WIFI_CONNECTING`
- log warmup แสดงเวลาที่เหลือ (ms)

### Rollback

- ย้อนไป: **Version 3.40**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไป **3.40** พร้อมกัน
- ไฟล์: `src/main.cpp` (`noteWifiLinkUp`, `connectwifi`, `taskWifiMqtt`), `src/varable.h` (`fwversion` → 3.40)
- NVS: ไม่ต้องล้าง

---

## Version 3.40 (2026-06-20)

### LDR — อ่านเสถียรขึ้นเมื่อไฟไม่สม่ำเสมอ (เครื่องทำงานปกติ)

- **`LdrAvgSampler` / `readLDRAverage`:** ใช้ **median** แทนค่าเฉลี่ย + ตัด sample ต่ำกว่า 35 (glitch `avg=0`)
- **Mode 1 หลัง Power (case 2):** เพิ่ม **`LdrPeakWindow`** — ผ่านถ้า `peak` หรือค่าปัจจุบันข้าม `ldr_set` (จับไฟกระพริบ; บอร์ด touch: `val <= ldr_set || peak <= ldr_set`)
- ช่วงอ่าน LDR 700 ms, sample 10 ครั้ง

---

## Version 3.39 (2026-06-20)

### แก้ pbuf_free crash ตอนสั่งโปรแกรม + LDR (หลัง v3.38)

- **ห้าม `mqttPumpLoopLocked()` ใน MQTT callback** — defer `publishPresenceOnline` / `mqttDiag` หลัง reconnect ไป `processDeferredMqttWork()` ใน `taskWifiMqtt`
- **`gNetMutex`** — ห้าม HTTP กับ MQTT พร้อมกัน (ครอบ `mqclient.loop/publish`, HTTPClient, reconnect)
- **`sentVarjson()`** — ห่อ HTTP ด้วย `netLockEnter()`
- **`subscribe(configResponse/...)`** — ใช้ buffer ถาวร แทน temporary `String`

---

## Version 3.38 (2026-06-20)

### แก้ boot ค้าง warmup หลัง WiFi ต่อครั้งแรก

- แก้เส้นทาง `WiFi_ini()` ให้ตั้ง `wifiConnectedSinceMs` และ reset `wifiReconnectBackoffMs`
- เดิม `wifiLinkUsable()` ถูกปลดล็อกเฉพาะตอน reconnect ผ่าน `connectwifi()` ทำให้การต่อ WiFi ครั้งแรกหลัง boot ค้าง log `connected but warming up`
- หลังแก้แล้ว boot path และ reconnect path ใช้หลัก `WiFi stable window` เหมือนกัน

---

## Version 3.37 (2026-06-20)

### WiFi reconnect / pending balance - ลดโอกาสรีบูทเอง

- เพิ่ม `wifiLinkUsable()` รอให้ WiFi ต่อค้างอย่างน้อย **4 วินาที** ก่อนเริ่ม MQTT / HTTP
- เพิ่ม **backoff** ตอน `connectwifi()` fail: 1s -> 2s -> 4s ... สูงสุด 30s
- เพิ่ม **backoff** ตอน `mqttreconnect()` fail: 5s -> 10s -> 20s ... สูงสุด 60s
- ตัดการส่ง `pendingBalance` แบบ **HTTP + MQTT พร้อมกัน**: ถ้า MQTT ออนไลน์ส่ง MQTT อย่างเดียว, ถ้า MQTT ล่มจริงค่อย fallback เป็น HTTP
- ระหว่าง WiFi เพิ่งกลับมา จะยังไม่เรียก `mqclient.loop()`, `pollMelodyDeviceHttp()`, `sendUpdateStateHttp()`

---

## Version 3.34 (2026-06-18)

### OTA / MQTT — แก้ค้าง "กำลังอัพเดท 0%" และ HTTP fallback

(ชุดเดียวกับ ATD_TM_V3_New_Hier v3.34)

- **MQTT fail ก่อน HTTP fallback:** `MQTT_FAIL_STREAK_FALLBACK` 5 → **20** ครั้ง
- **`sendOtaStatusMqtt`:** เพิ่ม `ensureMqttForOtaStatus()` — reconnect MQTT หลายรอบหลัง `pauseMqttForOta`
- **OTA เวอร์ชันตรงกัน:** ส่ง `OtaStatus failed` + `"เวอร์ชันตรงกัน"`

### อ้างอิง MelodyWebapp backend (ต้อง deploy คู่กัน)

- HTTP OTA เปิด/ปิดจากแอดมิน — ปิดแล้วเปิดได้เฉพาะแอดมิน
- ไม่ตั้ง updating ตอน HTTP ack; timeout updating ค้าง 15 นาที
- ส่ง OTA ใน HTTP poll เมื่อ `fail_count >= 20` และ `httpOtaEnabled=true`

---

## Version 3.32 (2026-06-14)

### LDR — อ่านค่าเสถียร ไม่ block MQTT / LVGL

- เพิ่ม `src/ldr_sampler.h` (ชุดเดียวกับ ATD_TM_V3_New_Hier)
  - `setupLdrAdc(LDR1_PIN, LDR2_PIN)`
  - `LdrAvgSampler` — non-blocking
  - `readLDRAverage()` — blocking สั้น
- ใช้ sampler ใน:
  - `checkLightStart()`
  - `checkLdr1()` / `checkLdr2()` (แก้บั๊ก `checkLdr2` ใช้ `stateCheckLdr2`)
  - `taskProgram` case 2 — ตรวจ power
  - `taskProgram` step 3 — ตรวจ ldr จบโปรแกรม
  - หน้าตั้งค่า LDR บน LVGL
- ขา LDR: `LDR1_PIN=1`, `LDR2_PIN=2` (ESP32-S3)

### อ้างอิง Melody backend

- รหัสข้อผิดพลาด `00/01/02` → `maintenance` + label ไทยบนแดชบอร์ด

---

## Version 9.99 (ก่อนหน้า — ค่าพัฒนา)

- Melody MQTT, OTA, LVGL UI
- HTTP fallback: mqtt-report, device-ack, update-state

---

## วิธีอ่าน log ตอน boot

Serial Monitor:

```
[FW] Current Firmware
[FW] Version 3.34
```
