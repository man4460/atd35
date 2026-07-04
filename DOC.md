# ATD35 Melody V2 — โครงสร้างและเอกสารอ้างอิง

โปรเจกต์ควบคุมเครื่องซักผ้า/อบผ้า ESP32-S3 + LVGL (SquareLine Studio), รองรับ WiFi / MQTT / OTA / QR Payment และการตั้งค่าผ่าน MQTT/HTTP

---

## 1. สถาปัตย์ Task (FreeRTOS)

| Task | Core | หน้าที่หลัก |
|------|------|--------------|
| **taskDisplay** | 0 | แตะ LVGL เท่านั้น: `applyPendingUI()`, `Display.loop()`, `count_update()`, `machineRuning()`, `prepareRunMachine()`, `checkLdr1/2`, `updateWiFiIcon()`, `countdownWait()`, `CheckPromotion()` |
| **taskProgram** | 0 | Logic เครื่อง: รีเลย์, โปรแกรมซัก, `modeSetting()` (ตั้งค่า UI ผ่าน pending), coin/chanel |
| **taskWifiMqtt** | 1 | WiFi/MQTT/HTTP: `otiUdate()`, `otaJobStep()`, `httpJobStep()`, GetData/GetSetupData, ส่งยอด `pendingBalance`, MQTT subscribe/publish, reconnect |

กฎสำคัญ: **มีเฉพาะ taskDisplay ที่เรียก LVGL โดยตรง** — task อื่นตั้งค่า state / pending แล้วให้ taskDisplay ไปอัปเดตจอ

---

## 2. Pending UI (ลดหลาย task แตะ LVGL)

- **ที่มา:** commandApp, otiUdate, checkQrpaymentRead/Gen, fwUpdate_OTI_POST ฯลฯ รันใน taskWifiMqtt — ห้ามเรียก `lv_*` โดยตรง
- **วิธีใช้:** ตั้ง `pendingUIAction`, `pendingLabel1`, `pendingLabel2`, `pendingProgram`, `pendingQrPayload` ฯลฯ แล้วให้ **taskDisplay** (หรือ `loop()` ตอน OTA) เรียก **`applyPendingUI()`** เป็นคนเดียวที่แตะ LVGL

### Enum Pending (ใน `varable.h`)

| ค่า | ความหมาย |
|-----|----------|
| `PENDING_UI_NONE` | ไม่มีงาน |
| `PENDING_UI_LABEL_MSG` | แสดงข้อความ ui_Label1, ui_Label2 |
| `PENDING_UI_FIRST_SCREEN` | เรียก addFlagstoFirstscreen() |
| `PENDING_UI_SHOW_RUN` | เปิดหน้าวิ่งโปรแกรม (screenUse=6, setStartMachine) |
| `PENDING_UI_SHUTDOWN` / `PENDING_UI_RESTART` | หน้า Shutdown/Restart |
| `PENDING_UI_SLOT_MSG` / `PENDING_UI_UPDATE_MSG` / `PENDING_UI_REBOOT_MSG` | ข้อความเปลี่ยนช่อง coin / กำลังอัพเดท / Reboot |
| `PENDING_UI_LDR_CLOSE` | คืนหน้าจอหลัง LdrClose |
| `PENDING_UI_BT1_HOME` / `PENDING_UI_BT4_SETTING` | ปุ่ม BT1 กลับหน้าแรก / BT4 เปิดเมนูตั้งค่า |
| `PENDING_UI_DISPLAY_SETTING` | อัปเดต ui_lb_display_setting |
| `PENDING_UI_QR_SUCCESS` / `PENDING_UI_QR_GEN` | QR ชำระสำเร็จ / สร้าง QR จาก payload |
| `PENDING_UI_SETTING_EXIT_COMMAND` | ออกจากหน้า setting กลับหน้าจอคำสั่ง (ui_con_command) |

---

## 3. OTA (State Machine แบบไม่ block)

- **จุดเริ่ม:** MQTT คำสั่ง `update` หรือเมนู setting โหมด "Mode update firmware" → ตั้ง `stateUpdateFw = true`, `UpdateFw = true`, ลบ taskDisplay/taskProgram แล้วให้ `loop()` แสดงข้อความ + `Display.loop()` ระหว่างอัพเดท
- **การทำงาน:** `otiUdate()` เปรียบเทียบเวอร์ชันจาก server กับ local ถ้าไม่เท่ากันเรียก `fwUpdate_OTI_POST("firmware.bin")` ซึ่งจะ **เริ่ม OTA job** (`otaJobStart`) ไม่ block
- **otaJobStep()** (เรียกใน taskWifiMqtt ทุกรอบ): เดิน state machine — CONNECT → SEND → WAIT_HEADERS → READ_HEADERS → BEGIN_UPDATE → STREAM (เขียนทีละ chunk) → FINISH → รีบูตเมื่อสำเร็จ
- **getVersionByPOST()** ยังใช้ `postDataToServer()` (blocking) อยู่ใน taskWifiMqtt; หน้าจอไม่ block เพราะ LVGL อยู่ใน loop() แยก

---

## 4. HTTP

- **GetData / sentDatatoAdmin:** ผ่าน **httpJobStart()** — ภายในใช้ HTTPClient แบบ sync (โหลด + parse ในครั้งเดียว) แล้วจบ; **httpJobStep()** เป็น no-op
- **GetSetupData, UpdateBalanceV3, sentVarjson/check:** ยังใช้ **HTTPClient** โดยตรงใน taskWifiMqtt (block ใน task นี้ ไม่กระทบจอ)
- **Server URLs:** Logic Apps (Azure) — `ServerGetdataV3`, `ServerSetupdataV3`, `ServerSentBalanceV3`; admin — `mawell.thddns.net:4740/completed`, `.../check`

---

## 5. ไฟล์หลัก

| ไฟล์ | บทบาท |
|------|--------|
| **src/main.cpp** | setup(), loop(), ทุก task, OTA/HTTP state machine, commandApp, applyPendingUI, ฟังก์ชันเครื่อง (รีเลย์, โปรแกรม, coin, QR ฯลฯ) |
| **src/varable.h** | ตัวแปร global, #define, PromoSlot, PENDING_UI_*, pendingBalance, state_wifi_on ฯลฯ (มี include guard แล้ว) |
| **src/main.h** | forward declarations ฟังก์ชัน (GetData, GetSetupData, commandApp, modeSetting ฯลฯ) |
| **src/var_setup.h** | template JSON สำหรับ MQTT setup (`const char* json`); ยังไม่มีที่ include ใช้ (มี include guard แล้ว) |
| **src/gui/ui.h** | LVGL UI (SquareLine), include ui_events.h, ui_helpers.h |
| **src/gui/ui_events.h** | stub จาก SquareLine (มี guard; ไม่มี event declarations) |

---

## 6. ตัวแปรสำคัญ (อ้างอิงจาก varable.h)

- **เครื่อง / เงิน:** `pendingBalance`, `item_price`, `price[]`, `pricePro[]`, `PriceShow[]`, `chanelPay`, `stateSentPriceServer`
- **โปรโมชั่น:** `PromoSlot`, `promoSlots[]`, `promoSlotCount`
- **สถานะเครื่อง:** `status_machine_run`, `status_machine_prepare`, `program`, `chanel`, `step`, `hrs`, `minn`, `second`
- **WiFi/MQTT/OTA:** `state_wifi_on`, `stateUpdateFw`, `UpdateFw`, `stateGetdata`, `stateSetupdata`, `topic` (อัปเดตใน main เมื่อ gid เปลี่ยน)
- **Pending UI:** `pendingUIAction`, `pendingLabel1`, `pendingLabel2`, `pendingProgram`, `pendingQrPayload`

---

## 7. การรอเวลา

- ใช้ **`vTaskDelay(... / portTICK_PERIOD_MS)`** ในทุก task แทน `delay()` เพื่อไม่ block task อื่น
- ไม่มี `delay()` ในเส้นทางที่รัน (เหลือแค่ใน comment)

---

## 8. หมายเหตุสำหรับการแก้โค้ด

- อย่าเรียก `lv_*` จาก taskProgram หรือ taskWifiMqtt — ใช้ pending UI แทน
- ถ้าเพิ่ม HTTP ใหม่ที่รันใน taskWifiMqtt สามารถใช้ HTTPClient ตรงๆ ได้ (หรือต่อคิวผ่าน httpJob ถ้าอยากรวมจุดเดียว)
- OTA ดาวน์โหลดผ่าน WiFiClient (port 80); HTTP ไป Azure ใช้ HTTPS ผ่าน HTTPClient/WiFiClientSecure

---

*เอกสารนี้สรุปจากโครงสร้างหลัง refactor (tasks, pending UI, OTA state machine, HTTP job, varable.h, ไฟล์ GUI/setup).*
