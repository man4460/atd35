# Stability Notes — ATD35_Melody_V3

อัปเดตล่าสุด: `Version 3.58`

## สิ่งที่ harden แล้ว

- **`gNetMutex` (recursive):** ห้าม HTTP กับ MQTT ทับซ้อน — ครอบ `mqclient.loop/publish`, HTTPClient, reconnect
- **`processDeferredMqttWork()`:** defer `presence online` / `mqttDiag recovered` หลัง reconnect (ไม่ publish ทันทีหลัง subscribe)
- **`mqttPumpLoopLocked()`:** flush packet หลัง publish (`UpdateState`, `postSQL`, OTA status, ฯลฯ)
- **`wifiLinkUsable()`** warmup **2s** (standby) / **1s** (เครื่องทำงาน) — v3.58 ลดจาก 4s
- **v3.58:** WiFi down hysteresis 2.5s; MQTT port หมุนเฉพาะ connect fail; backoff cap 15s ตอนรันโปรแกรม
- เพิ่ม reconnect backoff (WiFi / MQTT)
- ตอนส่ง `pendingBalance`: MQTT ออนไลน์ → `postSQL` อย่างเดียว | MQTT ล่มจริง → HTTP fallback
- **v3.49:** WiFi ทั้งหมดใน `taskWifiMqtt` — ไม่บล็อก setup; `WiFi.disconnect(true)` หลัง connect fail
- **v3.50:** WiFi หลุด → `teardownMqttOnWifiDown()`; `UpdateState` คิวไป `processDeferredMqttWork`
- **v3.50 task-hang watchdog:** `loop()` เฝ้า heartbeat ทุก task — display/program >2 นาที หรือ wifi >6 นาที → `ESP.restart()` (ข้ามช่วง OTA)
- **v3.50 low-heap guard:** free heap < 10 KB ต่อเนื่อง 1 นาที → รีบูท
- แก้ reconnect path (v3.41): `taskWifiMqtt` + `noteWifiLinkUp()` ตั้ง warmup เมื่อ WiFi กลับมา
- **LDR v3.40:** median + glitch filter + `LdrPeakWindow` หลัง Power (Mode 1)

## อาการที่ตั้งใจลด

- `assert failed: pbuf_free` ใน lwIP ตอน MQTT loop ซ้อน HTTP หรือ nested loop ใน callback
- publish MQTT บน socket ค้างหลัง WiFi หลุดชั่วคราว
- รีบูทตอน WiFi เพิ่ง reconnect แล้ว MQTT/HTTP เริ่มทำงานเร็วเกินไป
- LDR อ่าน `avg=0` หรือพลาดไฟกระพริบหลัง Power

## จุดที่ยังควรจับตา

- `checkQrpaymentRead()` ยัง publish `respondMc` จาก MQTT callback (ไม่มี nested loop แต่ควรเฝ้าดู)
- หลัง v3.41: ถ้าเห็น log `warming up` ค้างนานเกิน ~4s หลัง WiFi ต่อ ให้เก็บ Serial
- **v3.44 MQTT:** deferred publish + pump ครั้งเดียว — ลด `pbuf_free` crash หลัง presence
- **Run session (v3.42+):** grace 10s; **v3.45:** NVS เฉพาะเมื่อ step/phase เปลี่ยน; **v3.46:** โหมดอบ autosave timer ทุก 10 นาที
- ถ้า LVGL / OTA ทำงานพร้อม network หนัก ๆ ควรจับ heap และ watchdog เพิ่ม

## วิธีทดสอบภาคสนาม

1. เปิดเครื่องจนต่อ WiFi และ MQTT สำเร็จ
2. เริ่มงาน แล้วปิด router ชั่วคราว 10-20 วินาที
3. เปิด router กลับ ดูว่าเครื่องไม่รีบูทและ reconnect เอง
4. ทดสอบช่วงมี `pendingBalance` ค้าง + สั่งโปรแกรมหลัง `checkLightStart`
5. ทดสอบ Mode 1 หลัง Power — ไฟเครื่องกระพริบควรผ่าน case 2 ได้
6. **v3.42:** เริ่มรอบซัก/อบ → `ESP.restart()` หรือไฟดับสั้น ๆ → หลัง boot ดู `[RunSession] DRY resume` / `WASH resume`
