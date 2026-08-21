# ESP Firmware Changelog — ATD35_Melody_V3 (ESP32-S3)

รูปแบบเวอร์ชัน: `Version X.YY` ใน `varable.h` → `fwversion[1]` (OTA / presence MQTT)  
**ใช้เลขเวอร์ชันเดียวกับ ATD_TM_V3_New_Hier** (บอร์ด TM1637 vs ESP32-S3 แยกกันที่ `userID` / โฟลเดอร์ OTA เช่น `ai_new` vs `ai_touch`)

---

## Version 4.30 (2026-08-21) — สลับแหล่ง LDR: โปรไฟล์ที่เรียน vs ldr_set

### เลือกแหล่งตรวจไฟ

- ซิงก์ ATD_TM: NVS `lpUse` + MQTT `LdrUseLearn` / `LdrUseDefault`
- ปิดโปรไฟล์แล้วใช้ `ldr_set` ได้โดยไม่ลบค่าที่เรียน
- `LdrLearnStatus` ส่ง `lpUse`

### Rollback

- ย้อนไป: **Version 4.29**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp`, `src/varable.h`

---

## Version 4.29 (2026-08-21) — Step3 จบรอบใช้โปรไฟล์ LDR ที่เรียน

### Step 3

- ซิงก์ ATD_TM: จบเมื่อมืดตามโปรไฟล์ค้าง ≥3 วิ; ไม่มืด = ยังทำงาน
- fault 01 คงเดิม

### Rollback

- ย้อนไป: **Version 4.28**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.28 (2026-08-21) — Power check อ่าน LDR แบบ median

### Power check

- ใช้ `readLDRAverage()` แทน sample เดียว — ซิงก์ ATD_TM
- Serial: `avg=`

### Rollback

- ย้อนไป: **Version 4.27**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.27 (2026-08-21) — Power check ใช้โปรไฟล์ LDR ที่เรียน

### Power check (`chanel` 2)

- ถ้ามีโปรไฟล์เรียนแล้ว → `classifyPowerLdrSample()` (Mode 1 และ 6)
- ไม่มีโปรไฟล์: Mode 1 ใช้ `ldr_set`; Mode 6 ข้ามเช็ค
- ซิงก์ logic กับ ATD_TM

### Rollback

- ย้อนไป: **Version 4.26**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.26 (2026-08-21) — sync เวอร์ชัน (TM แก้ Ln/MQ)

### Sync

- bump ให้ตรง **ATD_TM** v4.26 (แก้ปุ่ม Ln/MQ บน TM1637)
- ATD35 Another menu จับคู่ Mode2 9/10 กับ ldrMinus/mqttStatus อยู่แล้ว — ไม่แก้ logic

### Rollback

- ย้อนไป: **Version 4.25**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.25 (2026-08-21) — Mode 6 เรียนรู้เปิด/ปิด LDR

### Mode 6

- ใช้ `hasOn` + `hasOff` ผ่าน `checkLightOnOffProfile`
- ไฟล์: `src/ldr_sampler.h`, `src/main.cpp`

### Rollback

- ย้อนไป: **Version 4.24**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.24 (2026-08-21) — LDR remote max:min + learn result

### LdrOpen / Learn

- stream ส่ง `ldrMax`/`ldrMin` ทุก 1 วิ
- learn ตอบ `ldrLearnResult` + ข้อความบนจอ

### Rollback

- ย้อนไป: **Version 4.23**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.23 (2026-08-21) — LPStatus ตอบ MQTT

### LdrLearnStatus / LPStatus

- publish `ldrProfileStatus` → `commandBack`
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.22**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.22 (2026-08-21) — LDR ทางไกลส่งค่าทุก 1000ms

### LdrOpen / LdrOpen2

- publish `ldrSample` → `commandBack` ทุก **1000ms** จน `LdrClose`
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.21**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.21 (2026-08-21) — MQTT เรียนรู้โปรไฟล์ LDR

### cmCommand

- `LdrLearnBlink`/`LP1`, `LdrLearnOn`/`LP2`, `LdrLearnOff`/`LP3`, `LdrLearnStatus`/`LPStatus`
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.20**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.20 (2026-08-21) — Mode 1 โปรไฟล์ LDR (คาบกระพริบ)

### checkLightStart Mode 1

- LP1/LP2/LP3 เรียนรู้ + NVS; valid แล้วใช้คาบกระพริบในเช็ค 02
- ไฟล์: `src/ldr_sampler.h`, `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.19**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.19 (2026-07-26) — CM4 เกณฑ์กระพริบปรับได้โดยแอดมิน

### cm4_blink_th

- default **1000**; แอดมินปรับได้ (เมนู / NVS / MQTT)
- เครื่อง noise สูงแนะนำ **2000**
- ไฟล์: `src/varable.h`, `src/main.cpp`, `src/run_session.h`

### Rollback

- ย้อนไป: **Version 4.18**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.18 (2026-07-26) — CM4 จบรอบต้อง LDR >3500

### step==3 end LDR CodeMachine 4

- ไฟดับจบรอบ: **>3500** ค้าง 3 วิ
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.17**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.17 (2026-07-26) — CM4 เช็ค 02 อ่าน 5 หน้าต่าง

### checkLightStart CodeMachine 4

- `winCount = 5` (~10 วิ) ก่อนสรุปผล
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.16**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.16 (2026-07-26) — CM4 EMPTY ถือเป็น ON

### checkLightStart CodeMachine 4

- EMPTY → นับเป็น ON
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.15**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.15 (2026-07-26) — CM4 เช็ค 02 แยกด้วยยอด ≥1000 @500ms

### checkLightStart CodeMachine 4 (fault 02)

- อ่านทุก **500ms**, หน้าต่าง **2s × 3**; ยอด **≥1000** → BLINK
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.14**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.14 (2026-07-26) — โหมดทดสอบ LDR อ่านทุก 1300ms

### checkLdr1

- ช่วงอ่านทดสอบ: **1300ms**
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.13**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.13 (2026-07-26) — โหมดทดสอบ LDR อ่านทุก 900ms

### checkLdr1

- ช่วงอ่านทดสอบ: **900ms**
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.12**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.12 (2026-07-26) — โหมดทดสอบ LDR แสดงค่าดิบทันที

### checkLdr1

- อ่านแล้วพิมพ์ทันที — ไม่เฉลี่ย / ไม่จัด ON-BLINK
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.11**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.11 (2026-07-26) — CM4 ทดลองอ่าน LDR ทุก 700ms

### checkLightStart + checkLdr1 CodeMachine 4

- อ่านทุก **700ms**, หน้าต่าง **3s × 3** (~9 วิ)
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.10**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.10 (2026-07-26) — CM4 แยกไฟนิ่ง / กระพริบใหม่

### checkLightStart + checkLdr1 CodeMachine 4

- ยอด≥900; BLINK เมื่อ highN≥3 หรือ (highN≥2 และ max≥1000); เลิก highN≥1+span
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.09**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.09 (2026-07-26) — CM4 รอไฟนิ่งหลัง Start ก่อนอ่าน LDR

### fault 02 CodeMachine 4

- หลัง Start รอ 4 วิ ก่อนเช็ค; ยืนยันรอบ 2 รอ 3 วิ เช็คซ้ำโดยไม่กด Start ซ้ำ
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.08**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.08 (2026-07-26) — ท้ายรอบพึ่ง LDR มืดจริง (เลิกจบนาที 5 ของ CM3/4)

### 5 นาทีสุดท้าย / ค้าง 0:01

- ลบบังคับจบนาที 5 ของ CM3/4; 15→01 / 20→รีเซ็ต; LDR end มืด≥3500 ค้าง 3 วิ (Mode1/6/CM3/4)
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.07**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.07 (2026-07-26) — CM4 จับกระพริบเพดานต่ำ + ยืนยัน ON 2 รอบ

### fault 02 CodeMachine 4

- `blinkHighTh`→700, `span`→500; ต้อง ON ติดกัน 2 ครั้งก่อนไปซัก; แก้ Mode 1 ที่เคยเรียก checkLightStart ซ้ำ
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.06**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.06 (2026-07-26) — CM4 เช็ค 02 ยืดเวลา 3×1.5 วิ

### checkLightStart CodeMachine 4

- **3×1.5 วิ (~4.5 วิ)**; EMPTY ไม่นับเป็น BLINK ปลอม; ON ต้องครบทุกหน้าต่าง
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.05**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.05 (2026-07-26) — CM4 BLINK เมื่อ highN พอ ไม่บังคับ span

### checkLightStart / checkLdr1 CodeMachine 4

- BLINK ถ้า `highN ≥ 2` หรือ (`highN ≥ 1` และ `span ≥ 700`) — กันเคส highN สูงแต่ span แคบ
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.04**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 4.04 (2026-07-26) — CodeMachine 4 เช็ค 02 ด้วยแอมปลิจูด LDR

### checkLightStart — เฉพาะ CodeMachine 4

- 2 หน้าต่าง × 1 วิ แยก ON/BLINK/OFF; มี BLINK → ไม่ผ่าน (สุดท้าย 02); ON ทั้งคู่ถึงไปต่อ
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.03**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp` (`checkLightStart`), `src/varable.h`

---

## Version 4.03 (2026-07-26) — checkLdr1 อ่านถี่จับไฟกระพริบ

### โหมดทดสอบ LDR1

- ช่วงอ่าน 900 ms → **80 ms**; ใช้ `readLDRInstant` แทน sampler
- เฉพาะ `checkLdr1()` — ไม่แตะ `checkLdr2`
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.02**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp` (`checkLdr1`), `src/varable.h`

---

## Version 4.02 (2026-07-26) — Mode 1 power check ตามเกณฑ์ &lt;3000 / &gt;3500

### fault 00 — หลักการเรียบ ไม่ใช้ trough

- `val < ldr_set` → ไปต่อ; `val > ldr_set+500` → Power ซ้ำทุก 5 วิ; ครบ 5 → **00**
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.01**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp` (case 1–2 Mode 1), `src/varable.h`

---

## Version 4.01 (2026-07-26) — checkLightStart กันกระพริบหลุดเป็น Light On

### Mode 1 fault 02 — A+B+C

- เทาในหน้าต่าง 3 วิ / กระพริบซ้ำ (On≥2) → Unstable; มืดใช้ `ldrMinus/2`; On เฉพาะสว่างนิ่งครั้งเดียวไม่เทา
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 4.00**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp` (`checkLightStart`), `src/varable.h`

---

## Version 4.00 (2026-07-26) — Mode 1 power check ใช้ trough

### fault 00 — จับไฟกระพริบถูกขั้ว (สว่าง=ค่าต่ำ)

- เพิ่ม `trough()` ใน `LdrPeakWindow`; ผ่านเมื่อ `val` หรือ `trough` ≤ `ldr_set` (ไม่ใช้ peak)
- ไฟล์: `src/ldr_sampler.h`, `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 3.99**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/ldr_sampler.h`, `src/main.cpp`, `src/varable.h`

---

## Version 3.99 (2026-07-26) — sync เวอร์ชันกับ TM checkLightStart OldBoard 1

### Version sync

- TM: รวมหลักการ On/Off/Unstable ทั้ง OldBoard 0/1 (polarity กลับกัน)
- ATD35: logic `checkLightStart` เหมือน v3.98 แล้ว (ขั้วเดียวกับ OldBoard 0) — bump เลขให้คู่กัน
- ไฟล์: `src/varable.h`

### Rollback

- ย้อนไป: **Version 3.98**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน

---

## Version 3.98 (2026-07-26) — checkLightStart ไม่ถือโซนเทาเป็น Light On

### Mode 1 start — ไฟกระพริบต้อง retry แล้ว 02

- **สาเหตุ:** timeout 3 วิ ถือค่ากลางเป็น On → ไปต่อทั้งที่ไฟกระพริบ
- **แก้:** On เฉพาะ `<= ldr_set`; Off เมื่อ `> ldr_set+ldrMinus`; โซนเทา → `Light Unstable` คืน 1
- ไฟล์: `src/main.cpp`, `src/varable.h`

### Rollback

- ย้อนไป: **Version 3.97**
- โปรเจกต์คู่: ย้อน **ATD_TM** และ **ATD35** ไปเลขเดียวกัน
- ไฟล์ที่ต้องคืน: `src/main.cpp` (`checkLightStart` timeout), `src/varable.h`

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
