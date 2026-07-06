#include <main.h>
#include <Arduino.h>
#include <lvgl.h>
#include <ATD3.5-S3.h>
#include "gui/ui.h"
#include <ESP32Time.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "varable.h"
#include "ldr_sampler.h"
#include "run_session.h"
#include <EEPROM.h>
#include <Update.h>

// ปุ่มคืนค่าโรงงานตอนเปิดเครื่อง (กดก่อนอ่าน Preferences) — ใช้ BOOT บน ESP32-S3
#ifndef FACTORY_RESTORE_BTN_PIN
#define FACTORY_RESTORE_BTN_PIN 0
#endif
// #include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>

static TaskHandle_t taskProgram_handle = NULL;
static TaskHandle_t taskWifiMqtt_handle = NULL;
static TaskHandle_t taskDisplay_handle = NULL;

// --- Task-hang watchdog: แต่ละ task อัปเดต heartbeat; loop() รีบูทถ้า task ค้างนานเกิน ---
static volatile unsigned long hbDisplayMs = 0;
static volatile unsigned long hbProgramMs = 0;
static volatile unsigned long hbWifiMs = 0;
static volatile bool otaInProgress = false;  // true ระหว่างดาวน์โหลด OTA (task wifi บล็อกได้นาน)
static const unsigned long TASK_HANG_TIMEOUT_MS = 120000UL;       // display/program ค้าง > 2 นาที = รีบูท
static const unsigned long WIFI_TASK_HANG_TIMEOUT_MS = 360000UL;  // wifi/mqtt ค้าง > 6 นาที (เผื่อ OTA)
static const unsigned long BOOT_HANG_GRACE_MS = 60000UL;          // งดเช็ค hang 60s หลัง boot (WiFi connect)
static const uint32_t LOW_HEAP_CRITICAL_BYTES = 10000UL;          // internal heap ต่ำกว่านี้ = ใกล้หมด (S3 + LVGL)
static const unsigned long LOW_HEAP_REBOOT_MS = 60000UL;          // ต่ำต่อเนื่อง 1 นาที = รีบูทกันค้าง/crash

/** กัน false positive เมื่อ now < since (millis skew / race ตอน boot) */
static bool hangElapsedMs(unsigned long now, unsigned long since, unsigned long limitMs)
{
  if (since == 0 || now < since)
    return false;
  return (unsigned long)(now - since) > limitMs;
}

//Object
WiFiClient client;
PubSubClient mqclient(client);
/** ล็อก lwIP — ห้าม HTTP กับ MQTT พร้อมกัน (กัน assert pbuf_free บน ESP32) */
static SemaphoreHandle_t gNetMutex = nullptr;

static bool netLockEnter()
{
  if (!gNetMutex)
    return true;
  return xSemaphoreTakeRecursive(gNetMutex, pdMS_TO_TICKS(12000)) == pdTRUE;
}

/** จับ net lock แบบไม่บล็อกนาน — ใช้กับ MQTT pump ต้นรอบ เพื่อไม่ให้ taskWifiMqtt ค้างรอ lock */
static bool netLockTryEnter(uint32_t ms)
{
  if (!gNetMutex)
    return true;
  return xSemaphoreTakeRecursive(gNetMutex, pdMS_TO_TICKS(ms)) == pdTRUE;
}

static void netLockLeave()
{
  if (gNetMutex)
    xSemaphoreGiveRecursive(gNetMutex);
}

/** เรียกเมื่อถือ net lock อยู่แล้ว */
static void mqttPumpLoopLocked(int rounds = 3)
{
  if (!mqclient.connected())
    return;
  for (int i = 0; i < rounds; i++)
  {
    mqclient.loop();
    vTaskDelay(1);
  }
}

// สถานะ MQTT online แบบ cache — อัปเดตเฉพาะใน taskWifiMqtt
// task จอ (LVGL) อ่านค่านี้แทน mqclient.connected() ตรง ๆ กัน recv() ซ้อน loop() -> pbuf double-free crash
volatile bool g_mqttOnline = false;

Preferences preferences;
WiFiClientSecure httpClient;  // สำหรับ HTTP state machine (Logic Apps / admin APIs)

ESP32Time rtc(0);  // offset in seconds GMT
struct tm timeinfo;

void taskWifiMqtt(void *parameter);  // forward declaration (defined later)
void setPriceShow();                 // forward declaration (defined later)
static bool wifiLinkUsable();        // forward declaration (defined later)
void setupWaitAdminRestoreFactory(); // กดปุ่ม BOOT ก่อนอ่าน Preferences = คืนค่าโรงงาน

/** แจ้ง Melody รหัสปัญหา 00/01/02 — ส่งผ่าน topic UpdateState (หรือ HTTP fallback) */
static void reportEspFaultToMelody(const char *code) {
  runSessionClear();
  StatusControl = code;
  stateUpdateState = 1;
  Serial.print(F("[Melody] fault -> UpdateState Status="));
  Serial.println(code);
}
void PublishConfigViaMqtt();         // ส่ง config ปัจจุบันไป topic getdataResponse (เหมือน ATD_TM_V2_New_Hier)
/** คืนค่ารหัสที่แปลงจาก Noserial สำหรับแสดงบนจอ (reversible ด้วย secret เดียวกับ backend เพื่อค้นหา id ได้) */
String getDisplayCodeFromNoserial();
/** ส่งสถานะ OTA ไป MQTT topic OtaStatus (phase, percent, message) — กำหนดไว้หลังในไฟล์ */
void sendOtaStatusMqtt(const char* phase, int percent, const char* message);
void normalizeOtaServer();
void suspendMachineTasksForOta();
void restoreMachineTasksAfterOta();
static bool tryLoadIdentityFromEeprom();  // forward declaration (defined later)
void taskDisplay(void *parameter);
void taskProgram(void *parameter);
static void revenuePersist();
static void revenueRestore();
/** MQTT presence/LWT — topic presence/{Noserial} สำหรับ MelodyWebapp */
bool publishPresenceOnline();
void publishPresenceOfflineGraceful();

// HTTP state machine (non-blocking inside taskWifiMqtt)
enum HttpJobType {
  HTTP_JOB_NONE = 0,
  HTTP_JOB_GETDATA,
  HTTP_JOB_GETSETUP,
  HTTP_JOB_BALANCE,
  HTTP_JOB_SENT_ADMIN,
  HTTP_JOB_CHECK
};

enum HttpState {
  HTTP_IDLE = 0,
  HTTP_CONNECT,
  HTTP_SEND,
  HTTP_WAIT,
  HTTP_READ_STATUS,
  HTTP_READ_BODY,
  HTTP_DONE,
  HTTP_ERROR
};

struct HttpJob {
  HttpJobType type = HTTP_JOB_NONE;
  HttpState state = HTTP_IDLE;
  String host;
  uint16_t port = 443;
  String path;
  String body;
  String contentType;
  unsigned long timeoutAt = 0;
  int responseCode = 0;
  String responseBody;
};

static HttpJob httpJob;
static void httpJobStart(HttpJobType type, const String &url, const String &body, const String &contentType);
static void httpJobStep();

// Implementation: ทำแบบ synchronous ใน Start เพื่อให้ GetData/sentDatatoAdmin ทำงานได้ (state machine เต็มรูปแบบทำภายหลังได้)
static void httpJobStart(HttpJobType type, const String &url, const String &body, const String &contentType) {
  if (httpJob.state != HTTP_IDLE) return;
  if (!wifiLinkUsable()) {
    Serial.println(F("[HTTP] skip sync request because WiFi is not stable yet"));
    return;
  }
  if (!netLockEnter()) return;
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", contentType);
  int code = http.POST(body);
  if (type == HTTP_JOB_SENT_ADMIN) {
    Serial.print(code);
    http.end();
    netLockLeave();
    return;
  }
  if (type == HTTP_JOB_GETDATA) {
    Serial.print("HTTP Getdata " + Noserial + " code : ");
    Serial.println(code);
    if (code == 200) {
      String payload = http.getString();
      const size_t capacity = JSON_OBJECT_SIZE(3) + 256;
      DynamicJsonDocument doc(capacity);
      DeserializationError error = deserializeJson(doc, payload);
      if (!error && doc["id"].as<String>() == Noserial && doc["cm"].as<String>() == "getdata") {
        JsonObject v = doc["value_str2"];
        IDserver = v["ID"].as<String>();
        gid = v["gid"].as<int>();
        Mode = v["ModeSystem"].as<int>();
        mqttStatus = v["mqttStatus"].as<int>();
        CodeMachine = v["CodeMachine"].as<int>();
        price[0] = v["Price1"].as<int>(); price[1] = v["Price2"].as<int>(); price[2] = v["Price3"].as<int>();
        pricePro[0] = v["PricePro1"].as<int>(); pricePro[1] = v["PricePro2"].as<int>(); pricePro[2] = v["PricePro3"].as<int>();
        timerDry[0] = v["timedry1"].as<int>(); timerDry[1] = v["timedry2"].as<int>(); timerDry[2] = v["timedry3"].as<int>();
        setRelayType();
        writePreferences();
      } else if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
      } else {
        Serial.println("ERR : Can't get data from server : GETDATA");
      }
    }
    http.end();
    netLockLeave();
    return;
  }
  http.end();
  netLockLeave();
}

static void httpJobStep() {
  (void)0; // no-op; งานทำใน httpJobStart แบบ sync
}

//relay
void IO_init() {
  Wire.beginTransmission(IO_ADDR);
  Wire.write(0x06); // Config Port0, 1 to output
  Wire.write(0x00);
  Wire.write(0x00);
  Wire.endTransmission();
}
void IO_write(uint16_t value) {
  Wire.beginTransmission(IO_ADDR);
  Wire.write(0x02);
  Wire.write(value & 0xFF);
  Wire.write((value >> 8) & 0xFF);
  Wire.endTransmission();
}
void IO_digitalWrite(int pin, int value) {
  static uint16_t old_value = 0;
  bitWrite(old_value, pin, value);
  IO_write(old_value);
}
void Power(){
  IO_digitalWrite(0, HIGH);//On
  vTaskDelay(1500 / portTICK_PERIOD_MS);
  IO_digitalWrite(0, LOW);//Off
  vTaskDelay(500 / portTICK_PERIOD_MS);
}
void Start(){
  IO_digitalWrite(1, HIGH);//On
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  IO_digitalWrite(1, LOW);//Off
  vTaskDelay(500 / portTICK_PERIOD_MS);
}
void Temp(){
  IO_digitalWrite(4, HIGH);//On
  vTaskDelay(700 / portTICK_PERIOD_MS);
  IO_digitalWrite(4, LOW);//Off
  vTaskDelay(500 / portTICK_PERIOD_MS);
}
void TempSpin()
{
  IO_digitalWrite(4, HIGH);//On
  vTaskDelay(5000 / portTICK_PERIOD_MS);
  IO_digitalWrite(4, LOW);//Off
  vTaskDelay(500 / portTICK_PERIOD_MS);
}
static bool state_jok = false;
void Jok(){
  if ((Mode == 1 && CodeMachine == 4) || Mode == 3)
  {
    if (!state_jok)
    {
      IO_digitalWrite(2, HIGH);//On
      vTaskDelay(400 / portTICK_PERIOD_MS);
      IO_digitalWrite(3, HIGH);//On
      vTaskDelay(500 / portTICK_PERIOD_MS);
      state_jok = true;
    }
    else
    {
      IO_digitalWrite(2, LOW);//Off
      vTaskDelay(400 / portTICK_PERIOD_MS);
      IO_digitalWrite(3, LOW);//Off
      vTaskDelay(500 / portTICK_PERIOD_MS);
      state_jok = false;
    }
  }
  else
  {
    IO_digitalWrite(2, HIGH);//On
    vTaskDelay(400 / portTICK_PERIOD_MS);
    IO_digitalWrite(3, HIGH);//On
    vTaskDelay(500 / portTICK_PERIOD_MS);
    IO_digitalWrite(2, LOW);//Off
    vTaskDelay(400 / portTICK_PERIOD_MS);
    IO_digitalWrite(3, LOW);//Off
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}
void JokBack(){
  if ((Mode == 1 && CodeMachine == 4) || Mode == 3)
  {
    if (!state_jok)
    {
      IO_digitalWrite(3, HIGH);//On
      vTaskDelay(400 / portTICK_PERIOD_MS);
      IO_digitalWrite(2, HIGH);//On
      vTaskDelay(500 / portTICK_PERIOD_MS);
      state_jok = true;
    }
    else
    {
      IO_digitalWrite(3, LOW);//Off
      vTaskDelay(400 / portTICK_PERIOD_MS);
      IO_digitalWrite(2, LOW);//Off
      vTaskDelay(500 / portTICK_PERIOD_MS);
      state_jok = false;
    }
  }
  else
  {
    IO_digitalWrite(3, HIGH);//On
    vTaskDelay(400 / portTICK_PERIOD_MS);
    IO_digitalWrite(2, HIGH);//On
    vTaskDelay(500 / portTICK_PERIOD_MS);
    IO_digitalWrite(3, LOW);//Off
    vTaskDelay(400 / portTICK_PERIOD_MS);
    IO_digitalWrite(2, LOW);//Off
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}
void Spin(){
  IO_digitalWrite(5, HIGH);//On
  vTaskDelay(700 / portTICK_PERIOD_MS);
  IO_digitalWrite(5, LOW);//Off
  vTaskDelay(500 / portTICK_PERIOD_MS);
}
void Dry(bool state){
  if(state){
    IO_digitalWrite(6, HIGH);
  }else{
    IO_digitalWrite(6, LOW);
  }
}
void Slot(bool state){
  if(state){
    digitalWrite(EN_PIN, HIGH);
  }else{
    digitalWrite(EN_PIN, LOW);
  }
}
void ClearJok()
{
  IO_digitalWrite(2, LOW);
  IO_digitalWrite(3, LOW);
  state_jok = false;
}
void ClearRelay()
{
  IO_digitalWrite(0, LOW);
  IO_digitalWrite(1, LOW);
  IO_digitalWrite(2, LOW);
  IO_digitalWrite(3, LOW);
  IO_digitalWrite(4, LOW);
  IO_digitalWrite(5, LOW);
  // IO_digitalWrite(6, LOW);
}

void setProgram(){
  // state_relay = true;
  if(program == 1){
    Serial.println("program 1 is running");
    for(int i = 0; i < program1[0]; i++){ //select program machine
      if(program1[2] == 1){
        JokBack();
      }else{
        Jok();
      }
    }
    for(int i = 0; i < program1[1]; i++){ //select temp
      Temp();
    }
    hrs = TimeCountdown1[0];
    minn = TimeCountdown1[1];
    second = 0;
    state_step2 = false;
    state_step3 = false;

  }else if(program == 2){
    Serial.println("program 2 is running");
    for(int i = 0; i < program2[0]; i++){ //select program machine
      if(program2[2] == 1){
        JokBack();
      }else{
        Jok();
      }
    }
    for(int i = 0; i < program2[1]; i++){ //select temp
      Temp();
    }
    hrs = TimeCountdown1[0];
    minn = TimeCountdown1[1];
    second = 0;
    state_step2 = false;
    state_step3 = false;

  }else if(program == 3){
    Serial.println("program 3 is running");
    for(int i = 0; i < program3[0]; i++){ //select program machine
      if(program3[2] == 1){
        JokBack();
      }else{
        Jok();
      }
    }
    for(int i = 0; i < program3[1]; i++){ //select temp
      Temp();
    }
    hrs = TimeCountdown1[0];
    minn = TimeCountdown1[1];
    second = 0;
    state_step2 = false;
    state_step3 = false;

  }else if(program == 4){ // drum wash
    Serial.println("drum wash is running");
    for(int i = 0; i < drum[0]; i++){ //select program machine
      Jok();
    }
    state_step2 = true;
    state_step3 = true;
    hrs = TimeCountdowndrum[0];
    minn = TimeCountdowndrum[1];
    second = 0;

  }else if(program == 5){ // rin command
    Serial.println("program rin is running");
    if(rincommand[1] == 1){
      for(int i = 0; i < rincommand[0]; i++){ //select program machine
        Jok();
      }
      for(int i = 0; i < spin; i++){ //select program machine
        Spin();
      }
      state_step2 = true;
      state_step3 = true;
    }else if(rincommand[1] == 2){
      for(int i = 0; i < rincommand[0]; i++){ //select program machine
        Jok();
      }
      state_step2 = true;
      state_step3 = false;
    }
    hrs = 0;
    minn = check_runing_time[0];
    second = 0;
    step = 2;

  }else if(program == 6){ // spin command
    Serial.println("program spin is running");
    for(int i = 0; i < spin; i++){ //select program machine
      Spin();
    }
    hrs = 0;
    minn = check_runing_time[1];
    second = 0;
    state_step2 = true;
    state_step3 = true;
    step = 3;

  }
  
  stateUpdateState = 1;
}

void pulse_in_cb() { // เมื่อได้รับ Pulse
  unsigned long duration = millis();
  while(!digitalRead(pinSlot)){
    //paul ++;
  }
  unsigned long coinISR = millis()-duration;
  if(coinISR >= coinPulse){
    count_update_flag = true;
  }
  //Serial.println("recieved coin");
}

void addFlagstoFirstscreen(){
  lv_obj_clear_flag(ui_conS1, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_btn_login,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_conS2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_conS3, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_conS4, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_conS5, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_conS6, LV_OBJ_FLAG_HIDDEN);
  screenUse = 1;
  Display.loop();
}

static void addDryMinutes(int minutes)
{
  if (minutes <= 0)
    return;
  minn += minutes;
  while (minn >= 60)
  {
    hrs++;
    minn -= 60;
  }
}

void setStartMachine(int dryFirstPaymentBaht = 0){
  lv_obj_add_flag(ui_conS1,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_conS3,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_conS4,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_conS5,LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_conS6,LV_OBJ_FLAG_HIDDEN);
  if(Mode == 2){
    Serial.println("Dry is runing..");
    lv_obj_add_flag(ui_lb_state_th,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_lb_state_en,LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui_lb_timer_machine, "00:00:00");
    lv_obj_clear_flag(ui_lb_timer_machine,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_img_src(ui_btn_img_temp, &ui_img_asset_96_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(ui_con_icon_step,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_con_dry_text,LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui_Label13, "ต่อเวลาด้วยการแสกนจ่ายกด ==>>");
    lv_label_set_text(ui_Label10, "Increase time with qr code pls touch ==>>");
    if(program == 1){
      minn = timerDry[0]+1;
    }else if(program == 2){
      minn = timerDry[1]+1;
    }else if(program == 3){
      minn = timerDry[2]+1;
    }
    second = 0;
    if(minn >= 60){
      hrs++;
      minn = minn - 60;
    }
    if (dryFirstPaymentBaht > 0 && program >= 1 && program <= 3)
    {
      const int tierPrice = PriceShow[program - 1];
      if (tierPrice > 0 && dryFirstPaymentBaht > tierPrice && coinValue > 0)
      {
        const int overpay = dryFirstPaymentBaht - tierPrice;
        const int extraMin = (overpay / coinValue) * DRY_EXTEND_MIN_PER_COIN;
        addDryMinutes(extraMin);
        Serial.println("Dry overpay +" + String(extraMin) + " min (" + String(overpay) + " baht over tier " + String(tierPrice) + ")");
      }
    }
    lv_obj_set_style_bg_img_src(ui_btn_img_temp, &ui_img_asset_96_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    stateUpdateState = 1;
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }else{
    Serial.println("set start machine Wash is runing..");
    // ตั้งเวลารวมตามโปรแกรมล่วงหน้า เพื่อให้ ack แรกแสดงเวลาพร้อม status (ตรงกับ setProgram)
    // นับถอยหลังจริงเริ่มเมื่อ status_machine_run = true เท่านั้น (machineRuning) จึงไม่ลดระหว่างเตรียม
    if(program == 1 || program == 2 || program == 3){
      hrs = TimeCountdown1[0]; minn = TimeCountdown1[1]; second = 0;
    }else if(program == 4){
      hrs = TimeCountdowndrum[0]; minn = TimeCountdowndrum[1]; second = 0;
    }else if(program == 5){
      hrs = 0; minn = check_runing_time[0]; second = 0;
    }else if(program == 6){
      hrs = 0; minn = check_runing_time[1]; second = 0;
    }
    lv_obj_add_flag(ui_lb_timer_machine,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_lb_state_th,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_lb_state_en,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_con_icon_step,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_con_dry_text,LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(ui_lb_state_th, "เครื่องกำลังทำงาน");
    lv_label_set_text(ui_lb_state_en, "Machine is runing");

    lv_obj_clear_flag(ui_img_wash,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_img_rin,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_img_spin,LV_OBJ_FLAG_HIDDEN);
    if(program == 1){
      lv_obj_set_style_bg_img_src(ui_btn_img_temp, &ui_img_asset_87_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    }else if(program == 2){
      lv_obj_set_style_bg_img_src(ui_btn_img_temp, &ui_img_asset_88_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    }else if(program == 3){
      lv_obj_set_style_bg_img_src(ui_btn_img_temp, &ui_img_asset_89_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    }else if(program == 4){
      lv_obj_set_style_bg_img_src(ui_btn_img_temp, &ui_img_asset_89_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    }else if(program == 5){
      lv_obj_set_style_bg_img_src(ui_btn_img_temp, &ui_img_asset_87_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    }else if(program == 6){
      lv_obj_set_style_bg_img_src(ui_btn_img_temp, &ui_img_asset_87_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    
  }
  status_machine_prepare = true;
  chanel = 0;
  step = 0;
  indexSet = 0;
  stateUpdateState = true;
  timerstanby = millis();
  runSessionSavePhase(Mode == 2 ? RS_DRY_PREPARE : RS_WASH_PREPARE);
  // Serial.println("--------------------- : " + String(chanel));
}
void prepareRunMachine(){
  // static unsigned long timePrepare = millis();
  if(status_machine_prepare){
    // Serial.println("*********************** : " + String(chanel));
    if(millis() - timerstanby >= 2000){
      Serial.println("status_machine_prepare is true");
      status_countdown_wait = false;
      status_machine_prepare = false;
      chanelcoinStatus = false;
      if(Mode == 2){
        // state_error = 4; chanel = 11;
        Dry(1);
        status_machine_run = true;
        runSessionSavePhase(RS_DRY_RUNNING);
        // digitalWrite(EN_PIN, HIGH); 
        Slot(1);
      }else{
        chanel = 1;
        runSessionSavePhase(RS_WASH_STARTUP);
        // digitalWrite(EN_PIN, LOW);
        Slot(0);
      }
    }else{
      // timerstanby = millis();
    }
  }
}
void updateBalanceIncreateDry()
{
  if (stateUpdateBalanceDry){
    if(millis() - timerstanby >= 10000)
    {
      stateUpdateBalanceDry = false;
      // เพิ่มยอดรายรับจากการต่อเวลาอบเข้า buffer รวม
      pendingBalance += priceSentVerver;
      priceSentVerver = 0;
      stateSentPriceServer = 1; // ขอให้ taskWifiMqtt ส่งเมื่อออนไลน์
      revenuePersist();         // กันยอดหายถ้า reboot ก่อนส่ง
    }
  }
}
void count_update(){
  if (count_update_flag && chanelcoinStatus) {
    count++; // เพิ่มค่าในตัวแปร count ขึ้น 1 ค่า
    count_update_flag = false;
    item_price = item_price - (count * coinValue);
    lv_label_set_text_fmt(ui_lb_coin, "%d.-", item_price);
    Display.loop(); // Keep GUI work
    Serial.println("item price = " + String(item_price));
    count = 0;
    if(item_price <= 0){
      const int paidTotal = priceSentVerver - item_price;
      vTaskDelay(500 / portTICK_PERIOD_MS);
      lv_obj_add_flag(ui_conS4,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_conS6,LV_OBJ_FLAG_HIDDEN);
      pendingBalance += paidTotal;
      stateSentPriceServer = 1;
      revenuePersist();         // กันยอดหายถ้า reboot ก่อนส่ง
      setStartMachine((Mode == 2 && program >= 1 && program <= 3) ? paidTotal : 0);
      item_price = 0;
      priceSentVerver = 0;
    }
  }else if(Mode == 2 && status_machine_run && count_update_flag){
    count++;
    count_update_flag = false;
    priceSentVerver = priceSentVerver + (count * coinValue);
    program = 7;
    chanelPay = 0;
    stateUpdateBalanceDry = true;
    // stateSentPriceServer = 1; // แจ้งว่าส่งราคาไปยังเซิร์ฟเวอร์แล้ว
    minn = minn + DRY_EXTEND_MIN_PER_COIN;
    if(minn >= 60){
      hrs++;
      minn = minn - 60;
    }
    count = 0;
    timerstanby = millis();
  }
}

void setupTime(){
  // ตั้งค่าเวลาโดยใช้ NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  rtc.setTimeStruct(timeinfo);
  vTaskDelay(100 / portTICK_PERIOD_MS);
}
void displayDateTime(){
  // อัปเดตเวลาและแสดงผล
  lv_label_set_text_fmt(ui_lb_datetime, "%04d-%02d-%02d %02d:%02d:%02d", 
                        rtc.getYear(), rtc.getMonth()+1, rtc.getDay(),
                        rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
 }
void secondMillis(){
  static unsigned long timerdatetime = millis();
  if(millis() - timerdatetime >= 1000){
    displayDateTime();
    timerdatetime = millis();
  }else if(millis() < timerdatetime){
    timerdatetime = millis();
  }
}
void setMc_no(){
  String m = String(rtc.getMinute());
  String h = String(rtc.getHour(true));
  String d = String(rtc.getDay());
  String M = String(rtc.getMonth());
  String y = String(rtc.getYear());
  String msg2;
  if(m.length() < 2){
    m = "0" + m;
  }
  if(h.length() < 2){
    h = "0" + h;
  }
  if(d.length() < 2){
    d = "0" + d;
  }
  if(M.length() < 2){
    M = "0" + M;
  }
  if(y.length() < 2){
    y = "0" + y;
  }
  mch_order_no_set = y+M+d+h+m;
}

void resetwaittime(){
  second_countdown_wait = 0;
  minn_countdown_wait = 5;
}
void countdownWait(){
  if(status_countdown_wait){
    static unsigned long waittime = millis();
    if ((waittime == 0) || ((millis() < waittime) || ((millis() - waittime) > 1000))){
      second_countdown_wait--;
      if(second_countdown_wait <= -1){
        minn_countdown_wait--;
        second_countdown_wait = 59;
        if(minn_countdown_wait <= -1){
          lv_obj_add_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(ui_conS3,LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(ui_conS4,LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(ui_conS5,LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(ui_conS6,LV_OBJ_FLAG_HIDDEN);
          lv_obj_clear_flag(ui_conS1,LV_OBJ_FLAG_HIDDEN);
          status_countdown_wait = false;
        }
      }
      lv_label_set_text_fmt(ui_lb_clock_qr, "%02d:%02d", minn_countdown_wait, second_countdown_wait);
      waittime = millis();
    }
  }
}

void checkQrpaymentRead(){
  if (cm == "qrsuccess") {
    if (value_str1 == "SUCCESS") {
      Serial.println("value_str_buf : " + value_str2);
      StaticJsonDocument<256> jsonDoc;
      jsonDoc["id"] = value_str2;
      jsonDoc["state"] = "SUCCESS";
      String requestBody;
      serializeJson(jsonDoc, requestBody);
      mqclient.publish("respondMc", requestBody.c_str());
      vTaskDelay(100 / portTICK_PERIOD_MS);
      // ให้ taskDisplay อัปเดต UI และ logic (ป้องกันหลาย task แตะ LVGL)
      pendingUIAction = PENDING_UI_QR_SUCCESS;
    }
  }
}
void checkQrpaymentGen(){
  if (cm == "qrgen") {
    if (value_str1 == "SUCCESS") {
      pendingQrPayload = value_str2;
      pendingUIAction = PENDING_UI_QR_GEN;
      vTaskDelay(100 / portTICK_PERIOD_MS);
    } else {
      Serial.print("qrgen fail..");
    }
  }
}
void mqttreqest(){
  if(WiFi.status()== WL_CONNECTED){
      // printLocalTime();
      setMc_no();
      mch_order_no = Noserial + mch_order_no_set;
      StaticJsonDocument<256> jsonDoc;
      jsonDoc["mch_order_no"] = mch_order_no;
      jsonDoc["device_id"] = Noserial;
      jsonDoc["total_fee"] = String(item_price*100);
      jsonDoc["attach"] = String(gid);
      jsonDoc["product"] = String(program);
      String requestBody;
      serializeJson(jsonDoc, requestBody);

      mqclient.publish("qrgen", requestBody.c_str());
      // vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void machineRuning(){
  static unsigned long lastStatusReportMs = 0;
  if (!status_machine_run) {
    lastStatusReportMs = 0;
    return;
  }
  runSessionMaybeDryAutosave();
  {
    if(lv_obj_has_flag(ui_conS6, LV_OBJ_FLAG_HIDDEN) && !stateIntime){
      lv_obj_clear_flag(ui_conS6,LV_OBJ_FLAG_HIDDEN);
    }
    if(lv_obj_has_flag(ui_lb_timer_machine, LV_OBJ_FLAG_HIDDEN) && !stateIntime){
      lv_obj_add_flag(ui_lb_state_th,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_lb_state_en,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_lb_timer_machine,LV_OBJ_FLAG_HIDDEN);
    }
    static unsigned long machine_runing_time = millis();
    if ((machine_runing_time == 0) || ((millis() < machine_runing_time) || ((millis() - machine_runing_time) > 1000))){
      second--;
      if(second <= -1){
        if(!pause_timer){
          minn--;// timer is pause
        } 
        second = 59;
        
        if(Mode == 2){
          if(minn <= -1){
            if(hrs >= 1){
              hrs--;
              minn = 59;
            }else{
              // go to screen 1
              addFlagstoFirstscreen();
              Dry(0);
              minn_countdown_wait = 1;
              second_countdown_wait = 0;
              status_countdown_wait = true;
              status_machine_run = false;
              endProgram = true;
              chanel = 10;
            }
          }
        }else{
          if(minn <= 1 && hrs == 0){
            minn = 1;
            count_minn_pass++;
            //new action
            if(count_minn_pass == 5){
              if(CodeMachine == 4 || CodeMachine == 3){
                Start();
                vTaskDelay(1500 / portTICK_PERIOD_MS);
                Power();
                vTaskDelay(1000 / portTICK_PERIOD_MS);

                StatusControl = "off";
                // stateUpdateState = 1;

                // go to screen 1
                // addFlagstoFirstscreen();
                endProgram = false;
                chanel = 10;
              }
            }
            // for old machine
            if(count_minn_pass == 15){
              lv_obj_add_flag(ui_lb_timer_machine,LV_OBJ_FLAG_HIDDEN);
              lv_obj_clear_flag(ui_lb_state_th,LV_OBJ_FLAG_HIDDEN);
              lv_obj_clear_flag(ui_lb_state_en,LV_OBJ_FLAG_HIDDEN);
        
              lv_label_set_text(ui_lb_state_th, "เครื่องไม่สามารถปั่นแรง");
              lv_label_set_text(ui_lb_state_en, "The machine cannot spin at high speed.");
              reportEspFaultToMelody("01");
            }else if(count_minn_pass >= 20){
              // go to screen 1
              // addFlagstoFirstscreen();
              endProgram = false;
              chanel = 10;
            }
          }else if(minn <= -1 && hrs >= 1){
            hrs--;
            minn = 59;
          }
        }
        // ส่ง status ไป UpdateState ทุก statusReportIntervalMinutes นาที (ค่าเริ่มต้น 5)
        // แต่ 5 นาทีสุดท้าย (hrs==0 && minn<=5) ส่งทุก 1 นาที ให้เวลา ESP↔server ตรงกันมากที่สุด
        if (lastStatusReportMs == 0) lastStatusReportMs = millis();
        const bool lastFiveMinutes = (hrs == 0 && minn <= 5);
        const unsigned long reportIntervalMs =
            lastFiveMinutes ? 60UL * 1000
                            : (unsigned long)statusReportIntervalMinutes * 60 * 1000;
        if ((unsigned long)(millis() - lastStatusReportMs) >= reportIntervalMs) {
          stateUpdateState = 1;
          lastStatusReportMs = millis();
        }
        // Serial.println("chanel => " + String(chanel) + " :: " + String(millis()) + " : " + String(timerstanby) + " step : " + String(step));
        // Serial.println("state => " + StatusControl + " :: " + String(hrs) + " : " + String(minn) + " : " + String(second));
      }
      lv_label_set_text_fmt(ui_lb_timer_machine, "%02d:%02d:%02d", hrs, minn, second);
      Display.loop();
      
      if(Mode != 2){
        updateStepIcon();
      }
      machine_runing_time = millis();
    }
  }
}

enum WifiState {
  WIFI_IDLE = 0,
  WIFI_DISCONNECTING,
  WIFI_CONNECTING
};

static unsigned long wifiConnectedSinceMs = 0;
static unsigned long wifiReconnectBackoffMs = 0;  // 0 = พยายามต่อครั้งแรกทันทีหลัง boot
static unsigned long wifiAssocDownSinceMs = 0;
static const unsigned long WIFI_STABLE_WINDOW_MS = 2000;
static const unsigned long WIFI_STABLE_MACHINE_MS = 1000;
static const unsigned long WIFI_DOWN_HYSTERESIS_MS = 2500;
static const unsigned long WIFI_RETRY_BACKOFF_MAX_MS = 30000;
static const unsigned long MQTT_RETRY_BACKOFF_MAX_MS = 60000;
static const unsigned long MQTT_RETRY_BACKOFF_RUN_MS = 15000;

static bool espWifiAssociated()
{
  return WiFi.status() == WL_CONNECTED;
}

static bool espWifiDownConfirmed(unsigned long now)
{
  if (espWifiAssociated())
  {
    wifiAssocDownSinceMs = 0;
    return false;
  }
  if (wifiAssocDownSinceMs == 0)
    wifiAssocDownSinceMs = now;
  return (unsigned long)(now - wifiAssocDownSinceMs) >= WIFI_DOWN_HYSTERESIS_MS;
}

static unsigned long wifiStableWindowMs()
{
  if (status_machine_run || status_machine_prepare)
    return WIFI_STABLE_MACHINE_MS;
  return WIFI_STABLE_WINDOW_MS;
}

static bool wifiLinkUsable() {
  return espWifiAssociated() &&
         wifiConnectedSinceMs != 0 &&
         (unsigned long)(millis() - wifiConnectedSinceMs) >= wifiStableWindowMs();
}

/** เรียกเมื่อ WiFi กลับมา WL_CONNECTED หลังเคยหลุด — เริ่มนับ warmup ก่อน MQTT/HTTP */
static void noteWifiLinkUp(bool syncTime)
{
  if (wifiConnectedSinceMs != 0)
    return;
  wifiConnectedSinceMs = millis();
  wifiReconnectBackoffMs = 1000;
  Serial.println(F("[WiFi] link up — stable warmup window started"));
  Serial.print(F("IP address: "));
  Serial.println(WiFi.localIP());
  Serial.println("WiFi stable warmup(ms): " + String(wifiStableWindowMs()));
  if (syncTime)
    setupTime();
}

void connectwifi() {
  static unsigned long startAttemptTime = millis();
  static WifiState wifiState = WIFI_IDLE;
  static bool wifiBootConnectLogged = false;

  if (!espWifiAssociated()) {
    unsigned long now = millis();
    if (!espWifiDownConfirmed(now))
      return;

    wifiConnectedSinceMs = 0;

    switch (wifiState) {
      case WIFI_IDLE:
        if (now - startAttemptTime >= wifiReconnectBackoffMs) {
          Serial.println(F("[WiFi] disconnect (clean) before connect/retry"));
          WiFi.disconnect(true);
          wifiState = WIFI_DISCONNECTING;
          startAttemptTime = now;
        }
        break;

      case WIFI_DISCONNECTING:
        if (now - startAttemptTime >= 1000) {
          if (!wifiBootConnectLogged) {
            wifiBootConnectLogged = true;
            Serial.println();
            Serial.print(F("[WiFi] Connecting to "));
            Serial.println(ssidStr);
          } else {
            Serial.println(F("[WiFi] Reconnecting..."));
          }
          WiFi.begin(ssidStr.c_str(), passStr.c_str());
          wifiState = WIFI_CONNECTING;
          startAttemptTime = now;
        }
        break;

      case WIFI_CONNECTING:
        if (now - startAttemptTime >= WiFi_TIMEOUT_MS && WiFi.status() != WL_CONNECTED) {
          Serial.println(F("[WiFi] connect failed — reset stack"));
          WiFi.disconnect(true);
          wifiState = WIFI_IDLE;
          startAttemptTime = now;
          wifiReconnectBackoffMs = min(max(wifiReconnectBackoffMs, 1000UL) * 2, WIFI_RETRY_BACKOFF_MAX_MS);
          Serial.println("WiFi retry backoff(ms): " + String(wifiReconnectBackoffMs));
        }
        break;
    }
  } else {
    wifiAssocDownSinceMs = 0;
    if (wifiState == WIFI_CONNECTING) {
      wifiState = WIFI_IDLE;
      Serial.println("\nWiFi connected");
      startAttemptTime = millis();
      noteWifiLinkUp(true);
    } else if (wifiConnectedSinceMs == 0) {
      // auto-reconnect / ต่อกลับขณะ state machine ไม่ใช่ CONNECTING
      noteWifiLinkUp(false);
    }
  }
}
// --- MQTT presence / LWT + mqttDiag (MelodyWebapp subscribe presence/+ , mqttDiag/+) ---
// เหมือน ATD_TM_V2_New_Hier : LWT แจ้ง offline อัตโนมัติเมื่อหลุด, presence online เมื่อต่อสำเร็จ,
// mqttDiag "recovered" เมื่อกลับมาต่อได้หลังเคย fail
static char presenceTopicBuf[64];
static char presencePayloadBuf[320];
static char mqttDiagTopicBuf[64];
static int lastMqttFailRc = 0;
static int lastMqttFailPort = 0;
static int mqttConnectFailStreak = 0;
static char configResponseTopicBuf[80];
static bool pendingPresenceAfterMqttConnect = false;
static bool pendingPresenceHeartbeat = false;
static bool pendingUpdateStatePublish = false;
static char pendingUpdateStateBuf[220];
static bool pendingMqttDiagAfterConnect = false;
static int pendingMqttDiagRc = 0;
static int pendingMqttDiagFails = 0;
static bool mqttDownForFallback();

/** ตัด MQTT/TCP เมื่อ WiFi หลุด — กัน publish บน socket ค้าง (assert pbuf_free) */
static void teardownMqttOnWifiDown()
{
  pendingPresenceAfterMqttConnect = false;
  pendingPresenceHeartbeat = false;
  pendingUpdateStatePublish = false;
  pendingUpdateStateBuf[0] = '\0';
  pendingMqttDiagAfterConnect = false;
  if (!netLockEnter())
    return;
  // ปิดครั้งเดียว — disconnect() มี client.stop() ในตัว; อย่า stop() ซ้ำ (lwIP pbuf assert)
  if (mqclient.connected())
    mqclient.disconnect();
  else
    client.stop();
  netLockLeave();
}

static void buildPresenceTopicStr() {
  snprintf(presenceTopicBuf, sizeof(presenceTopicBuf), "presence/%s", Noserial.c_str());
}

static void buildMqttDiagTopicStr() {
  snprintf(mqttDiagTopicBuf, sizeof(mqttDiagTopicBuf), "mqttDiag/%s", Noserial.c_str());
}

static void buildPresencePayload(const char* state, bool withConnInfo = false) {
  StaticJsonDocument<384> doc;
  doc["cm"] = "presence";
  doc["id"] = Noserial;
  doc["value_str1"] = gid;
  doc["value_str2"] = state;
  // รายงานเวอร์ชัน firmware + โปรโตคอล Melody (v3) — backend เก็บไว้เตือน version mismatch
  String fwStr = String(fwversion[1]);
  fwStr.replace("Version ", "");
  fwStr.trim();
  doc["fw"] = fwStr;
  doc["mv"] = 3;
  if (withConnInfo && mqtt_server != nullptr) {
    doc["broker"] = mqtt_server;
    doc["port"] = mqtt_port;
    doc["mqttStatus"] = mqttStatus;
    if (lastMqttFailRc != 0) {
      doc["fail_rc"] = lastMqttFailRc;
      if (lastMqttFailPort > 0)
        doc["fail_port"] = lastMqttFailPort;
    }
  }
  presencePayloadBuf[0] = '\0';
  serializeJson(doc, presencePayloadBuf, sizeof(presencePayloadBuf));
}

static void publishMqttDiag(const char* event, int rc, int failCount) {
  if (!mqclient.connected())
    return;
  if (!netLockEnter())
    return;
  buildMqttDiagTopicStr();
  StaticJsonDocument<256> doc;
  doc["cm"] = "mqttDiag";
  doc["id"] = Noserial;
  doc["event"] = event;
  if (mqtt_server != nullptr)
    doc["broker"] = mqtt_server;
  doc["port"] = mqtt_port;
  doc["mqttStatus"] = mqttStatus;
  if (rc != 0)
    doc["rc"] = rc;
  if (failCount > 0)
    doc["fail_count"] = failCount;
  char buf[256];
  buf[0] = '\0';
  serializeJson(doc, buf, sizeof(buf));
  mqclient.publish(mqttDiagTopicBuf, buf, false);
  netLockLeave();
  Serial.println(String("[MQTT] mqttDiag ") + event + " -> " + mqttDiagTopicBuf);
}

/** งาน MQTT ที่ต้องทำนอก callback — publish รวมรอบเดียวแล้ว pump ครั้งเดียว (กัน lwIP pbuf crash) */
static void processDeferredMqttWork()
{
  if (!wifiLinkUsable() || !mqclient.connected())
    return;

  if (!netLockEnter())
    return;

  if (pendingUpdateStatePublish && pendingUpdateStateBuf[0] != '\0')
  {
    mqclient.publish("UpdateState", pendingUpdateStateBuf);
    pendingUpdateStatePublish = false;
  }

  if (pendingPresenceAfterMqttConnect)
  {
    pendingPresenceAfterMqttConnect = false;
    buildPresenceTopicStr();
    buildPresencePayload("online", true);
    if (mqclient.publish(presenceTopicBuf, presencePayloadBuf, true))
      Serial.println(String("[MQTT] presence online -> ") + presenceTopicBuf);
  }

  if (pendingPresenceHeartbeat)
  {
    pendingPresenceHeartbeat = false;
    buildPresenceTopicStr();
    buildPresencePayload("online", true);
    if (mqclient.publish(presenceTopicBuf, presencePayloadBuf, true))
      Serial.println(String("[MQTT] presence heartbeat -> ") + presenceTopicBuf);
  }

  if (pendingMqttDiagAfterConnect)
  {
    pendingMqttDiagAfterConnect = false;
    buildMqttDiagTopicStr();
    StaticJsonDocument<256> doc;
    doc["cm"] = "mqttDiag";
    doc["id"] = Noserial;
    doc["event"] = "recovered";
    if (mqtt_server != nullptr)
      doc["broker"] = mqtt_server;
    doc["port"] = mqtt_port;
    doc["mqttStatus"] = mqttStatus;
    if (pendingMqttDiagRc != 0)
      doc["rc"] = pendingMqttDiagRc;
    if (pendingMqttDiagFails > 0)
      doc["fail_count"] = pendingMqttDiagFails;
    char buf[256];
    buf[0] = '\0';
    serializeJson(doc, buf, sizeof(buf));
    mqclient.publish(mqttDiagTopicBuf, buf, false);
    mqttConnectFailStreak = 0;
    lastMqttFailRc = 0;
    lastMqttFailPort = 0;
    Serial.println(String("[MQTT] mqttDiag recovered -> ") + mqttDiagTopicBuf);
  }

  mqttPumpLoopLocked(2);
  netLockLeave();
}

bool publishPresenceOnline() {
  if (!mqclient.connected())
    return false;
  pendingPresenceHeartbeat = true;
  return true;
}

void publishPresenceOfflineGraceful() {
  if (!mqclient.connected())
    return;
  if (!netLockEnter())
    return;
  buildPresenceTopicStr();
  buildPresencePayload("offline");
  mqclient.publish(presenceTopicBuf, presencePayloadBuf, true);
  for (int i = 0; i < 5; i++) {
    mqttPumpLoopLocked(1);
    delay(20);
  }
  Serial.println(String("[MQTT] presence offline (graceful) -> ") + presenceTopicBuf);
  mqclient.disconnect();
  netLockLeave();
}

// ช่วง boot/setup: ยังไม่ poll HTTP / ไม่รับ reboot|OTA|set_broker — กันรีบูทวนซ้ำตอนตั้งค่า WiFi/MQTT/ดึง config
static unsigned long melodyBootMs = 0;
static const unsigned long MELODY_BOOT_SETUP_GRACE_MS = 180UL * 1000;

static bool isMelodyBootSetupPhase() {
  if (melodyBootMs == 0) return true;
  if (mqttDownForFallback())
    return false;
  if (firstGetdata || stateGetdata || stateSetupdata) return true;
  return (unsigned long)(millis() - melodyBootMs) < MELODY_BOOT_SETUP_GRACE_MS;
}

// กัน config ซ้ำตอน boot — รอ debounce แล้วใช้ชุดสุดท้ายจาก Melody
static unsigned long bootMelodySyncLastMs = 0;
static bool bootMelodyConfigPending = false;
static int bootMelodyConfigCount = 0;
static const unsigned long BOOT_MELODY_SYNC_DEBOUNCE_MS = 2500UL;

static bool bootMelodySyncQuietReady() {
  return bootMelodySyncLastMs > 0 &&
         (unsigned long)(millis() - bootMelodySyncLastMs) >= BOOT_MELODY_SYNC_DEBOUNCE_MS;
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  String message;

  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  String topicStr = String(topic);
  if (topicStr == "configResponse/" + Noserial) {
    if (isMelodyBootSetupPhase()) {
      bootMelodyConfigCount++;
      mqttPayloadBuffer = message;
      bootMelodyConfigPending = true;
      bootMelodySyncLastMs = millis();
      if (bootMelodyConfigCount > 1) {
        Serial.println(F("[MQTT] <<< รับ configResponse ชุดใหม่ — จะใช้ชุดสุดท้ายจาก Melody"));
      } else {
        Serial.println(F("[MQTT] <<< รับ configResponse สำเร็จ"));
      }
      Serial.println("       topic  = configResponse/" + Noserial);
      Serial.println(F("       -> รอ debounce แล้ว GetSetupData() จะบันทึกชุดสุดท้าย"));
      return;
    }
    mqttPayloadBuffer = message;
    stateSetupdata = true;
    Serial.println(F("[MQTT] <<< รับ configResponse สำเร็จ"));
    Serial.println("       topic  = configResponse/" + Noserial);
    Serial.println("       ความยาว = " + String(message.length()) + " bytes");
    if (message.length() > 0) {
      int previewLen = (message.length() > 180) ? 180 : message.length();
      Serial.println("       payload (ตัวอย่าง) = " + message.substring(0, previewLen) + (message.length() > 180 ? "..." : ""));
    }
    Serial.println(F("       -> GetSetupData() จะบันทึกลง NVS"));
    return;
  }

  // payload บน V<gid> อาจเป็น setup ขนาดใหญ่ (1–2 KB) ต้องใช้ buffer พอ ไม่ใช่ 256
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  // ตรวจสอบ key ก่อนใช้งาน
  if (!doc.containsKey("cm") || !doc.containsKey("id")) {
    Serial.println("Missing required keys in JSON");
    return;
  }

  // Extract values
  String cm_buf = doc["cm"].as<String>();
  String id_buf = doc["id"].as<String>();
  String value_str1_buf = doc["value_str1"].as<String>();
  String value_str2_buf = doc["value_str2"].as<String>();
  // value_str1_int = atoi(doc["value_str1"]);

  cm = cm_buf;
  value_str1 = value_str1_buf;
  value_str2 = value_str2_buf;

  if(id_buf == Noserial){

    if(cm_buf == "getdata"){
      Serial.println(F("[MQTT] >>> getdata received (แอดมินสอบถาม) <<<"));
      if (id_buf == Noserial) {
        stateSendConfigMqtt = true;  // ส่ง config กลับทาง getdataResponse (เหมือน ATD_TM_V2_New_Hier)
      }
    }else if(cm_buf == "setup"){
      Serial.println("setup data ####### : " + cm_buf);
      mqttPayloadBuffer = message;
      stateSetupdata = true;
    }
    else if(cm_buf == "cmProgram" || cm_buf == "cmCommand"){
      Serial.println("commandApp ####### : " + cm_buf);
      commandApp();
    }
    
    else{
      // String value_str2_buf = doc["value_str2"].as<String>();
      // value_str2 = value_str2_buf;
      // Print extracted values
      Serial.println("cm: " + cm_buf);
      Serial.println("id: " + id_buf);
      Serial.println("value_str1: " + value_str1_buf);
      Serial.println("value_str2: " + value_str2_buf);

      // gen qr
      checkQrpaymentGen();
      // read pay success
      checkQrpaymentRead();

      // //command
      // commandApp();
    }
  }
}

void mqttreconnect() {
  static unsigned long lastReconnectAttempt = 0;
  static unsigned long reconnectInterval = 5000;   // backoff เบา 5→15s
  static uint8_t mqttSamePortFails = 0;            // fail ติดกันในพอร์ตเดิม — ครบ 4 ค่อยเปลี่ยนพอร์ต

  // วินิจฉัยสาเหตุหลุด: log ตอน connected -> disconnected (edge)
  // rc=-3 CONNECTION_LOST (TCP ถูกตัด) / rc=-4 CONNECTION_TIMEOUT (ping ไม่ตอบ) / rc=-1 เราสั่ง disconnect
  static bool mqttWasConnected = false;
  bool mqttNowConnected = mqclient.connected();
  if (mqttWasConnected && !mqttNowConnected) {
    Serial.print(F("[MQTT] dropped rc="));
    Serial.print(mqclient.state());
    Serial.print(F(" wifi="));
    Serial.print(WiFi.status());
    Serial.print(F(" rssi="));
    Serial.println(WiFi.RSSI());
  }
  mqttWasConnected = mqttNowConnected;

  if (!wifiLinkUsable()) {
    return;
  }

  if (!mqclient.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt >= reconnectInterval) {
      lastReconnectAttempt = now;
      if (!netLockEnter())
        return;

      // ปิด socket เก่าครั้งเดียว — อย่า double-close (disconnect() stop ในตัว + client.stop() ซ้ำ
      // ทำ lwIP pbuf ref พัง -> assert "pbuf_free: p->ref > 0" -> รีบูตกลางงาน)
      if (mqclient.connected())
        mqclient.disconnect();
      else
        client.stop();
      vTaskDelay(pdMS_TO_TICKS(50));
      yield();

      // สลับ server และ port ตาม mqttStatus
      if (mqttStatus == 1) {
        mqtt_server = mqtt_server1;
        mqtt_port = mqtt_port1;
      } else {
        mqtt_server = "broker.mqtt.cool";
        mqtt_port = mqtt_port2;
      }

      Serial.print("MQTT connecting to port: ");
      Serial.println(mqtt_port);

      vTaskDelay(pdMS_TO_TICKS(1));

      mqclient.setServer(mqtt_server, mqtt_port);
      mqclient.setKeepAlive(60);  // ตรงกับ 3.00 — keepAlive 15 พิสูจน์แล้วว่า drop ถี่ขึ้น + churn กระตุ้น crash
      mqclient.setCallback(callback);
      mqclient.setSocketTimeout(15);  // ตรงกับ 3.00 (default 15s) — 6s ตัด socket เร็วไปตอน WiFi jitter → หลุดทั้งที่ยังต่อ
      mqclient.setBufferSize(2048);
      client.setTimeout(15000);

      topic = "V" + String(gid);
      // LWT : ถ้า ESP หลุดแบบไม่ตั้งใจ broker จะ publish presence offline ให้อัตโนมัติ
      buildPresenceTopicStr();
      buildPresencePayload("offline");
      snprintf(configResponseTopicBuf, sizeof(configResponseTopicBuf), "configResponse/%s", Noserial.c_str());

      if (mqclient.connect(
              Noserial.c_str(),
              mqtt_username,
              mqtt_password,
              presenceTopicBuf,
              1,
              true,
              presencePayloadBuf)) {
        mqclient.subscribe(topic.c_str());
        mqclient.subscribe(configResponseTopicBuf);
        int prevRc = lastMqttFailRc;
        int prevFails = mqttConnectFailStreak;
        pendingPresenceAfterMqttConnect = true;
        if (!isMelodyBootSetupPhase())
          stateUpdateState = 1;
        if (prevFails > 0) {
          pendingMqttDiagAfterConnect = true;
          pendingMqttDiagRc = prevRc;
          pendingMqttDiagFails = prevFails;
        }
        reconnectInterval = 5000;
        mqttSamePortFails = 0;
        Serial.println("MQTT connected : " + String(mqtt_port) + " subscribed : " + topic + " , " + String(configResponseTopicBuf));
      } else {
        lastMqttFailRc = mqclient.state();
        lastMqttFailPort = mqtt_port;
        mqttConnectFailStreak++;
        mqttSamePortFails++;
        // อยู่พอร์ตเดิมก่อน — fail ครบ 4 ครั้งค่อยเปลี่ยนพอร์ต; backoff เบา 5→15s; เปลี่ยนพอร์ตแล้วเริ่มใหม่ 5s
        if (mqttStatus == 1 && mqttSamePortFails >= 4) {
          mqttSamePortFails = 0;
          mqtt_port1++;
          if (mqtt_port1 >= 4745)
            mqtt_port1 = 4741;
          reconnectInterval = 5000;
        } else {
          reconnectInterval = min(reconnectInterval * 2, 15000UL);
        }
        Serial.print("MQTT connection failed, rc=");
        Serial.println(lastMqttFailRc);
        Serial.println("Will try again in " + String(reconnectInterval / 1000) + "s, port: " + String(mqtt_port1) + " (fail " + String(mqttSamePortFails) + "/4)");
      }
      vTaskDelay(pdMS_TO_TICKS(1));
      netLockLeave();
    }
  }
}
void updateWiFiIcon(){
  static unsigned long timerWifi = 0;
  if((timerWifi == 0) || ((millis() < timerWifi) || ((millis() - timerWifi) > 500))){
    if(WiFi.isConnected()){
      lv_obj_clear_flag(ui_icon_wifi,LV_OBJ_FLAG_HIDDEN);
      if (g_mqttOnline) {  // อ่าน cache — ห้ามเรียก mqclient.connected() ใน task จอ (recv ซ้อน -> pbuf crash)
        lv_obj_clear_flag(ui_icon_mqtt,LV_OBJ_FLAG_HIDDEN);
      }else{
        if(!lv_obj_has_flag(ui_icon_mqtt, LV_OBJ_FLAG_HIDDEN)){
          lv_obj_add_flag(ui_icon_mqtt,LV_OBJ_FLAG_HIDDEN);
        }else{
          lv_obj_clear_flag(ui_icon_mqtt,LV_OBJ_FLAG_HIDDEN);
        }
      }
      
    }else{
      if(!lv_obj_has_flag(ui_icon_wifi, LV_OBJ_FLAG_HIDDEN)){
        lv_obj_add_flag(ui_icon_wifi,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_icon_mqtt,LV_OBJ_FLAG_HIDDEN);
      }else{
        lv_obj_clear_flag(ui_icon_wifi,LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_icon_mqtt,LV_OBJ_FLAG_HIDDEN);
      }
    }
    timerWifi = millis();
  }
}
void updateStepIcon(){
  if(minn >= check_runing_time[0]){
    step = 1;
  }else if(minn < check_runing_time[0] && minn >= check_runing_time[1]){
    step = 2;
  }else if(minn < check_runing_time[1]){
    step = 3;
  }

  if(step == 1){
    lv_obj_clear_flag(ui_img_rin,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_img_spin,LV_OBJ_FLAG_HIDDEN);
    if(!lv_obj_has_flag(ui_img_wash, LV_OBJ_FLAG_HIDDEN)){
      lv_obj_add_flag(ui_img_wash,LV_OBJ_FLAG_HIDDEN);
    }else{
      lv_obj_clear_flag(ui_img_wash,LV_OBJ_FLAG_HIDDEN);
    }
  }else if(step == 2){
    lv_obj_clear_flag(ui_img_wash,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_img_spin,LV_OBJ_FLAG_HIDDEN);
    if(!lv_obj_has_flag(ui_img_rin, LV_OBJ_FLAG_HIDDEN)){
      lv_obj_add_flag(ui_img_rin,LV_OBJ_FLAG_HIDDEN);
    }else{
      lv_obj_clear_flag(ui_img_rin,LV_OBJ_FLAG_HIDDEN);
    }
  }else if(step == 3){
    lv_obj_clear_flag(ui_img_wash,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_img_rin,LV_OBJ_FLAG_HIDDEN);
    if(!lv_obj_has_flag(ui_img_spin, LV_OBJ_FLAG_HIDDEN)){
      lv_obj_add_flag(ui_img_spin,LV_OBJ_FLAG_HIDDEN);
    }else{
      lv_obj_clear_flag(ui_img_spin,LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void checkLdr1(){
  if(stateCheckLdr1){
    static unsigned long timerCheckLDR = millis();
    static LdrAvgSampler ldrSampler;
    static bool sampling = false;

    if (millis() - timerCheckLDR >= 900)
    {
      if (!sampling) {
        ldrSampler.begin(LDR1_PIN);
        sampling = true;
      }
    }

    if (sampling) {
      int val = 0;
      if (ldrSampler.tick(&val)) {
        sampling = false;
        lv_label_set_text_fmt(ui_Label1, "ระบบกำลังอ่าน Ldr1 : %d", val);
        lv_label_set_text(ui_Label2, "Read Ldr1 Sensor");
        printLdrSummary("checkLdr1", LDR1_PIN, val);
        timerCheckLDR = millis();
      }
    }
  }  
}
void checkLdr2(){
  if(stateCheckLdr2){
    static unsigned long timerCheckLDR = millis();
    static LdrAvgSampler ldrSampler;
    static bool sampling = false;

    if (millis() - timerCheckLDR >= 900)
    {
      if (!sampling) {
        ldrSampler.begin(LDR2_PIN);
        sampling = true;
      }
    }

    if (sampling) {
      int val = 0;
      if (ldrSampler.tick(&val)) {
        sampling = false;
        lv_label_set_text_fmt(ui_Label1, "ระบบกำลังอ่าน Ldr2 : %d", val);
        lv_label_set_text(ui_Label2, "Read Ldr2 Sensor");
        printLdrSummary("checkLdr2", LDR2_PIN, val);
        timerCheckLDR = millis();
      }
    }
  }  
}

int checkLightStart(int countStateLight, bool stateWhileRead) {
  bool stateLight = false;
  unsigned long timerChecklight = millis();
  int sentReturn = 2; // 0=off, 1=blink, 2=on
  int ldrRead = 0;
  stateWhile = stateWhileRead;
  int LigthOn = 0;
  int LigthOff = 0;
  int ldrLigthCount = 0;
  bool ligth = false;

  // int LIGHT_OFF = 0;
  // int LIGHT_BLINK = 1;
  // int LIGHT_ON = 2;

  while (stateWhile) {
    static unsigned long lastReadTime = 0;
    static LdrAvgSampler ldrSampler;
    static bool sampling = false;
    unsigned long now = millis();

    if (!sampling && (now - lastReadTime >= LDR_READ_INTERVAL_MS)) {
      ldrSampler.begin(ldrPin);
      sampling = true;
    }

    int ldrRead = 0;
    if (sampling && ldrSampler.tick(&ldrRead)) {
      sampling = false;
      lastReadTime = now;
      printLdrSummary("checkLightStart", (uint8_t)ldrPin, ldrRead);

      if (ldrRead <= ldr_set && !stateLight) {
        countStateLight++;
        stateLight = true;
        Serial.println("Blink On : " + String(ldrRead));
        timerChecklight = millis();
      } else if (ldrRead > ldr_set + ldrMinus && stateLight) {
        stateLight = false;
        Serial.println("Blink Off : " + String(ldrRead));
        timerChecklight = millis();
        if (countStateLight >= 5) {
          sentReturn = 1;
          Serial.println("Light Blink : " + String(ldrRead));
          stateWhile = false;
        }
      } else if (millis() - timerChecklight >= 3000) {  
        if (ldrRead > ldr_set + ldrMinus) {
          sentReturn = 0;
          Serial.println("Light Off : " + String(ldrRead));
        } else {
          sentReturn = 2;
          Serial.println("Light On : " + String(ldrRead));
        } 
        stateWhile = false;
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  Serial.println("Check light result: " + String(sentReturn));
  return sentReturn;
}

// อัปเดต UI จาก pending state ที่ taskWifiMqtt ตั้งไว้ — เรียกเฉพาะใน taskDisplay เพื่อให้มีแค่ task เดียวแตะ LVGL
void applyPendingUI() {
  if (pendingUIAction == PENDING_UI_NONE) return;
  int action = pendingUIAction;
  pendingUIAction = PENDING_UI_NONE;

  switch (action) {
    case PENDING_UI_LABEL_MSG:
      lv_label_set_text(ui_Label1, pendingLabel1.c_str());
      lv_label_set_text(ui_Label2, pendingLabel2.c_str());
      break;
    case PENDING_UI_FIRST_SCREEN:
      addFlagstoFirstscreen();
      break;
    case PENDING_UI_SHOW_RUN:
      lv_obj_add_flag(ui_con_command, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_conS1, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_conS6, LV_OBJ_FLAG_HIDDEN);
      screenUse = 6;
      program = pendingProgram;
      setStartMachine();
      break;
    case PENDING_UI_RESUME_RUN:
      // กู้รอบหลังรีบูท — โชว์หน้า run แต่ไม่ setStartMachine (คง timer ที่กู้จาก snapshot)
      lv_obj_add_flag(ui_con_command, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_conS1, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_conS6, LV_OBJ_FLAG_HIDDEN);
      screenUse = 6;
      break;
    case PENDING_UI_SHUTDOWN:
      addFlagstoFirstscreen();
      lv_obj_add_flag(ui_btn_login, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_btn_wifi_setting, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_btn_pass, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(ui_Label1, pendingLabel1.c_str());
      lv_label_set_text(ui_Label2, pendingLabel2.c_str());
      break;
    case PENDING_UI_RESTART:
      addFlagstoFirstscreen();
      lv_obj_clear_flag(ui_btn_login, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_btn_wifi_setting, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_btn_pass, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(ui_Label1, pendingLabel1.c_str());
      lv_label_set_text(ui_Label2, pendingLabel2.c_str());
      break;
    case PENDING_UI_SLOT_MSG:
    case PENDING_UI_UPDATE_MSG:
    case PENDING_UI_REBOOT_MSG:
      lv_label_set_text(ui_Label1, pendingLabel1.c_str());
      lv_label_set_text(ui_Label2, pendingLabel2.c_str());
      break;
    case PENDING_UI_LDR_CLOSE:
      if (stateLdr1Screen == 1) {
        lv_obj_clear_flag(ui_conS1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_btn_login, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS5, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS6, LV_OBJ_FLAG_HIDDEN);
        screenUse = 1;
      } else if (stateLdr1Screen == 2) {
        lv_obj_add_flag(ui_conS1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_btn_login, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_conS2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS5, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS6, LV_OBJ_FLAG_HIDDEN);
        screenUse = 2;
      } else if (stateLdr1Screen == 3) {
        lv_obj_add_flag(ui_conS1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_btn_login, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_conS3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS5, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS6, LV_OBJ_FLAG_HIDDEN);
        screenUse = 3;
      } else if (stateLdr1Screen == 4) {
        lv_obj_add_flag(ui_conS1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_btn_login, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_conS4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS5, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS6, LV_OBJ_FLAG_HIDDEN);
        screenUse = 4;
      } else if (stateLdr1Screen == 5) {
        lv_obj_add_flag(ui_conS1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_btn_login, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_conS5, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS6, LV_OBJ_FLAG_HIDDEN);
        screenUse = 5;
      } else if (stateLdr1Screen == 6) {
        lv_obj_add_flag(ui_conS1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_btn_login, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS4, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_conS5, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_conS6, LV_OBJ_FLAG_HIDDEN);
        screenUse = 6;
      }
      break;
    case PENDING_UI_BT1_HOME:
      lv_obj_add_flag(ui_con_all_setting, LV_OBJ_FLAG_HIDDEN);
      addFlagstoFirstscreen();
      break;
    case PENDING_UI_BT4_SETTING:
      lv_obj_clear_flag(ui_con_all_setting, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_btn_login, LV_OBJ_FLAG_HIDDEN);
      chanel = 12;
      indexSet = 0;
      Mode1 = 0;
      break;
    case PENDING_UI_DISPLAY_SETTING:
      lv_label_set_text(ui_lb_display_setting, pendingLabel1.c_str());
      break;
    case PENDING_UI_SETTING_EXIT_COMMAND:
      lv_obj_add_flag(ui_con_all_setting, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_con_command, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text_fmt(ui_lb_mode, "mode : %d", Mode);
      lv_label_set_text_fmt(ui_lb_slot, "slot : %d", pinSlot);
      lv_label_set_text_fmt(ui_lb_setting, "ตั้งค่า : %s V%d", Noserial, gid);
      break;
    case PENDING_UI_QR_SUCCESS:
      lv_obj_add_flag(ui_conS5, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_conS6, LV_OBJ_FLAG_HIDDEN);
      pendingBalance += item_price;
      stateSentPriceServer = 1;
      revenuePersist();         // กันยอดหายถ้า reboot ก่อนส่ง
      if (status_machine_run && stateIntime) {
        stateIntime = false;
        int increaseTime = 10;
        if (item_price >= (3 * coinValue)) increaseTime = 30;
        else if (item_price >= (2 * coinValue)) increaseTime = 20;
        else if (item_price >= (1 * coinValue)) increaseTime = 10;
        minn = minn + increaseTime;
        if (minn >= 60) { hrs++; minn = minn - 60; }
      } else {
        setStartMachine();
      }
      status_countdown_wait = false;
      break;
    case PENDING_UI_QR_GEN:
      lv_obj_clean(ui_qr_frame);
      if (pendingQrPayload.length() > 0) {
        qrcode = lv_qrcode_create(ui_qr_frame, 160, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
        lv_qrcode_update(qrcode, pendingQrPayload.c_str(), pendingQrPayload.length());
        lv_obj_center(qrcode);
      }
      break;
    default:
      break;
  }
}

void taskDisplay(void *parameter){
  while (true)
  {
    hbDisplayMs = millis();  // heartbeat สำหรับ task-hang watchdog
    applyPendingUI(); // นำการอัปเดต UI จาก taskWifiMqtt มา apply ที่ task เดียว
    checkLdr1();
    checkLdr2();
    updateBalanceIncreateDry();
    count_update(); //coin active for runing
    if(!stateUpdateFw){
      Display.loop(); // Keep GUI work
    }
    // secondMillis(); //update time
    updateWiFiIcon(); //update icon
    countdownWait(); //countdown waiting doing
    prepareRunMachine(); // prepare runing
    machineRuning(); //maching runing
    CheckPromotion(); // check promotion
    // machingPrepare(); // wait for prepare machine
    
    vTaskDelay(10 / portTICK_PERIOD_MS); // Check connection every 10 seconds
  }
}
void taskProgram(void *parameter){
  // Serial.println("** task **");
  // printTocore();
  auto handleRunSessionRecoveryChannel = []() {
    RunRecoveryResult r = runSessionTickRecovery();
    if (r.action == RUN_RECOVERY_CONTINUE) {
      return;
    }
    if (r.action == RUN_RECOVERY_ABORTED) {
      chanel = 0;
      return;
    }
    if (r.action == RUN_RECOVERY_RESUMED) {
      runSessionApplySnapshot(&r.snap);
      status_machine_run = true;
      status_machine_prepare = false;
      endProgram = false;
      if (r.snap.mode == 2) {
        chanel = 0;             // อบ: machineRuning เดินเวลาโดยไม่ขึ้นกับ chanel
        Dry(1);
        Slot(1);
      } else {
        // ซัก: countdown อยู่ใน case 0 กับ step 1/2/3 (chanel อื่นเป็น action ชั่วคราวแล้ววนกลับ case 0)
        // ถ้า reboot ช่วง startup (step ยังไม่ถูกตั้ง) ให้เริ่ม sequence ใหม่ที่ chanel 1 กัน step ค้างที่ 0
        chanel = (r.snap.step == 0) ? 1 : 0;
        Slot(0);
      }
      pendingProgram = r.snap.program;
      pendingUIAction = PENDING_UI_RESUME_RUN;  // โชว์หน้า timer แทน standby หลังกู้รอบ
      stateUpdateState = 1;
    }
  };
  while (true)
  {
    hbProgramMs = millis();  // heartbeat สำหรับ task-hang watchdog
    if(millis() < timerstanby){
      timerstanby = millis();
    }
    
    if(stateReset){
      chanel = 10;
      endProgram = false;
    }

    //protect error
    if(Mode == 2 && !status_machine_run){
      Dry(0); // stop dry machine
    }else if(Mode == 2 && status_machine_run){
      Dry(1);
    }

    static LdrPeakWindow powerOnLdrPeak;
    static int count_check_power = 0;
    
    switch(chanel){
        case CH_RECOVERY:
                  handleRunSessionRecoveryChannel();
                  break;
        case 0 :  //statnby
                  if(step == 0){//
                    // setStartMachine();
                    if(!state_relay){
                      ClearRelay();
                    }
                  }else if(step == 1){//
                    if(!state_step2 && minn <= check_runing_time[0] && hrs == 0 && program != 4){
                      if(!drain_water){
                        drain_water = true;
                        pause_timer = true; 
                        chanel = 6;
                        runSessionSavePhase(RS_WASH_RUNNING);
                      }else{
                        if(millis() - timerstanby >= 75000){ //wait for drain
                          state_step2 = true;
                          drain_water = false;
                          Start();
                          vTaskDelay(1500 / portTICK_PERIOD_MS);

                          if (CodeMachine == 4)
                          { // closed
                            Power();
                            vTaskDelay(2500 / portTICK_PERIOD_MS);
                            ClearJok();
                          }

                          chanel = 8;
                          runSessionSavePhase(RS_WASH_RUNNING);
                        }
                      }
                    }
                  }else if(step == 2){//
                    if(!state_step3 && minn <= check_runing_time[1] && hrs == 0){
                      state_step3 = true;
                      if(rinStep2[1] == 2){
                        pause_timer = true; 
                        chanel = 9;
                      }else if(rinStep2[1] == 1){
                        step = 3;
                        timerstanby = millis();
                      }
                      runSessionSavePhase(RS_WASH_RUNNING);
                    }
                  }else if(step == 3){//
                    // state_step3 = false;
                    if(minn <= check_runing_time[2]){
                      static unsigned long timerEnd = millis();
                      if(millis() - timerstanby >= LDR_READ_INTERVAL_MS){
                        static LdrAvgSampler endSampler;
                        static bool endSampling = false;
                        static bool endReady = false;
                        static int ldrEnd = 0;

                        if (!endSampling && !endReady) {
                          endSampler.begin(ldrPin);
                          endSampling = true;
                        }
                        if (endSampling && endSampler.tick(&ldrEnd)) {
                          endSampling = false;
                          endReady = true;
                        }
                        if (endReady) {
                          endReady = false;
                          printLdrSummary("check ldr end program", (uint8_t)ldrPin, ldrEnd);
                          if(ldrEnd > ldr_set + ldrMinus){
                            if(millis() - timerEnd >= 3000){
                              minn_countdown_wait = 1;
                              second_countdown_wait = 0;
                              status_countdown_wait = true;
                              status_machine_run = false;
                              status_machine_prepare = false;
                              chanel = 10;
                              endProgram = true;
                            }
                          }else if(ldrEnd < ldr_set){
                            timerEnd = millis();
                          }
                          timerstanby = millis();
                        }
                      }
                    }
                  }
                  break;
        case 1 :  //power
                  Serial.println("Power is on..");
                  powerOnLdrPeak.reset();
                  Power();
                  // vTaskDelay(2500 / portTICK_PERIOD_MS);
                  chanel = 2;
                  timerstanby = millis();
                  break;
        case 2 :  //check power open?
                  if(Mode == 1){
                    int val = analogRead(ldrPin);
                    powerOnLdrPeak.push(val);
                    int peak = powerOnLdrPeak.peak();
                    static unsigned long lastPowerLdrLogMs = 0;
                    if (lastPowerLdrLogMs == 0 ||
                        (unsigned long)(millis() - lastPowerLdrLogMs) >= 3000) {
                      lastPowerLdrLogMs = millis();
                      Serial.print("check ldr power on | LDR pin=");
                      Serial.print(ldrPin);
                      Serial.print(" now=");
                      Serial.print(val);
                      Serial.print(" peak=");
                      Serial.println(peak);
                    }
                    if (val <= ldr_set || peak <= ldr_set) {
                      chanel = 3;
                      count_check_power = 0;
                    } else if(millis() - timerstanby >= 5000){
                      chanel = 1;
                      count_check_power++;
                      if(count_check_power >= 5){
                        step = 0;
                        chanel = 0; // send error power
                        count_check_power = 0;
                        lv_obj_add_flag(ui_lb_timer_machine,LV_OBJ_FLAG_HIDDEN);
                        lv_obj_clear_flag(ui_lb_state_th,LV_OBJ_FLAG_HIDDEN);
                        lv_obj_clear_flag(ui_lb_state_en,LV_OBJ_FLAG_HIDDEN);

                        lv_label_set_text(ui_lb_state_th, "เครื่องขัดข้อง");
                        lv_label_set_text(ui_lb_state_en, "Machine is problem");
                        reportEspFaultToMelody("00");
                      }
                      timerstanby = millis();
                    }
                  }else{
                    chanel = 3;
                    count_check_power = 0;
                  }
                  break;
        case 3 :  //shoot the program
                  Serial.println("setProgram..");
                  setProgram();
                  chanel = 4;
                  break;
        case 4 :  //start
                  Serial.println("start is runing..");
                  Start();
                  vTaskDelay(1500 / portTICK_PERIOD_MS);
                  chanel = 5;
                  timerstanby = millis();
                  break;
        case 5 :  //check start runing
                  // Serial.println("check ldr start runing.. " + String(analogRead(LDR1_PIN)));
                  static int count_start = 0;
                  if(Mode == 1){
                    if(checkLightStart(0, true) == 2){
                      chanel = 0;
                      if(program == 5){
                        step = 2;
                      }else if(program == 6){
                        step = 3;
                      }else{
                        step = 1;
                      }
                      count_start = 0;
                      // state_step2 = false;
                      status_machine_run = true;
                      pause_timer = false;
                      runSessionSavePhase(RS_WASH_RUNNING);
                      lv_obj_add_flag(ui_lb_state_th,LV_OBJ_FLAG_HIDDEN);
                      lv_obj_add_flag(ui_lb_state_en,LV_OBJ_FLAG_HIDDEN);
                      lv_obj_clear_flag(ui_lb_timer_machine,LV_OBJ_FLAG_HIDDEN);
                    }else if(checkLightStart(0, true) == 1){
                      chanel = 4;
                      count_start++;
                      if(count_start >= 5){
                        step = 0;
                        chanel = 0; // send error start button
                        count_start = 0;
                        lv_obj_add_flag(ui_lb_timer_machine,LV_OBJ_FLAG_HIDDEN);
                        lv_obj_clear_flag(ui_lb_state_th,LV_OBJ_FLAG_HIDDEN);
                        lv_obj_clear_flag(ui_lb_state_en,LV_OBJ_FLAG_HIDDEN);
  
                        lv_label_set_text(ui_lb_state_th, "ประตูเครื่อง ปิดไม่สนิท");
                        lv_label_set_text(ui_lb_state_en, "Check door closed");
                        reportEspFaultToMelody("02");
                      }
                    }
                  }else{
                    if(checkLightStart(0, true) == 2){
                      chanel = 0;
                      if(program == 5){
                        step = 2;
                      }else if(program == 6){
                        step = 3;
                      }else{
                        step = 1;
                      }
                      count_start = 0;
                      // state_step2 = false;
                      status_machine_run = true;
                      pause_timer = false;
                      runSessionSavePhase(RS_WASH_RUNNING);
                      lv_obj_add_flag(ui_lb_state_th,LV_OBJ_FLAG_HIDDEN);
                      lv_obj_add_flag(ui_lb_state_en,LV_OBJ_FLAG_HIDDEN);
                      lv_obj_clear_flag(ui_lb_timer_machine,LV_OBJ_FLAG_HIDDEN);
                    }else{
                      chanel = 1;
                      count_start++;
                      if(count_start >= 5){
                        step = 0;
                        chanel = 0; // send error start button
                        count_start = 0;
                        lv_obj_add_flag(ui_lb_timer_machine,LV_OBJ_FLAG_HIDDEN);
                        lv_obj_clear_flag(ui_lb_state_th,LV_OBJ_FLAG_HIDDEN);
                        lv_obj_clear_flag(ui_lb_state_en,LV_OBJ_FLAG_HIDDEN);
  
                        lv_label_set_text(ui_lb_state_th, "ตรวจสอบเครื่อง หรือประตู");
                        lv_label_set_text(ui_lb_state_en, "Check machine or door closed");
                        reportEspFaultToMelody("00");
                      }
                    }
                  }
                  break;
        case 6 :  //closed machine and on power go to step2
                  Serial.println("power off for step2..");
                  Start();
                  vTaskDelay(1500 / portTICK_PERIOD_MS);
                  Power();
                  vTaskDelay(2500 / portTICK_PERIOD_MS);
                  if (CodeMachine == 4)
                  {
                    ClearJok();
                  }
                  Power();
                  vTaskDelay(2500 / portTICK_PERIOD_MS);
                  chanel = 7;
                  break;
        case 7 :  //spin drain water
                  Serial.println("drain water is runing..");
                  if (CodeMachine == 4)
                  { // for 15kgNew
                    TempSpin();
                  }
                  else
                  {
                    Spin();
                  }
                  vTaskDelay(1000 / portTICK_PERIOD_MS);
                  Start();
                  vTaskDelay(1500 / portTICK_PERIOD_MS);
                  chanel = 0;
                  timerstanby = millis();
                  break;
        case 8 :  //step 2 for rin
                  Serial.println("rin is runing..");
                  if (CodeMachine == 4)
                  { // for 15kgNew && hier
                    Power();
                    vTaskDelay(2500 / portTICK_PERIOD_MS);
                  }
                  //------------------------------
                  
                  if(rinStep2[1] == 1){
                    for(int i = 0; i < rinStep2[0]; i++){ //select program machine
                      if (CodeMachine == 4)
                      { // for 15kgNew
                        JokBack();
                      }
                      else
                      {
                        Jok();
                      }
                    }
                    for(int i = 0; i < spin; i++){ //select program machine
                      Spin();
                    }
                  }else if(rinStep2[1] == 2){
                    for(int i = 0; i < rinStep2[0]; i++){ //select program machine
                      if (CodeMachine == 4)
                      { // for 15kgNew
                        JokBack();
                      }
                      else
                      {
                        Jok();
                      }
                    }
                  }
                  vTaskDelay(1000 / portTICK_PERIOD_MS);
                  Start();
                  vTaskDelay(1500 / portTICK_PERIOD_MS);
                  step = 2;
                  chanel = 0;
                  state_step3 = false;
                  pause_timer = false; //timer is runing
                  runSessionSavePhase(RS_WASH_RUNNING);
                  break;
        case 9 :  //closed machine and on power go to step3
                  Serial.println("power off for step3..");
                  Start();
                  vTaskDelay(1500 / portTICK_PERIOD_MS);
                  Power();
                  vTaskDelay(2500 / portTICK_PERIOD_MS);
                  if (CodeMachine == 4)
                  {
                    ClearJok();
                  }
                  Power();
                  vTaskDelay(2500 / portTICK_PERIOD_MS);
                  if (CodeMachine == 4)
                  { // for 15kgNew
                    TempSpin();
                  }
                  for(int i = 0; i < spin; i++){ //select program machine
                    Spin();
                  }
                  Start();
                  // vTaskDelay(1500 / portTICK_PERIOD_MS);
                  chanel = 0;
                  step = 3;
                  pause_timer = false; 
                  timerstanby = millis();
                  runSessionSavePhase(RS_WASH_RUNNING);
                  break;
        case 10 :  //reset system
                  runSessionClear();
                  if(!endProgram & Mode != 2){
                    if(checkLightStart(0, true) != 0){
                      Start();
                      vTaskDelay(1500 / portTICK_PERIOD_MS);
                      Power();
                    }
                  }
                  count = 0;
                  step = 0;
                  hrs = 0; minn = 0;
                  chanel = 0;
                  program = 0;
                  item_price = 0;
                  count_minn_pass = 0;
                  // priceSentVerver = 0;
                  stateReset = false;
                  status_machine_run = false;
                  status_machine_prepare = false;
                  stateIntime = false;
                  // digitalWrite(EN_PIN, LOW);
                  if (CodeMachine == 4)
                  {
                    ClearJok();
                  }
                  Slot(0);
                  Dry(0);
                  StatusControl = "off";
                  stateUpdateState = 1;
                  // go to screen 1
                  addFlagstoFirstscreen();
                  screenUse = 1;
                  Serial.println("Reset and end program..!!");
                  vTaskDelay(1000 / portTICK_PERIOD_MS);
                  break;
        case 11 :  //command
                  if(state_error == 0 || state_error == 1){
                    // free
                  }else if(state_error == 2){
                    if(timerstanby == 0 || (millis() < timerstanby || millis() - timerstanby >= 1000)){
                      int ldrVal = readLDRAverage(ldrPin, LDR_AVG_SAMPLES, "state_error LDR display");
                      lv_label_set_text_fmt(ui_lb_ldr, "ldr : %d : %d.", ldrPin, ldrVal);
                      timerstanby = millis();
                    }
                  }else if(state_error == 4){
                    // for dryer
                  }else if(state_error == 5){
                    // for update
                  }
                  break;
        case 12 : //setting
                  // Button();
                  modeSetting();
                  // delay(300);
                  break;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS); // Check connection every 10 seconds
  }
}

void set_formatted_text(lv_obj_t* textarea, const char* format, const char* ssidStr) {
  char buffer[128]; // Adjust the buffer size as needed
  snprintf(buffer, sizeof(buffer), format, ssidStr); // Format the string
  lv_textarea_set_text(textarea, buffer); // Set the text area content
}

// WiFi ทั้งหมดใน taskWifiMqtt ผ่าน connectwifi() — ไม่บล็อกใน setup()
static void timer_callback(lv_timer_t * timer) {
  // โค้ดที่ต้องการให้ทำงานเมื่อกดค้าง 5 วินาที
  stateReset = true;
  // addFlagstoFirstscreen();
  // endProgram = false;
  // chanel = 10;
  Serial.println("⚠️ reset program -> หน้าหลัก");
}
static void btn_event_cb(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
      // เริ่มจับเวลาเมื่อกดปุ่ม
      timer = lv_timer_create(timer_callback, 3000, NULL);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
      // ยกเลิกจับเวลาถ้าปล่อยปุ่มก่อน 3 วินาที
      lv_timer_del(timer);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.print("[FW] ");
  Serial.print(fwversion[0]);
  Serial.print("[FW] ");
  Serial.println(fwversion[1]);
  melodyBootMs = millis();
  Wire.begin();

  pinMode(SIG_PIN, INPUT); // กำหนดขา SIG เป็นอินพุต
  pinMode(SIG_PIN2, INPUT); // กำหนดขา SIG เป็นอินพุต
  pinMode(TX_PIN, OUTPUT); // กำหนดขา TX เป็นเอาต์พุต
  // attachInterrupt(digitalPinToInterrupt(pinSlot), pulse_in_cb, FALLING); // เปิดใช้อินเตอร์รัพท์ภายนอก
  pinMode(EN_PIN, OUTPUT); // กำหนดขา EN เป็นอินพุต
  // digitalWrite(EN_PIN, HIGH); // สั่งให้ขา EN เป็นลอจิก 1 (HIGH) เพื่อให้รับเหรียญ (12V output enable)

  pinMode(LED_Y,OUTPUT);
  digitalWrite(LED_Y, HIGH);

  pinMode(FACTORY_RESTORE_BTN_PIN, INPUT_PULLUP);  // ปุ่มคืนค่าโรงงาน (BOOT)
  setupLdrAdc(LDR1_PIN, LDR2_PIN);

  preferences.begin("config", false);  // เปิด Preferences ในโหมดเขียนได้
    bool preSetupData = preferences.isKey("SetupData");
    bool preNoserial = preferences.isKey("Noserial");
  preferences.end();
  if (!preSetupData) {
      Serial.println("⚠️ SetupData ยังไม่มีข้อมูล -> บันทึกค่าเริ่มต้น");
      // preferences.putInt("SetupData", 0);
      writePreferences();
  }
  if (!preNoserial)
  {
    EEPROM.begin(EEPROM_SIZE);
    if (tryLoadIdentityFromEeprom())
      Serial.println("📦 ย้าย ID/WiFi/gid จาก EEPROM -> NVS: " + Noserial);
    else
      Serial.println("⚠️ Noserial ไม่พบใน NVS/EEPROM -> บันทึกค่าเริ่มต้นจากโค้ด");
    writePreferencesfirst();
    EEPROM.end();
  }
  vTaskDelay(100 / portTICK_PERIOD_MS);

  Display.begin(0); // rotation number 0
  Touch.begin();
  // Sound.begin();
  // Card.begin(); // uncomment if you want to Read/Write/Play/Load file in MicroSD Card
  vTaskDelay(100 / portTICK_PERIOD_MS);
  // digitalWrite(EN_PIN, HIGH);
  // attachInterrupt(digitalPinToInterrupt(SIG_PIN), []() {
  //   pulse_trigger_flag = true;
  // }, FALLING);
  
  // Map peripheral to LVGL
  Display.useLVGL(); // Map display to LVGL
  Touch.useLVGL(); // Map touch screen to LVGL
  // Sound.useLVGL(); // Map speaker to LVGL
  // Card.useLVGL(); // Map MicroSD Card to LVGL File System

  // Display.enableAutoSleep(120); // Auto off display if not touch in 2 min
  
  // Add load your UI function
  ui_init();
  // setup ready relay
  IO_init();

  if(SetupData == 1){
    SetupData = 0;
    writePreferences();
    // PutEprom();
    Serial.println("******* Change SetupData to : " + String(SetupData) + " *******");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

  //****************************************************************** */
  // writePreferences();
  // writePreferencesfirst();
  //**************************************************************** */
  setupWaitAdminRestoreFactory();  // กดปุ่ม BOOT = คืนค่าโรงงาน (ก่อนอ่าน Preferences)
  readPreferencesfirst();
  readPreferences();
  revenueRestore(); // กู้ยอดรายรับค้าง (pendingBalance/txn) จาก NVS ถ้า reboot ก่อนส่งสำเร็จ
  if(SetupData == 1){
    SetupData = 0;
    writePreferences();
    // PutEprom();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
  Serial.println("******* SetupData to : " + String(SetupData) + " *******");
  topic = "V"+ String(gid);
  Serial.println("read from eeprom id : "+Noserial+", ssid : "+ssidStr+", pass : "+passStr+", idsharepoint : "+IDserver+", gid : "+topic);
  
  if(pinSlot != SIG_PIN && pinSlot != SIG_PIN2){
    pinSlot = SIG_PIN;
    // EEPROM.write(1, pinSlot);//1
    // EEPROM.commit();
    writePreferences();
  }

  attachInterrupt(digitalPinToInterrupt(pinSlot), pulse_in_cb, FALLING); // เปิดใช้อินเตอร์รัพท์ภายนอก
  chanel = 0;
  if (runSessionBeginRecovery(StateShutdown)) {
    chanel = CH_RECOVERY;
  }

  // ตั้งค่าโหมด WiFi ตามโปรเจกต์ใหม่
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  gNetMutex = xSemaphoreCreateRecursiveMutex();
  Serial.println(F("[WiFi] connect deferred to taskWifiMqtt (non-blocking boot)"));
  vTaskDelay(100 / portTICK_PERIOD_MS);

  //screen 1 ***********************************************************************
  //lv_obj_add_event_cb(ui_btn_reset, btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_btn_login, [](lv_event_t * e) {
    status_countdown_wait = true;
    resetwaittime();
    lv_obj_add_flag(ui_conS1,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
    screenUse = 2;
    if(Mode == 2){
      lv_obj_add_flag(ui_con_shoot,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_con_shoot1,LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text_fmt(ui_price_dry1, "%d.-", PriceShow[0]);
      lv_label_set_text_fmt(ui_price_dry2, "%d.-", PriceShow[1]);
      lv_label_set_text_fmt(ui_price_dry3, "%d.-", PriceShow[2]);
      lv_label_set_text_fmt(ui_lb_timedry1, "%d.", timerDry[0]);
      lv_label_set_text_fmt(ui_lb_timedry2, "%d.", timerDry[1]);
      lv_label_set_text_fmt(ui_lb_timedry3, "%d.", timerDry[2]);
    }else{
      lv_obj_add_flag(ui_con_shoot1,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_con_shoot,LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text_fmt(ui_price1, "%d.-", PriceShow[0]);
      lv_label_set_text_fmt(ui_price2, "%d.-", PriceShow[1]);
      lv_label_set_text_fmt(ui_price3, "%d.-", PriceShow[2]);
    }
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_add_event_cb(ui_btn_pass, [](lv_event_t * e) {
    if(!lv_obj_has_flag(ui_con_key, LV_OBJ_FLAG_HIDDEN)){
      lv_obj_add_flag(ui_con_key,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_btn_login,LV_OBJ_FLAG_HIDDEN);
    }else{
      lv_textarea_set_text(ui_tx_pin, "");
      lv_label_set_text_fmt(ui_lb_btn_command, "เข้าสู่ระบบ : %s", getDisplayCodeFromNoserial().c_str());
      lv_obj_clear_flag(ui_con_key,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_btn_login,LV_OBJ_FLAG_HIDDEN);
      // stateChange_passAdmin = false;
    }
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_add_event_cb(ui_btn_change_pass, [](lv_event_t * e) {
    lv_textarea_set_text(ui_tx_pin, "");
    lv_label_set_text(ui_lb_btn_command, "บันทึกรหัสผ่าน");
    lv_obj_clear_flag(ui_con_key,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_con_command,LV_OBJ_FLAG_HIDDEN);
    stateChange_passAdmin = true;
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_add_event_cb(ui_btn_setting, [](lv_event_t * e) {
    lv_obj_clear_flag(ui_con_all_setting,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_con_command,LV_OBJ_FLAG_HIDDEN);
    chanel = 12; indexSet = 0; Mode1 = 0;
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_add_event_cb(ui_btn_check_key, [](lv_event_t * e) {
    if(!stateChange_passAdmin){
      const char * password = lv_textarea_get_text(ui_tx_pin);
      Serial.println("passkey : " + String(password) + " " + "passAdmin : " + String(passAdmin));
      if(String(password) == passAdmin){
        lv_obj_add_flag(ui_con_key,LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_con_command,LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(ui_lb_mode, "mode : %d", Mode);
        lv_label_set_text_fmt(ui_lb_slot, "slot : %d", pinSlot);
        lv_label_set_text_fmt(ui_lb_setting, "ตั้งค่า : %s V%d", Noserial, gid);
        stateChange_passAdmin = false;
        state_error = 2;
        chanel = 11;
      }
    }else{
      const char * password = lv_textarea_get_text(ui_tx_pin);
      Serial.println("passkeyChange : " + String(password));
      passAdmin = String(password);
      preferences.begin("config", false);  // สร้างพื้นที่เก็บข้อมูลชื่อ "config"
      preferences.putString("passAdmin", passAdmin);
      preferences.end();  // ปิด Preferences
      lv_obj_add_flag(ui_con_key,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_con_command,LV_OBJ_FLAG_HIDDEN);
    }
  }, LV_EVENT_CLICKED, NULL);

  // wifi config -----------------------------------------------
  lv_obj_add_event_cb(ui_btn_wifi_setting, [](lv_event_t * e) {
    if(!lv_obj_has_flag(ui_con_wifi, LV_OBJ_FLAG_HIDDEN)){
      lv_obj_add_flag(ui_con_wifi,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_btn_login,LV_OBJ_FLAG_HIDDEN);
    }else{
      // const char* ssidStr1 = ssidStr;
      set_formatted_text(ui_tx_wifi_name, "%s", ssidStr.c_str());
      set_formatted_text(ui_tx_wifi_pass, "%s", passStr.c_str());
      lv_obj_clear_flag(ui_con_wifi,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_btn_login,LV_OBJ_FLAG_HIDDEN);
    }
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_btn_save_wifi, [](lv_event_t * e) {
    ssidStr = String(lv_textarea_get_text(ui_tx_wifi_name));
    passStr = String(lv_textarea_get_text(ui_tx_wifi_pass));
    writePreferencesfirst();

    if (g_mqttOnline)  // LVGL callback = task จอ — อ่าน cache กัน recv ซ้อน
      PublishConfigViaMqtt();

    WiFi.disconnect();
    lv_obj_add_flag(ui_con_wifi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_btn_login, LV_OBJ_FLAG_HIDDEN);
}, LV_EVENT_CLICKED, NULL);

  // button ----------------------------------------------------
  lv_obj_add_event_cb(ui_btn_back, [](lv_event_t * e) {
    BT = 1;
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_btn_Down, [](lv_event_t * e) {
    BT = 2;
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_btn_Up, [](lv_event_t * e) {
    BT = 3;
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_btn_Setting, [](lv_event_t * e) {
    BT = 4;
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_btn_Reset, [](lv_event_t * e) {
    if(Mode == 2){
      // digitalWrite(EN_PIN, LOW);
      Slot(0);
      chanel = 0;
    }else{
      endProgram = false;
      chanel = 10;
    }
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_add_event_cb(ui_btn_command_home, [](lv_event_t * e) {
    lv_obj_add_flag(ui_con_command,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_btn_login,LV_OBJ_FLAG_HIDDEN);
    stateChange_passAdmin = false;
    chanel = 10;
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_add_event_cb(ui_btn_program1, [](lv_event_t * e) {
    // status_machine_prepare = true;
    lv_obj_add_flag(ui_con_command,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_conS1,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_conS6,LV_OBJ_FLAG_HIDDEN);
    program = 1;
    chanel = 0;
    screenUse = 6;
    setStartMachine();
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_btn_program2, [](lv_event_t * e) {
    // status_machine_prepare = true;
    lv_obj_add_flag(ui_con_command,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_conS1,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_conS6,LV_OBJ_FLAG_HIDDEN);
    program = 2;
    chanel = 0;
    screenUse = 6;
    setStartMachine();
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_btn_program3, [](lv_event_t * e) {
    // status_machine_prepare = true;
    lv_obj_add_flag(ui_con_command,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_conS1,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_conS6,LV_OBJ_FLAG_HIDDEN);
    program = 3;
    chanel = 0;
    screenUse = 6;
    setStartMachine();
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_btn_program4, [](lv_event_t * e) {
    // status_machine_prepare = true;
    lv_obj_add_flag(ui_con_command,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_conS1,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_conS6,LV_OBJ_FLAG_HIDDEN);
    program = 4;
    chanel = 0;
    screenUse = 6;
    setStartMachine();
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_btn_program5, [](lv_event_t * e) {
    // status_machine_prepare = true;
    lv_obj_add_flag(ui_con_command,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_conS1,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_conS6,LV_OBJ_FLAG_HIDDEN);
    program = 5;
    chanel = 0;
    screenUse = 6;
    setStartMachine();
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_btn_program6, [](lv_event_t * e) {
    // status_machine_prepare = true;
    lv_obj_add_flag(ui_con_command,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_conS1,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_conS6,LV_OBJ_FLAG_HIDDEN);
    program = 6;
    chanel = 0;
    screenUse = 6;
    setStartMachine();
  }, LV_EVENT_CLICKED, NULL);

//************************************************************************************** */
//************************************************************************************** */

  //screen 2
  lv_obj_add_event_cb(ui_btn_back1, [](lv_event_t * e) {
    status_countdown_wait = false;
    if(stateIntime){
      stateIntime = false;
      lv_obj_add_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_conS6,LV_OBJ_FLAG_HIDDEN);
      screenUse = 6;
    }else{
      lv_obj_add_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_conS1,LV_OBJ_FLAG_HIDDEN);
      screenUse = 1;
    } 
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_add_event_cb(ui_pn_mode1, [](lv_event_t * e) {
    item_price = PriceShow[0];program = 1;
    priceSentVerver = item_price;
    lv_obj_add_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_conS3,LV_OBJ_FLAG_HIDDEN);
    screenUse = 3;
    lv_obj_set_style_bg_img_src(ui_btn_img_temp, &ui_img_asset_87_png, LV_PART_MAIN | LV_STATE_DEFAULT);
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_pn_mode2, [](lv_event_t * e) {
    item_price = PriceShow[1];program = 2;
    priceSentVerver = item_price;
    lv_obj_add_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_conS3,LV_OBJ_FLAG_HIDDEN);
    screenUse = 3;
    lv_obj_set_style_bg_img_src(ui_btn_img_temp, &ui_img_asset_88_png, LV_PART_MAIN | LV_STATE_DEFAULT);
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_pn_mode3, [](lv_event_t * e) {
    item_price = PriceShow[2];program = 3;
    priceSentVerver = item_price;
    lv_obj_add_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_conS3,LV_OBJ_FLAG_HIDDEN);
    screenUse = 3;
    lv_obj_set_style_bg_img_src(ui_btn_img_temp, &ui_img_asset_89_png, LV_PART_MAIN | LV_STATE_DEFAULT);
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_add_event_cb(ui_pn_dry1, [](lv_event_t * e) {
    if(status_machine_run && stateIntime){
      item_price = 1*coinValue;program = 7;
      priceSentVerver = item_price;
      chanelPay = 1;
      lv_obj_add_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_conS5,LV_OBJ_FLAG_HIDDEN);
      screenUse = 5;
      lv_label_set_text_fmt(ui_lb_price_qr, "%d.-", item_price);
      resetwaittime();
      mqttreqest(); //sent requese mqtt qr code
    }else{
      item_price = PriceShow[0];program = 1;
      priceSentVerver = item_price;
      lv_obj_add_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_conS3,LV_OBJ_FLAG_HIDDEN);
      screenUse = 3;
      // lv_img_set_src(ui_btn_img_temp, &ui_img_asset_96_png);
      lv_obj_set_style_bg_img_src(ui_btn_img_temp, &ui_img_asset_96_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_pn_dry2, [](lv_event_t * e) {
    if(status_machine_run && stateIntime){
      item_price = 2*coinValue;program = 7;
      priceSentVerver = item_price;
      chanelPay = 1;
      lv_obj_add_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_conS5,LV_OBJ_FLAG_HIDDEN);
      screenUse = 5;
      lv_label_set_text_fmt(ui_lb_price_qr, "%d.-", item_price);
      resetwaittime();
      mqttreqest(); //sent requese mqtt qr code
    }else{
      item_price = PriceShow[1];program = 2;
      priceSentVerver = item_price;
      lv_obj_add_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_conS3,LV_OBJ_FLAG_HIDDEN);
      screenUse = 3;
      // lv_img_set_src(ui_btn_img_temp, &ui_img_asset_96_png);
      lv_obj_set_style_bg_img_src(ui_btn_img_temp, &ui_img_asset_96_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_pn_dry3, [](lv_event_t * e) {
    if(status_machine_run && stateIntime){
      item_price = 3*coinValue;program = 7;
      priceSentVerver = item_price;
      chanelPay = 1;
      lv_obj_add_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_conS5,LV_OBJ_FLAG_HIDDEN);
      screenUse = 5;
      lv_label_set_text_fmt(ui_lb_price_qr, "%d.-", item_price);
      resetwaittime();
      mqttreqest(); //sent requese mqtt qr code
    }else{
      item_price = PriceShow[2];program = 3;
      priceSentVerver = item_price;
      lv_obj_add_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_conS3,LV_OBJ_FLAG_HIDDEN);
      screenUse = 3;
      // lv_img_set_src(ui_btn_img_temp, &ui_img_asset_96_png);
      lv_obj_set_style_bg_img_src(ui_btn_img_temp, &ui_img_asset_96_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
  }, LV_EVENT_CLICKED, NULL);

  //screen 3
  lv_obj_add_event_cb(ui_btn_back2, [](lv_event_t * e) {
    lv_obj_add_flag(ui_conS3,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
    screenUse = 2;
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_add_event_cb(ui_pn_coin, [](lv_event_t * e) {
    chanelPay = 0;
    lv_obj_add_flag(ui_conS3,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_conS4,LV_OBJ_FLAG_HIDDEN);
    screenUse = 4;
    lv_label_set_text_fmt(ui_lb_coin, "%d.-", item_price);
    //set coin active
    chanelcoinStatus = true; // set coin input status
    // digitalWrite(EN_PIN, HIGH);
    Slot(1);
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_add_event_cb(ui_pn_qr, [](lv_event_t * e) {
    chanelPay = 1;
    
    resetwaittime();
    lv_obj_add_flag(ui_conS3,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_conS5,LV_OBJ_FLAG_HIDDEN);
    screenUse = 5;
    lv_label_set_text_fmt(ui_lb_price_qr, "%d.-", item_price);
    lv_label_set_text_fmt(ui_lb_clock_qr, "%02d:%02d", minn_countdown_wait, second_countdown_wait);
    // lv_obj_del(qrcode);
    mqttreqest(); //sent requese mqtt qr code
    // String url = "value_str2";
    // qrcode = lv_qrcode_create(ui_qr_frame, 160, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
    // lv_qrcode_update(qrcode, value_str2.c_str(), value_str2.length());
    // lv_obj_center(qrcode);
  }, LV_EVENT_CLICKED, NULL);

  //screen 4
  lv_obj_add_event_cb(ui_btn_back3, [](lv_event_t * e) {
    lv_obj_add_flag(ui_conS4,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_conS3,LV_OBJ_FLAG_HIDDEN);
    screenUse = 3;
    Slot(1);
  }, LV_EVENT_CLICKED, NULL);

  //screen 5
  lv_obj_add_event_cb(ui_btn_back4, [](lv_event_t * e) {
    if(stateIntime){
      lv_obj_add_flag(ui_conS5,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
      screenUse = 2;
    }else{
      lv_obj_add_flag(ui_conS5,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_conS3,LV_OBJ_FLAG_HIDDEN);
      screenUse = 3;
    } 
  }, LV_EVENT_CLICKED, NULL);

  //screen 6
  lv_obj_add_event_cb(ui_btn_reset, btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_btn_img_temp, [](lv_event_t * e) {
    if(status_machine_run){
      if(Mode == 2){
        stateIntime = true;
        lv_obj_add_flag(ui_conS6,LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_conS2,LV_OBJ_FLAG_HIDDEN);
        screenUse = 2;
        lv_obj_add_flag(ui_con_shoot,LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_con_shoot1,LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(ui_price_dry1, "%d.-", 1*coinValue);
        lv_label_set_text_fmt(ui_price_dry2, "%d.-", 2*coinValue);
        lv_label_set_text_fmt(ui_price_dry3, "%d.-", 3*coinValue);
        lv_label_set_text_fmt(ui_lb_timedry1, "%d.", 1*coinValue);
        lv_label_set_text_fmt(ui_lb_timedry2, "%d.", 2*coinValue);
        lv_label_set_text_fmt(ui_lb_timedry3, "%d.", 3*coinValue);
        status_countdown_wait = true;
        resetwaittime();
      }
    }
  }, LV_EVENT_CLICKED, NULL);
    
  //screen 7
  // lv_obj_add_event_cb(ui_btn_home, [](lv_event_t * e) {
  //   lv_obj_add_flag(ui_conS7,LV_OBJ_FLAG_HIDDEN);
  //   lv_obj_clear_flag(ui_conS1,LV_OBJ_FLAG_HIDDEN);
  //   lv_obj_clear_flag(ui_btn_login,LV_OBJ_FLAG_HIDDEN);
  // }, LV_EVENT_CLICKED, NULL);

  //screen 8
  // lv_obj_add_event_cb(ui_btn_reset_dry, [](lv_event_t * e) {
  //   Dry(0);
  //   lv_obj_add_flag(ui_conS8,LV_OBJ_FLAG_HIDDEN);
  //   lv_obj_clear_flag(ui_conS1,LV_OBJ_FLAG_HIDDEN);
  // }, LV_EVENT_CLICKED, NULL);


  //botton command
  lv_obj_add_event_cb(ui_btn_power, [](lv_event_t * e) {
    Power();
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_btn_jok, [](lv_event_t * e) {
    Jok();
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_btn_start, [](lv_event_t * e) {
    Start();
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_btn_spin, [](lv_event_t * e) {
    Spin();
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_btn_temp, [](lv_event_t * e) {
    Temp();
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_add_event_cb(ui_btn_mode, [](lv_event_t * e) {
    Mode++;
    if(Mode >= 8){
      Mode = 1;
    }
    lv_label_set_text_fmt(ui_lb_mode, "mode : %d", Mode);
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_btn_slot, [](lv_event_t * e) {
    if(pinSlot == SIG_PIN){
      pinSlot = SIG_PIN2;
    }else{
      pinSlot = SIG_PIN;
    }
    lv_label_set_text_fmt(ui_lb_slot, "slot : %d", pinSlot);
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_btn_ldr, [](lv_event_t * e) {
    ldrPin++;
    if(ldrPin >= 3){
      ldrPin = 1;
    }
  }, LV_EVENT_CLICKED, NULL);

  // Create a task for Display on core 0
  xTaskCreate(
    taskDisplay,        // Function to implement the task
    "taskDisplay",      // Name of the task
    1024 * 4,            // Stack size in words
    NULL,             // Task input parameter
    1,                // Priority of the task
    &taskDisplay_handle             // Task handle
    // 0                 // Core where the task should run
  );
  // Create a task for Program on core 0
  xTaskCreate(
    taskProgram,        // Function to implement the task
    "taskProgram",      // Name of the task
    1024 * 5,            // Stack size in words
    NULL,             // Task input parameter
    1,                // Priority of the task
    &taskProgram_handle             // Task handle
    // 0                 // Core where the task should run
  );
  // // Create a task for Program on core 0
  xTaskCreatePinnedToCore(
    taskWifiMqtt,        // Function to implement the task
    "taskWifiMqtt",      // Name of the task
    1024 * 10,           // Stack size in words (เพิ่มสำหรับ OTA/HTTP ขนาดใหญ่)
    NULL,                // Task input parameter
    1,                   // Priority of the task
    &taskWifiMqtt_handle,// Task handle
    1                    // Core where the task should run
  );

  // // setup ready relay
  // IO_init();
  //check for shutdown mode
  if(StateShutdown == 1){
    // digitalWrite(EN_PIN, LOW); // Not recived coin
    Slot(0);
    lv_obj_add_flag(ui_btn_login,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_btn_pass,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_btn_wifi_setting,LV_OBJ_FLAG_HIDDEN);
  }

  firstGetdata = true;
}

// Update Firmware OTI*********************************************
static String otaHttpUrl(const String &path)
{
  if (port == 443)
    return "https://" + server + path;
  if (port == 80)
    return "http://" + server + path;
  return "http://" + server + ":" + String(port) + path;
}

/** fw.ma-well.com / mawell.thddns.net:4746 ใช้ไม่ได้ — OTA อยู่ที่ backend.ma-well.com:80 */
void normalizeOtaServer()
{
  server.trim();
  if (server.length() > 0 && (uint8_t)server.charAt(0) > 127)
  {
    Serial.println(F("[OTA] strip invalid host prefix"));
    int idx = 0;
    while (idx < (int)server.length() && (uint8_t)server.charAt(idx) > 127)
      idx++;
    server = server.substring(idx);
    server.trim();
  }
  const bool legacyHost =
      server.length() == 0 ||
      server == "fw.ma-well.com" ||
      server.indexOf("fw.ma-well") >= 0 ||
      server == "mawell.thddns.net" ||
      server.indexOf("thddns.net") >= 0;
  if (legacyHost)
  {
    Serial.println(F("[OTA] normalize -> backend.ma-well.com:80"));
    server = "backend.ma-well.com";
    port = 80;
  }
  if (server == "backend.ma-well.com" && port != 80 && port != 443)
    port = 80;
  host = server;
}

void suspendMachineTasksForOta()
{
  if (taskDisplay_handle != NULL)
  {
    vTaskDelete(taskDisplay_handle);
    taskDisplay_handle = NULL;
  }
  if (taskProgram_handle != NULL)
  {
    vTaskDelete(taskProgram_handle);
    taskProgram_handle = NULL;
  }
}

void restoreMachineTasksAfterOta()
{
  if (taskDisplay_handle == NULL)
  {
    xTaskCreate(taskDisplay, "taskDisplay", 1024 * 4, NULL, 1, &taskDisplay_handle);
  }
  if (taskProgram_handle == NULL)
  {
    xTaskCreate(taskProgram, "taskProgram", 1024 * 5, NULL, 1, &taskProgram_handle);
  }
  chanel = 0;
  step = 0;
  setPriceShow();
}

static void pauseMqttForOta()
{
  if (netLockEnter())
  {
    if (mqclient.connected())
    {
      Serial.println(F("[OTA] pause MQTT"));
      mqclient.disconnect();  // client.stop() อยู่ในตัวแล้ว
    }
    else
    {
      client.stop();  // ปิดครั้งเดียว — อย่า double-close (lwIP pbuf assert)
    }
    netLockLeave();
  }
  delay(50);
  yield();
}

// OTA override จากคำสั่ง HTTP poll (machines/mqtt-report) — ถ้าไม่ว่างจะแทน userID/filename เดิม
static String otaFolderOverride;
static String otaFilenameOverride;
static unsigned long lastMqttHttpReportMs = 0;
static const unsigned long MQTT_HTTP_REPORT_INTERVAL_MS = 60UL * 1000;

String postDataToServer(String server, String path, String postData)
{
  (void)server;
  if (!netLockEnter())
    return String("Can not connect to Server");
  HTTPClient http;
  const String url = otaHttpUrl(path);
  Serial.println("[OTA] HTTP POST " + url);
  http.begin(url);
  http.setTimeout(15000);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Connection", "close");
  const int code = http.POST(postData);
  Serial.println(String("[OTA] HTTP code=") + code);
  if (code <= 0)
  {
    Serial.println(String("[OTA] HTTP error: ") + http.errorToString(code));
    http.end();
    netLockLeave();
    return (code == HTTPC_ERROR_READ_TIMEOUT) ? String(">>> Client Timeout !")
                                              : String("Can not connect to Server");
  }
  if (code != HTTP_CODE_OK)
  {
    const String errBody = http.getString();
    http.end();
    netLockLeave();
    Serial.println("[OTA] HTTP body: " + errBody);
    return String("Can not connect to Server");
  }
  String payload = http.getString();
  http.end();
  netLockLeave();
  payload.trim();
  Serial.println("[OTA] version response: " + payload);
  return payload;
}

/** อ่าน HTTP stream → flash พร้อม retry (กัน writeStream หยุดเร็วเมื่อ stream/lwIP ยังไม่พร้อม) */
static size_t otaWriteStreamWithRetry(Client &stream, size_t contentLength, size_t initialWritten)
{
  size_t written = initialWritten;
  uint8_t buf[1024];
  const unsigned long deadline = millis() + 300000UL;
  unsigned long lastProgressMs = millis();
  unsigned idleRounds = 0;

  while (written < contentLength && !Update.hasError() && (long)(millis() - deadline) < 0)
  {
    const size_t remaining = contentLength - written;
    int avail = stream.available();
    if (avail <= 0)
    {
      if (!stream.connected())
      {
        Serial.print(F("[OTA] stream closed at "));
        Serial.println(written);
        break;
      }
      if (++idleRounds > 500)
      {
        Serial.print(F("[OTA] stream idle timeout at "));
        Serial.println(written);
        break;
      }
      vTaskDelay(1);
      continue;
    }
    idleRounds = 0;

    size_t chunk = (size_t)avail;
    if (chunk > sizeof(buf))
      chunk = sizeof(buf);
    if (chunk > remaining)
      chunk = remaining;

    const size_t n = stream.readBytes(buf, chunk);
    if (n == 0)
    {
      vTaskDelay(1);
      continue;
    }

    const size_t w = Update.write(buf, n);
    if (w != n)
    {
      Serial.print(F("[OTA] flash write "));
      Serial.print(w);
      Serial.print(F("/"));
      Serial.println(n);
      break;
    }
    written += w;

    const unsigned long now = millis();
    if ((unsigned long)(now - lastProgressMs) >= 15000UL)
    {
      lastProgressMs = now;
      Serial.print(F("[OTA] "));
      Serial.print(written);
      Serial.print(F("/"));
      Serial.println(contentLength);
    }
  }
  return written;
}

String getVersionByPOST(String filename)
{
  String path = Path_GetVersion;
  String uid = otaFolderOverride.length() > 0 ? otaFolderOverride : userID;
  String fn = otaFilenameOverride.length() > 0 ? otaFilenameOverride : filename;
  String verData = "user_id=" + uid + "&api_key=" + api_key + "&filename=" + fn;
  return postDataToServer(server, path, verData);
}

bool fwUpdate_OTI_POST(String filename)
{
  contentLength = 0;
  isValidContentType = false;
  String uid = otaFolderOverride.length() > 0 ? otaFolderOverride : userID;
  String fn = otaFilenameOverride.length() > 0 ? otaFilenameOverride : filename;
  String verData = "user_id=" + uid + "&api_key=" + api_key + "&filename=" + fn;

  HTTPClient http;
  const String url = otaHttpUrl(Path_OTI);
  Serial.println("[OTA] download POST " + url);
  http.begin(url);
  http.setTimeout(300000);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Connection", "close");
  const int code = http.POST(verData);
  Serial.println(String("[OTA] download HTTP=") + code);

  if (code != HTTP_CODE_OK)
  {
    const String errBody = http.getString();
    Serial.println("[OTA] download err: " + errBody);
    http.end();
    pendingLabel1 = "อัพเดทไม่ได้";
    pendingLabel2 = "Firmware not available";
    pendingUIAction = PENDING_UI_LABEL_MSG;
    data = Noserial + " gid : " + gid + " ==> อัพเดทไม่ได้ (Firmware not available)";
    sentDatatoAdmin();
    sendOtaStatusMqtt("failed", 0, "Firmware not available");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    return false;
  }

  contentLength = http.getSize();
  isValidContentType = true;
  Serial.println("Got " + String(contentLength) + " bytes from server");

  if (contentLength <= 0)
  {
    http.end();
    pendingLabel1 = "อัพเดทไม่ได้";
    pendingLabel2 = "No content";
    pendingUIAction = PENDING_UI_LABEL_MSG;
    data = Noserial + " gid : " + gid + " ==> อัพเดทไม่ได้";
    sentDatatoAdmin();
    sendOtaStatusMqtt("failed", 0, "No content");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  if (!stream)
  {
    http.end();
    return false;
  }

  if (!Update.begin(contentLength))
  {
    http.end();
    Serial.println("Not enough space to begin OTA");
    pendingLabel1 = "อัพเดทไม่ได้";
    pendingLabel2 = "พื้นที่ไม่พอ";
    pendingUIAction = PENDING_UI_LABEL_MSG;
    data = Noserial + " gid : " + gid + " ==> อัพเดทไม่ได้ (พื้นที่ไม่พอ)";
    sentDatatoAdmin();
    sendOtaStatusMqtt("failed", 0, "พื้นที่ไม่พอ");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    return false;
  }

  Serial.println("Begin OTA. This may take 2 - 5 mins to complete.");
  const size_t written = otaWriteStreamWithRetry(*stream, (size_t)contentLength, 0);
  http.end();

  if (written != (size_t)contentLength)
  {
    Serial.println("Written only : " + String(written) + "/" + String(contentLength));
    Serial.println("[OTA] Update error #: " + String(Update.getError()));
    pendingLabel1 = "อัพเดทไม่ได้";
    pendingLabel2 = "เขียนไม่ครบ";
    pendingUIAction = PENDING_UI_LABEL_MSG;
    data = Noserial + " gid : " + gid + " ==> อัพเดทไม่ได้ (เขียนไม่ครบ)";
    sentDatatoAdmin();
    sendOtaStatusMqtt("failed", 0, "เขียนไม่ครบ");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    return false;
  }

  Serial.println("Written : " + String(written) + " successfully");

  if (Update.end() && Update.isFinished())
  {
    Serial.println("Update successfully completed. Rebooting.");
    pendingLabel1 = "อัพเดทสำเร็จ..";
    pendingLabel2 = "Update successful.";
    pendingUIAction = PENDING_UI_LABEL_MSG;
    data = Noserial + " gid : " + gid + " ==> Update successful. !";
    sentDatatoAdmin();
    sendOtaStatusMqtt("success", 100, "");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    return true;
  }

  Serial.println("Error Occurred. Error #: " + String(Update.getError()));
  pendingLabel1 = "อัพเดทไม่ได้";
  pendingLabel2 = "Update error";
  pendingUIAction = PENDING_UI_LABEL_MSG;
  data = Noserial + " gid : " + gid + " ==> อัพเดทไม่ได้";
  sentDatatoAdmin();
  sendOtaStatusMqtt("failed", 0, "Update error");
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  return false;
}

String getField(String str, int fcount)
{
  int i = 0;
  char ch;
  int len = str.length();
  String str2 = "";
  while (fcount >= 1 and i < len)
  {
    ch = str[i];
    if (ch != ',' & ch != ' ')
    {
      str2 += ch;
    }
    else
    {
      fcount--;
      if (fcount > 0)
      {
        str2 = "";
      }
    }
    i++;
  }
  return (str2);
}

void otiUdate()
{
  if (!UpdateFw)
    return;

  if (!WiFi.isConnected())
  {
    Serial.println("OTI update requested but WiFi is not connected. Abort.");
    UpdateFw = false;
    pendingLabel1 = "อัพเดทไม่ได้";
    pendingLabel2 = "WiFi ไม่พร้อม";
    pendingUIAction = PENDING_UI_LABEL_MSG;
    data = Noserial + " gid : " + gid + " ==> อัพเดทไม่ได้ (WiFi ไม่พร้อม)";
    sentDatatoAdmin();
    sendOtaStatusMqtt("failed", 0, "WiFi ไม่พร้อม");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    restoreMachineTasksAfterOta();
    return;
  }

  UpdateFw = false;
  otaInProgress = true;  // งดเช็ค task-hang watchdog ระหว่าง OTA
  suspendMachineTasksForOta();
  normalizeOtaServer();
  pauseMqttForOta();
  Serial.println("\nCheck New Firmware");
  Serial.println("OTA server: " + server + ":" + String(port));
  Serial.println("OTA folder: " + String(otaFolderOverride.length() > 0 ? otaFolderOverride : userID));
  String otaFn = otaFilenameOverride.length() > 0 ? otaFilenameOverride : "firmware.bin";
  String str = getVersionByPOST(otaFn);
  Serial.println(str);

  if (str.indexOf("Timeout") >= 0 || str.indexOf("Can not connect") >= 0 || str.length() < 2)
  {
    pendingLabel1 = "อัพเดทไม่ได้";
    pendingLabel2 = "เช็คเวอร์ชันไม่สำเร็จ";
    pendingUIAction = PENDING_UI_LABEL_MSG;
    data = Noserial + " gid : " + gid + " ==> อัพเดทไม่ได้ (เช็คเวอร์ชันไม่สำเร็จ)";
    sentDatatoAdmin();
    sendOtaStatusMqtt("failed", 0, "เช็คเวอร์ชันไม่สำเร็จ");
    Serial.println("OTA version check failed. Continue normal operation.");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    restoreMachineTasksAfterOta();
    otaFolderOverride = "";
    otaFilenameOverride = "";
    otaInProgress = false;
    return;
  }

  str = getField(str, 2);
  float curVer = str.toFloat();
  str = getField(fwversion[1], 2);
  float fwVersion = str.toFloat();

  if (curVer != fwVersion)
  {
    pendingLabel1 = "Updating firmware ..";
    pendingLabel2 = "System is updating.";
    pendingUIAction = PENDING_UI_UPDATE_MSG;
    data = Noserial + " gid : " + gid + " ==> Updating firmware .. !";
    sentDatatoAdmin();
    sendOtaStatusMqtt("start", 0, "");
    pauseMqttForOta();
    Serial.println("Update Firmware");
    bool ok = fwUpdate_OTI_POST(otaFn);
    if (ok)
    {
      Serial.println("Restarting after OTA success...");
      publishPresenceOfflineGraceful();
      ESP.restart();
    }
    restoreMachineTasksAfterOta();
    otaFolderOverride = "";
    otaFilenameOverride = "";
    otaInProgress = false;
  }
  else
  {
    pendingLabel1 = "กรุณาแตะหน้าจอ เพื่อเข้าสู่ระบบ";
    pendingLabel2 = "Please Touch";
    pendingUIAction = PENDING_UI_LABEL_MSG;
    data = Noserial + " gid : " + gid + " ==> New firmware Same CurenVersion !";
    sentDatatoAdmin();
    Serial.println("New firmware Same CurenVersion ");
    sendOtaStatusMqtt("failed", 0, "เวอร์ชันตรงกัน");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    restoreMachineTasksAfterOta();
    otaFolderOverride = "";
    otaFilenameOverride = "";
    otaInProgress = false;
  }
}
//*** end of OTI ****

// ===== HTTP fallback ไป Melody backend เมื่อ MQTT ต่อไม่ได้ แต่ WiFi ใช้ได้ =====
// รายงานสถานะ MQTT (broker/rc/fail) + รับคำสั่งค้าง (เช่น OTA) ทาง HTTP
static String melodyHttpUrl(const String &path) {
  if (melodyPort == 443) return "https://" + melodyServer + path;
  if (melodyPort == 80) return "http://" + melodyServer + path;
  return "http://" + melodyServer + ":" + String(melodyPort) + path;
}

// MQTT ถือว่า "ล่มจริง" เมื่อ fail ติดกันเกิน 20 ครั้ง — ก่อนหน้านั้นให้ retry MQTT อย่างเดียว
static const int MQTT_FAIL_STREAK_FALLBACK = 20;
static bool mqttDownForFallback() {
  return !mqclient.connected() && mqttConnectFailStreak > MQTT_FAIL_STREAK_FALLBACK;
}

// ยืนยันรับคำสั่งกลับไป Melody (machines/device-ack) — เคลียร์คิวฝั่ง backend
// result (ถ้ามี) = ผลคำสั่ง เช่น diag JSON
static bool ackMelodyHttpCommand(const char *ack, const char *status, const String &result = "") {
  if (!netLockEnter())
    return false;
  HTTPClient http;
  http.begin(melodyHttpUrl(Path_DeviceAck));
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<768> doc;
  doc["api_key"] = api_key;
  doc["controller_id"] = Noserial;
  doc["ack"] = ack;
  doc["status"] = status;
  if (result.length() > 0) doc["result"] = result;
  String body;
  serializeJson(doc, body);
  int code = http.POST(body);
  http.end();
  netLockLeave();
  Serial.println(String("[HTTP] device-ack ") + ack + " code=" + code);
  return code == 200 || code == 201;
}

// สร้างผลวินิจฉัยตอบคำสั่ง diag — สถานะ MQTT/WiFi/ระบบ เพื่อให้แอดมินไล่ปัญหา
static String buildDiagResultJson() {
  StaticJsonDocument<384> doc;
  doc["rc"] = lastMqttFailRc;
  doc["fail_count"] = mqttConnectFailStreak;
  if (mqtt_server != nullptr) doc["broker"] = mqtt_server;
  doc["port"] = lastMqttFailPort > 0 ? lastMqttFailPort : mqtt_port;
  doc["mqttStatus"] = mqttStatus;
  doc["rssi"] = WiFi.RSSI();
  doc["heap"] = (uint32_t)ESP.getFreeHeap();
  doc["uptime_s"] = (uint32_t)(millis() / 1000);
  doc["fw"] = fwversion[1];
  String out;
  serializeJson(doc, out);
  return out;
}

// แปลงคำสั่งที่ backend ตอบกลับ ({"commands":[{"cmd":...}]}) — ota / diag / set_broker / reboot
static void handleMelodyPollCommands(const String &responseJson) {
  DynamicJsonDocument doc(768);
  if (deserializeJson(doc, responseJson)) {
    Serial.println("[HTTP] poll response JSON parse error");
    return;
  }
  JsonArray cmds = doc["commands"].as<JsonArray>();
  if (cmds.isNull()) return;
  const bool setupPhase = isMelodyBootSetupPhase();
  for (JsonObject c : cmds) {
    const char *cmd = c["cmd"];
    if (!cmd) continue;
    if (setupPhase && strcmp(cmd, "diag") != 0) {
      Serial.println(String("[HTTP] skip command during boot/setup: ") + cmd);
      continue;
    }
    if (strcmp(cmd, "ota") == 0) {
      otaFolderOverride = c["folder"].as<String>();
      otaFilenameOverride = c["filename"] | "firmware.bin";
      Serial.println("[HTTP] command OTA folder=" + otaFolderOverride + " file=" + otaFilenameOverride);
      ackMelodyHttpCommand("ota", "accepted");
      UpdateFw = true;
    } else if (strcmp(cmd, "diag") == 0) {
      // ตอบข้อมูลวินิจฉัยกลับทันที — backend เก็บใน httpDiagResult
      Serial.println("[HTTP] command diag");
      ackMelodyHttpCommand("diag", "ok", buildDiagResultJson());
    } else if (strcmp(cmd, "set_broker") == 0) {
      int newStatus = c["mqttStatus"] | 0;
      if (newStatus == 1 || newStatus == 2) {
        Serial.println("[HTTP] command set_broker -> mqttStatus=" + String(newStatus));
        mqttStatus = newStatus;
        // บันทึกถาวร — boot หน้าก็ใช้ broker ใหม่
        if (preferences.begin("config", false)) {
          preferences.putInt("mqttStatus", mqttStatus);
          preferences.end();
        }
        ackMelodyHttpCommand("set_broker", "accepted");
        // reset ตัวนับ ให้ mqttreconnect ลอง broker ใหม่ทันที
        mqttConnectFailStreak = 0;
        lastMqttFailRc = 0;
        lastMqttFailPort = 0;
        if (netLockEnter()) {
          mqclient.disconnect();
          netLockLeave();
        }
      } else {
        ackMelodyHttpCommand("set_broker", "failed");
      }
    } else if (strcmp(cmd, "reboot") == 0) {
      Serial.println("[HTTP] command reboot");
      ackMelodyHttpCommand("reboot", "accepted");
      vTaskDelay(500 / portTICK_PERIOD_MS);
      publishPresenceOfflineGraceful();
      ESP.restart();
    } else if (strcmp(cmd, "run") == 0) {
      // สั่งเริ่มโปรแกรมผ่าน HTTP เมื่อ MQTT ล่ม — ใช้เส้นทางเดียวกับคำสั่ง MQTT (commandApp)
      int prog = c["program"] | 0;
      if (prog >= 1 && prog <= 7 && !status_machine_prepare && !status_machine_run) {
        Serial.println("[HTTP] command run program=" + String(prog));
        cm = "cmProgram";
        value_str1 = String(gid);
        value_str2 = String(prog);
        commandApp();
        ackMelodyHttpCommand("run", "accepted");
      } else {
        Serial.println("[HTTP] command run rejected (busy/invalid) program=" + String(prog));
        ackMelodyHttpCommand("run", "failed");
      }
    } else if (strcmp(cmd, "stop") == 0) {
      // สั่งหยุด/รีเซ็ตผ่าน HTTP — value_str2=0 คือ reset (เหมือนคำสั่ง MQTT)
      Serial.println("[HTTP] command stop");
      cm = "cmProgram";
      value_str1 = String(gid);
      value_str2 = "0";
      commandApp();
      ackMelodyHttpCommand("stop", "accepted");
    }
  }
}

// ส่ง UpdateState ทาง HTTP — ใช้เมื่อ MQTT ล่มจริง (ลูกค้าใช้เครื่องระหว่าง MQTT ล่ม)
// เรียกหลัง SetStatusControl()/SetTimerSend() แล้ว (StatusControl/TimeSent พร้อมใช้)
static bool sendUpdateStateHttp() {
  if (!wifiLinkUsable()) return false;
  if (!netLockEnter()) return false;
  HTTPClient http;
  http.begin(melodyHttpUrl(Path_UpdateState));
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<320> doc;
  doc["api_key"] = api_key;
  doc["controller_id"] = Noserial;
  doc["ID"] = IDserver;
  doc["Status"] = StatusControl;
  doc["Time"] = TimeSent;
  String body;
  serializeJson(doc, body);
  int code = http.POST(body);
  http.end();
  netLockLeave();
  Serial.println(String("[HTTP] update-state code=") + code + " :: " + StatusControl + " :: " + TimeSent);
  return code == 200 || code == 201;
}

// ===== รายรับทาง HTTP (idempotent) — แทน MQTT postSQL เพื่อไม่ให้ข้อมูลหายตอน MQTT flap =====
// ใช้ Preferences object แยก (namespace "revenue") ต่อครั้ง — NVS driver ล็อกภายใน จึงปลอดภัยข้าม task
static void revenuePersist()
{
  Preferences p;
  if (!p.begin("revenue", false))
    return;
  p.putInt("pendBal", pendingBalance);
  p.putInt("sendAmt", revSendingAmount);
  p.putString("sendTxn", revSendingTxn);
  p.putULong("txnSeq", revTxnSeq);
  p.end();
}

static void revenueRestore()
{
  Preferences p;
  if (!p.begin("revenue", true))
    return;
  pendingBalance = p.getInt("pendBal", 0);
  revSendingAmount = p.getInt("sendAmt", 0);
  revSendingTxn = p.getString("sendTxn", "");
  revTxnSeq = p.getULong("txnSeq", 0);
  p.end();
  if (pendingBalance > 0 || revSendingAmount > 0)
  {
    stateSentPriceServer = 1; // มียอดค้างจากก่อน reboot — ส่งต่อ (idempotent จาก txn เดิม)
    Serial.println("[REV] restore pending=" + String(pendingBalance) +
                   " sending=" + String(revSendingAmount) + " txn=" + revSendingTxn);
  }
}

// txnId ไม่ซ้ำต่อเครื่อง: <Noserial>-<seq> (seq persistent) — retry ใช้ค่าเดิมเพื่อ idempotent
static String revenueMakeTxnId()
{
  revTxnSeq++;
  return Noserial + "-" + String(revTxnSeq);
}

// ส่งรายรับทาง HTTP device-revenue — คืน true เมื่อ backend รับ (created หรือ duplicate = idempotent)
static bool sendRevenueHttp(int amount, const String &txnId, const char *source)
{
  if (!wifiLinkUsable())
    return false;
  if (!netLockEnter())
    return false;
  HTTPClient http;
  http.begin(melodyHttpUrl(Path_DeviceRevenue));
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "close");
  StaticJsonDocument<256> doc;
  doc["api_key"] = api_key;
  doc["controller_id"] = Noserial;
  doc["amount"] = amount;
  doc["source"] = source;
  doc["txnId"] = txnId;
  String body;
  serializeJson(doc, body);
  int code = http.POST(body);
  http.end();
  netLockLeave();
  Serial.println(String("[REV] http device-revenue code=") + code + " amount=" + amount + " txn=" + txnId);
  return code == 200 || code == 201;
}

// เรียกตอน WiFi ต่อแต่ MQTT ล่มจริง (fail > 20 ครั้ง) — POST machines/mqtt-report (throttle ทุก 1 นาที)
bool pollMelodyDeviceHttp() {
  if (isMelodyBootSetupPhase()) return false;
  if (!wifiLinkUsable() || !mqttDownForFallback()) return false;

  unsigned long nowMs = millis();
  if (lastMqttHttpReportMs != 0 &&
      (unsigned long)(nowMs - lastMqttHttpReportMs) < MQTT_HTTP_REPORT_INTERVAL_MS)
    return false;

  if (!netLockEnter()) return false;
  HTTPClient http;
  http.begin(melodyHttpUrl(Path_MqttReport));
  http.setTimeout(10000);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<384> doc;
  doc["api_key"] = api_key;
  doc["controller_id"] = Noserial;
  doc["event"] = "mqtt_connect_fail";
  doc["rc"] = lastMqttFailRc;
  if (mqtt_server != nullptr) doc["broker"] = mqtt_server;
  doc["port"] = lastMqttFailPort > 0 ? lastMqttFailPort : mqtt_port;
  doc["mqttStatus"] = mqttStatus;
  doc["fail_count"] = mqttConnectFailStreak;
  doc["wifi_rssi"] = WiFi.RSSI();

  String body;
  serializeJson(doc, body);
  int code = http.POST(body);
  String response = http.getString();
  http.end();
  netLockLeave();

  if (code == 200 || code == 201) {
    lastMqttHttpReportMs = nowMs;
    Serial.println(String("[HTTP] device-poll ok code=") + code);
    if (response.length() > 0) handleMelodyPollCommands(response);
    return true;
  }
  Serial.println(String("[HTTP] device-poll fail code=") + code);
  return false;
}

void taskWifiMqtt(void *parameter){
  while (true){
    hbWifiMs = millis();  // heartbeat สำหรับ task-hang watchdog
    g_mqttOnline = mqclient.connected();  // cache ให้ task จอ อ่าน (กัน recv ซ้อน -> pbuf crash)
    // pump keepalive ต้นรอบด้วย lock สั้น — รับประกัน mqclient.loop() ถูกเรียกสม่ำเสมอ
    // (กันกรณี deferred work ติด lock นาน -> PINGRESP ไม่ถูกอ่าน -> drop rc=-4)
    if (mqclient.connected())
    {
      if (netLockTryEnter(30))
      {
        mqclient.loop();
        netLockLeave();
      }
    }
    if (state_wifi_on){
      otiUdate();
      httpJobStep(); // ทำ HTTP jobs ทีละขั้นแบบ non-blocking

      const unsigned long wifiNow = millis();
      if (espWifiAssociated()) {
        if (wifiConnectedSinceMs == 0)
          noteWifiLinkUp(true);

        if (mqclient.connected() && !wifiLinkUsable()) {
          if (netLockEnter()) {
            mqttPumpLoopLocked(1);
            netLockLeave();
          }
        }

        statewifi = wifiLinkUsable();
        if (!statewifi) {
          static unsigned long lastWifiWarmupLogMs = 0;
          if (lastWifiWarmupLogMs == 0 ||
              (unsigned long)(wifiNow - lastWifiWarmupLogMs) >= 2000) {
            lastWifiWarmupLogMs = wifiNow;
            if (wifiConnectedSinceMs == 0) {
              Serial.println(F("[WiFi] connected but warmup not started; fixing next loop"));
            } else {
              unsigned long remain = wifiStableWindowMs();
              if ((unsigned long)(wifiNow - wifiConnectedSinceMs) < remain)
                remain -= (unsigned long)(wifiNow - wifiConnectedSinceMs);
              else
                remain = 0;
              Serial.print(F("[WiFi] warming up; postpone MQTT/HTTP ~"));
              Serial.print(remain);
              Serial.println(F("ms"));
            }
          }
        } else {
          mqttreconnect();
        }

        if (!mqclient.connected() && statewifi) {
          pollMelodyDeviceHttp();
        }

        static bool melodySetupPhaseEnded = false;
        if (!melodySetupPhaseEnded && !isMelodyBootSetupPhase()) {
          melodySetupPhaseEnded = true;
          stateUpdateState = 1;
          Serial.println(F("[Melody] boot/setup complete — sync UpdateState"));
        }

        // ส่ง configRequest เฉพาะเมื่อ MQTT เชื่อมต่อแล้ว และเป็นครั้งแรกหรือมีการขอ GetData
        if(stateGetdata || firstGetdata){
          if (mqclient.connected()) {
            stateGetdata = false;
            firstGetdata = false;
            GetData();  // ส่ง configRequest ครั้งเดียว แล้วรอ configResponse
          }
          // ถ้า MQTT ยังไม่ต่อ ยังไม่ล้าง firstGetdata จะรอรอบถัดไปจนกว่า MQTT จะเชื่อมต่อ
        }

        if (isMelodyBootSetupPhase() && bootMelodySyncQuietReady()) {
          if (bootMelodyConfigPending) {
            bootMelodyConfigPending = false;
            Serial.println(F("[MQTT] ✅ ใช้ configResponse ชุดสุดท้ายจาก Melody -> GetSetupData()"));
            GetSetupData();
          }
        }

        if(stateSetupdata){
          stateSetupdata = false;
          GetSetupData();
        }

        if (stateSendConfigMqtt && mqclient.connected() && wifiLinkUsable()) {
          stateSendConfigMqtt = false;
          PublishConfigViaMqtt();
        }

        if(stateUpdateState){
          if (mqclient.connected() && wifiLinkUsable()) {
            stateUpdateState = false;
            SetStatusControl();
            SetTimerSend();
            String msg = "{\"ID\":\"" + IDserver + "\",\"Title\":\"" + Noserial + "\",\"Status\":\"" + StatusControl + "\",\"Time\":\"" + TimeSent + "\"}";
            msg.toCharArray(pendingUpdateStateBuf, sizeof(pendingUpdateStateBuf));
            pendingUpdateStatePublish = true;
            Serial.println("state update status and time ..!! :: " + StatusControl + " :: " + TimeSent);
          } else if (mqttDownForFallback() && !isMelodyBootSetupPhase()) {
            // MQTT ล่มจริง (fail > 20 ครั้ง) → ส่งสถานะทาง HTTP แทน เพื่อให้ Melody เห็นลูกค้าใช้เครื่อง
            // ส่งสำเร็จค่อยเคลียร์ flag — ถ้า HTTP ก็ fail จะ retry ทุก 30 วิ (ไม่ทิ้งสถานะ)
            static unsigned long lastUpdateStateHttpMs = 0;
            if (lastUpdateStateHttpMs == 0 ||
                (unsigned long)(millis() - lastUpdateStateHttpMs) >= 30UL * 1000) {
              lastUpdateStateHttpMs = millis();
              SetStatusControl();
              SetTimerSend();
              if (sendUpdateStateHttp()) {
                stateUpdateState = false;
              }
            }
          }
          // MQTT หลุดแต่ยังไม่เกิน 20 ครั้ง → คง flag ไว้ รอ mqttreconnect ก่อน (MQTT-first)
        }

        static unsigned long lastRevSendMs = 0;
        if (stateSentPriceServer && pendingBalance > 0 &&
            (lastRevSendMs == 0 || (unsigned long)(millis() - lastRevSendMs) >= 4000)){
          lastRevSendMs = millis();
          if (statewifi){
            // ส่งรายรับทาง HTTP เป็นหลัก (idempotent ด้วย txnId) — ไม่หายแม้ MQTT flap
            // freeze batch ปัจจุบัน (amount+txnId) ครั้งเดียว: coin ที่หยอดเพิ่มระหว่างส่งจะไปอยู่ batch ถัดไป
            // throttle 4s: กันยิง HTTP ถี่จนถือ net lock บ่อย -> mqclient.loop() ขาด (กัน MQTT flap แย่ลง)
            if (revSendingTxn == "" || revSendingAmount <= 0) {
              revSendingAmount = pendingBalance;
              revSendingTxn = revenueMakeTxnId();
              revenuePersist();
            }
            bool ok = sendRevenueHttp(revSendingAmount, revSendingTxn, "coin");
            if (ok){
              pendingBalance -= revSendingAmount;
              if (pendingBalance < 0)
                pendingBalance = 0;
              Serial.println("[REV] sent OK amount=" + String(revSendingAmount) + " remain=" + String(pendingBalance));
              revSendingAmount = 0;
              revSendingTxn = "";
              if (pendingBalance == 0)
                stateSentPriceServer = false;
              revenuePersist();
            }else{
              Serial.println("[REV] send failed, will retry (txn=" + revSendingTxn + ")");
            }
          }
        }

        static bool midnight = false;
        if (rtc.getHour(true) == 0 && rtc.getMinute() == 0 && !midnight) {
          midnight = true;
          Serial.println("It's midnight!");
          setupTime();
        }else if (rtc.getHour(true) == 0 && rtc.getMinute() == 1 && midnight){
          midnight = false;
        }

        if (mqclient.connected() && statewifi) {
          static unsigned long lastPresenceHeartbeatMs = 0;
          if (lastPresenceHeartbeatMs == 0)
            lastPresenceHeartbeatMs = millis();
          if ((unsigned long)(millis() - lastPresenceHeartbeatMs) >= 5UL * 60 * 1000) {
            pendingPresenceHeartbeat = true;
            lastPresenceHeartbeatMs = millis();
          }
          processDeferredMqttWork();
        }
      } else if (espWifiDownConfirmed(wifiNow)) {
        statewifi = false;
        if (wifiConnectedSinceMs != 0 || mqclient.connected())
          teardownMqttOnWifiDown();
        wifiConnectedSinceMs = 0;
        connectwifi();
      } else {
        if (mqclient.connected()) {
          if (netLockEnter()) {
            mqttPumpLoopLocked(1);
            netLockLeave();
          }
        }
        connectwifi();
      }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// เฝ้าดู task ค้าง (WiFi/MQTT/จอชนกัน หรือ LVGL ค้าง) — รีบูทกู้ตัวเอง
// run_session (v3.42+) จะกู้รอบซัก/อบต่อหลังรีบูท จึงปลอดภัยที่จะรีสตาร์ตแม้กำลังทำงาน
static void checkTaskHang()
{
  unsigned long now = millis();

  if (melodyBootMs > 0 &&
      (unsigned long)(now - melodyBootMs) < BOOT_HANG_GRACE_MS)
  {
    return;
  }

  // ระหว่าง OTA: machine task ถูกลบ + wifi task ดาวน์โหลดบล็อกนาน -> reset heartbeat กัน false trigger
  // ใช้ otaInProgress (เคลียร์แน่นอนทุก path) — ไม่พึ่ง stateUpdateFw ที่อาจค้าง true หลัง OTA fail
  if (otaInProgress || UpdateFw)
  {
    hbDisplayMs = hbProgramMs = hbWifiMs = now;
    return;
  }

  // เครื่องกำลังทำงาน/เตรียม -> ห้ามรีบูทเองเด็ดขาด (ตรงพฤติกรรม 3.00 ที่ไม่มี watchdog)
  // กันตัดกลางรอบจาก false-trigger; feed heartbeat กันค้างสะสมแล้วรีบูทหลังจบงาน
  if (status_machine_run || status_machine_prepare)
  {
    hbDisplayMs = hbProgramMs = hbWifiMs = now;
    return;
  }

  // heap ต่ำวิกฤติต่อเนื่อง (fragmentation ระยะยาว) -> รีบูทกัน alloc fail/crash
  static unsigned long lowHeapSinceMs = 0;
  if (ESP.getFreeHeap() < LOW_HEAP_CRITICAL_BYTES)
  {
    if (lowHeapSinceMs == 0)
      lowHeapSinceMs = now;
    else if (hangElapsedMs(now, lowHeapSinceMs, LOW_HEAP_REBOOT_MS))
    {
      Serial.print(F("[WDT] low heap -> restart, free="));
      Serial.println((uint32_t)ESP.getFreeHeap());
      Serial.flush();
      vTaskDelay(100 / portTICK_PERIOD_MS);
      ESP.restart();
    }
  }
  else
  {
    lowHeapSinceMs = 0;
  }

  const char *stuck = nullptr;
  if (taskDisplay_handle != NULL && hbDisplayMs != 0 &&
      hangElapsedMs(now, hbDisplayMs, TASK_HANG_TIMEOUT_MS))
    stuck = "taskDisplay";
  else if (taskProgram_handle != NULL && hbProgramMs != 0 &&
           hangElapsedMs(now, hbProgramMs, TASK_HANG_TIMEOUT_MS))
    stuck = "taskProgram";
  else if (taskWifiMqtt_handle != NULL && hbWifiMs != 0 &&
           hangElapsedMs(now, hbWifiMs, WIFI_TASK_HANG_TIMEOUT_MS))
    stuck = "taskWifiMqtt";

  if (stuck != nullptr)
  {
    Serial.print(F("[WDT] task hang detected -> restart: "));
    Serial.print(stuck);
    Serial.print(F(" now="));
    Serial.print(now);
    Serial.print(F(" hbDisplay="));
    Serial.print(hbDisplayMs);
    Serial.print(F(" hbProgram="));
    Serial.print(hbProgramMs);
    Serial.print(F(" hbWifi="));
    Serial.println(hbWifiMs);
    Serial.flush();
    vTaskDelay(100 / portTICK_PERIOD_MS);
    ESP.restart();
  }
}

void loop() {
  if(stateUpdateFw){
    applyPendingUI(); // แสดงข้อความจาก commandApp/otiUdate หลังลบ taskDisplay
    Display.loop();   // Keep GUI work ระหว่าง OTA
  }
  // เช็คทุก 10 วิ (ไม่ต้องถี่ 1 วิ) ลดโอกาสรีบูทเอง; ตอนเครื่องทำงาน checkTaskHang จะไม่รีบูทอยู่แล้ว
  static unsigned long lastHangCheckMs = 0;
  unsigned long now = millis();
  if ((unsigned long)(now - lastHangCheckMs) >= 10000)
  {
    lastHangCheckMs = now;
    checkTaskHang();
  }
  vTaskDelay(10 / portTICK_PERIOD_MS);
}

void modeSetting(){
  switch (indexSet)
      {
      case 0: settingMode1();  
              break;
      case 1: settingMode2();  
              break;
      case 2: settingMode3();  
              break;
      case 3: Drysetting();  
              break;
      case 4: Anothersetting();  
              break;
      // case 5: Test1(); 
      //         break;
      // case 6: CommandProgram(); 
      //         break;
      }
}
void settingMode1(){
    static bool stateMode1 = true;
    if(stateMode1){
      stateMode1 = false;
      if(Mode1 == 0){
        lv_label_set_text_fmt(ui_lb_display_setting, "Program => 1");
      }else if(Mode1 == 1){
        lv_label_set_text_fmt(ui_lb_display_setting, "Program => 2");
      }else if(Mode1 == 2){
        lv_label_set_text_fmt(ui_lb_display_setting, "Program => 3");
      }else if(Mode1 == 3){
        lv_label_set_text_fmt(ui_lb_display_setting, "Drum Clean");
      }else if(Mode1 == 4){
        lv_label_set_text_fmt(ui_lb_display_setting, "Another Setting");
      }else if(Mode1 == 5){
        lv_label_set_text_fmt(ui_lb_display_setting, "Program Dry");
      }
    }
  //Button();
  if(BT == 3){
    stateMode1 = true;
    BT = 0;
    Mode1++;
    if(Mode1 >= 6){
      Mode1 = 0;
    }
  }else if(BT == 2){
    stateMode1 = true;
    BT = 0;
    Mode1--;
    if(Mode1 <= -1){
      Mode1 = 5;
    }
  }else if(BT == 4){
    stateMode1 = true;
    BT = 0;
    if(Mode1 == 4){
      indexSet = 4;
    }else if(Mode1 == 5){
      indexSet = 3;
    }else{
      Serial.println("Mode1 => " + String(Mode1));
      indexSet = 1;
      Mode2 = 0;
    }
  }else if(BT == 1){
    // stateMode1 = true;
    BT = 0;
    Mode1 = 0;
    chanel = 0;
    lv_obj_add_flag(ui_con_all_setting,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_con_command,LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(ui_lb_mode, "mode : %d", Mode);
    lv_label_set_text_fmt(ui_lb_slot, "slot : %d", pinSlot);
    lv_label_set_text_fmt(ui_lb_setting, "ตั้งค่า : %s V%d", Noserial, gid);
    stateChange_passAdmin = false;
    state_error = 2;
    chanel = 11;
    Display.loop();
  }
}
void settingMode2(){
  if(Mode2 == 0){ 
    lv_label_set_text(ui_lb_display_setting, "Price");
  }else if(Mode2 == 1){
    lv_label_set_text(ui_lb_display_setting, "Jok Program");
  }else if(Mode2 == 2){
    lv_label_set_text(ui_lb_display_setting, "Temp");
  }else if(Mode2 == 3){
    lv_label_set_text(ui_lb_display_setting, "State Jok Program");
  }else if(Mode2 == 4){
    lv_label_set_text(ui_lb_display_setting, "Time of Program Hrs");    
  }else if(Mode2 == 5){
    lv_label_set_text(ui_lb_display_setting, "Time of Program Minn");    
  }

  //Button();
  if(BT == 3){
    BT = 0;
    Mode2++;
    if(Mode2 >= 6){
      Mode2 = 0;
    }
  }else if(BT == 2){
    BT = 0;
    Mode2--;
    if(Mode2 <= -1){
      Mode2 = 5;
    }
  }else if(BT == 4){
    BT = 0;
    if(Mode1 == 0){
      Price = price[0];
      for(int i=0 ; i<3 ;i++){
        R[i] = program1[i];
      }
      for(int i=0 ; i<2 ;i++){
        T[i] = TimeCountdown1[i];
      }
    }else if(Mode1 == 1){
      Price = price[1];
      for(int i=0 ; i<3 ;i++){
        R[i] = program2[i];
      }
      for(int i=0 ; i<2 ;i++){
        T[i] = TimeCountdown2[i];
      }
    }else if(Mode1 == 2){
      Price = price[2];
      for(int i=0 ; i<3 ;i++){
        R[i] = program3[i];
      }
      for(int i=0 ; i<2 ;i++){
        T[i] = TimeCountdown3[i];
      }
    }else if(Mode1 == 3){ //drum clean
      Price = 0;
      for(int i=0 ; i<3 ;i++){
        R[i] = drum[i];
      }
      for(int i=0 ; i<2 ;i++){
        T[i] = TimeCountdowndrum[i];
      }
    }
    indexSet = 2;
  }else if(BT == 1){
    BT = 0;
    indexSet = 0;
  }
}
void settingMode3(){
  if(Mode2 == 0){
    lv_label_set_text_fmt(ui_lb_display_setting, "Price : %d", Price);
  }else if(Mode2 == 1){
    lv_label_set_text_fmt(ui_lb_display_setting, "Jok program : %d", R[0]);
  }else if(Mode2 == 2){;
    lv_label_set_text_fmt(ui_lb_display_setting, "Temp : %d", R[1]);
  }else if(Mode2 == 3){
    lv_label_set_text_fmt(ui_lb_display_setting, "State jok program : %d", R[2]);
  }else if(Mode2 == 4){
    lv_label_set_text_fmt(ui_lb_display_setting, "Time of program Hrs : %2d", T[0]);
  }else if(Mode2 == 5){
    lv_label_set_text_fmt(ui_lb_display_setting, "Time of program Minn : %2d", T[1]);
  }

  //Button();
  if(BT == 3){
    BT = 0;
    if(Mode2 == 0){
      Price = Price+10;
      if(Price >= 110){
        Price = 0;
      }
    }else if(Mode2 == 1){
      R[Mode2-1]++;
      if(R[Mode2-1] >= 16){
        R[Mode2-1] = 0;
      }
    }else if(Mode2 == 2){
      R[Mode2-1]++;
      if(R[Mode2-1] >= 16){
        R[Mode2-1] = 0;
      }
    }else if(Mode2 == 3){
      R[Mode2-1]++;
      if(R[Mode2-1] >= 16){
        R[Mode2-1] = 0;
      }
    }else if(Mode2 == 4){
      T[0]++;
      if(T[0] >= 5){
        T[0] = 0;
      }
    }else if(Mode2 == 5){
      T[1] = T[1] + 5;
      if(T[1] >= 60){
        T[1] = 0;
      }
    }
  }else if(BT == 2){
    BT = 0;
    if(Mode2 == 0){
      Price = Price-10;
      if(Price <= -10){
        Price = 100;
      }
    }else if(Mode2 == 1){
      R[Mode2-1]--;
      if(R[Mode2-1] <= -1){
        R[Mode2-1] = 15;
      }
    }else if(Mode2 == 2){
      R[Mode2-1]--;
      if(R[Mode2-1] <= -1){
        R[Mode2-1] = 15;
      }
    }else if(Mode2 == 3){
      R[Mode2-1]--;
      if(R[Mode2-1] <= -1){
        R[Mode2-1] = 15;
      }
    }else if(Mode2 == 4){
      T[0]--;
      if(T[0] <= -1){
        T[0] = 5;
      }
    }else if(Mode2 == 5){
      T[1] = T[1] - 5;
      if(T[1] <= -5){
        T[1] = 55;
      }
    }
  }else if(BT == 1){
    BT = 0;
    if(Mode1 == 0){
      price[0] = Price;
      for(int i=0 ; i<3 ;i++){
        program1[i] = R[i];
      }
      for(int i=0 ; i<2 ;i++){
        TimeCountdown1[i] = T[i];
      }
    }else if(Mode1 == 1){
      price[1] = Price;
      for(int i=0 ; i<3 ;i++){
        program2[i] = R[i];
      }
      for(int i=0 ; i<2 ;i++){
        TimeCountdown2[i] = T[i];
      }
    }else if(Mode1 == 2){
      price[2] = Price;
      for(int i=0 ; i<3 ;i++){
        program3[i] = R[i];
      }
      for(int i=0 ; i<2 ;i++){
        TimeCountdown3[i] = T[i];
      }
    }else if(Mode1 == 3){
      // price[1] = Price;
      for(int i=0 ; i<3 ;i++){
        drum[i] = R[i];
      }
      for(int i=0 ; i<2 ;i++){
        TimeCountdowndrum[i] = T[i];
      }
    }
    // PutEprom(); // Save
    writePreferences();
    indexSet = 1;
  }
}
void Anothersetting(){
  static String str = "Send data to admin";
  if(Mode2 == 0){
    lv_label_set_text_fmt(ui_lb_display_setting, "Rin Step 2 : %2d", rinStep2[0]);
  }else if(Mode2 == 1){
    lv_label_set_text_fmt(ui_lb_display_setting, "State rin Step 2 : %2d", rinStep2[1]);
  }else if(Mode2 == 2){
    lv_label_set_text_fmt(ui_lb_display_setting, "Rin command : %2d", rincommand[0]);
  }else if(Mode2 == 3){
    lv_label_set_text_fmt(ui_lb_display_setting, "State rin command : %2d", rincommand[1]);
  }else if(Mode2 == 4){
    lv_label_set_text_fmt(ui_lb_display_setting, "Spin : %2d", spin);
  }else if(Mode2 == 5){
    lv_label_set_text_fmt(ui_lb_display_setting, "Check time 1 : %2d", check_runing_time[0]);
  }else if(Mode2 == 6){
    lv_label_set_text_fmt(ui_lb_display_setting, "Check time 2 : %2d", check_runing_time[1]);
  }else if(Mode2 == 7){
    lv_label_set_text_fmt(ui_lb_display_setting, "Check time 3 : %2d", check_runing_time[2]);
  }else if(Mode2 == 8){
    lv_label_set_text_fmt(ui_lb_display_setting, "Ldr set : %d", ldr_set);
  }else if(Mode2 == 9){
    lv_label_set_text_fmt(ui_lb_display_setting, "Ldr set : %d", ldrMinus);
  }else if(Mode2 == 10){
    lv_label_set_text_fmt(ui_lb_display_setting, "Mqtt state : %2d", mqttStatus);
  }else if(Mode2 == 11){
    lv_label_set_text_fmt(ui_lb_display_setting, "Mqtt state : %2d", CodeMachine);
  }else if(Mode2 == 12){
    lv_label_set_text_fmt(ui_lb_display_setting, "Mqtt state : %2d", pinSlot);
  }else if(Mode2 == 13){
    lv_label_set_text(ui_lb_display_setting, str.c_str());
  }else if(Mode2 == 14){
    lv_label_set_text(ui_lb_display_setting, "Mode update firmware");
  }

  //Button();
  if(BT == 4){
    BT = 0;
    Mode2++;
    if(Mode2 >= 11){
      Mode2 = 0;
    }
  }else if(BT == 3){
    BT = 0;
    if(Mode2 == 0){
      rinStep2[0]++;
    }else if(Mode2 == 1){
      rinStep2[1]++;
    }else if(Mode2 == 2){
      rincommand[0]++;
    }else if(Mode2 == 3){
      rincommand[1]++;
    }else if(Mode2 == 4){
      spin++;
    }else if(Mode2 == 5){
      check_runing_time[0]++;
    }else if(Mode2 == 6){
      check_runing_time[1]++;
    }else if(Mode2 == 7){
      check_runing_time[2]++;
    }else if(Mode2 == 8){
      ldr_set = ldr_set + 100;
    }else if(Mode2 == 9){
      ldrMinus = ldrMinus + 100;
    }else if(Mode2 == 10){
      mqttStatus++;
    }else if(Mode2 == 11){
      CodeMachine++;
      if (CodeMachine >= 9)
      {
        CodeMachine = 0;
      }
    }else if(Mode2 == 12){
      if (pinSlot == SIG_PIN)
      {
        pinSlot = SIG_PIN2;
      }
      else if (pinSlot == SIG_PIN2)
      {
        pinSlot = SIG_PIN;
      }
    }else if(Mode2 == 13){
      sentVarjson();
      str = "Send data to admin Suscess";
    }else if(Mode2 == 14){
      if(!UpdateFw){
        // digitalWrite(EN_PIN, LOW);
        Slot(0);
        UpdateFw = true;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
      }
    }
  }else if(BT == 2){
    BT = 0;
    if(Mode2 == 0){
      rinStep2[0]--;
    }else if(Mode2 == 1){
      rinStep2[1]--;
    }else if(Mode2 == 2){
      rincommand[0]--;
    }else if(Mode2 == 3){
      rincommand[1]--;
    }else if(Mode2 == 4){
      spin++;
    }else if(Mode2 == 5){
      check_runing_time[0]--;
    }else if(Mode2 == 6){
      check_runing_time[1]--;
    }else if(Mode2 == 7){
      check_runing_time[2]--;
    }else if(Mode2 == 8){
      ldr_set = ldr_set - 100;
    }else if(Mode2 == 9){
      ldrMinus = ldrMinus - 100;
    }else if(Mode2 == 10){
      mqttStatus--;
    }else if(Mode2 == 11){
      CodeMachine--;
      if (CodeMachine <= -1)
      {
        CodeMachine = 8;
      }
    }else if(Mode2 == 12){
      if (pinSlot == SIG_PIN)
      {
        pinSlot = SIG_PIN2;
      }
      else if (pinSlot == SIG_PIN2)
      {
        pinSlot = SIG_PIN;
      }
    }
  }else if(BT == 1){
    BT = 0;
    // PutEprom(); // Save
    writePreferences();
    indexSet = 0;
  }
}
void Drysetting(){
  if(Mode2 == 0){
    lv_label_set_text_fmt(ui_lb_display_setting, "Price 1 : %2d", price[0]);
  }else if(Mode2 == 1){
    lv_label_set_text_fmt(ui_lb_display_setting, "Price 2 : %2d", price[1]);
  }else if(Mode2 == 2){
    lv_label_set_text_fmt(ui_lb_display_setting, "Price 3 : %2d", price[2]);
  }else if(Mode2 == 3){
    lv_label_set_text_fmt(ui_lb_display_setting, "Time dry prgram 1 : %2d", timerDry[0]);
  }else if(Mode2 == 4){
    lv_label_set_text_fmt(ui_lb_display_setting, "Time dry prgram 2 : %2d", timerDry[1]);
  }else if(Mode2 == 5){
    lv_label_set_text_fmt(ui_lb_display_setting, "Time dry prgram 3 : %2d", timerDry[2]);
  }

  //Button();
  if(BT == 4){
    BT = 0;
    Mode2++;
    if(Mode2 >= 6){
      Mode2 = 0;
    }
  }else if(BT == 3){
    BT = 0;
    if(Mode2 == 0){
      price[0] = price[0] + 10;
    }else if(Mode2 == 1){
      price[1] = price[1] + 10;
    }else if(Mode2 == 2){
      price[2] = price[2] + 10;
    }else if(Mode2 == 3){
      timerDry[0] = timerDry[0] + 5;
    }else if(Mode2 == 4){
      timerDry[1] = timerDry[1] + 5;
    }else if(Mode2 == 5){
      timerDry[2] = timerDry[2] + 5;
    }
  }else if(BT == 2){
    BT = 0;
    if(Mode2 == 0){
      price[0] = price[0] - 10;
    }else if(Mode2 == 1){
      price[1] = price[1] - 10;
    }else if(Mode2 == 2){
      price[2] = price[2] - 10;
    }else if(Mode2 == 3){
      timerDry[0] = timerDry[0] - 5;
    }else if(Mode2 == 4){
      timerDry[1] = timerDry[1] - 5;
    }else if(Mode2 == 5){
      timerDry[2] = timerDry[2] - 5;
    }
  }else if(BT == 1){
    BT = 0;
    // PutEprom(); // Save
    writePreferences();
    indexSet = 0;
  }
}

void setRelayType(){
  //var program
  if(CodeMachine == 0){//LG10KgBlack
    program1[0] = 3;
    program1[1] = 2;
    program1[2] = 1;
    
    program2[0] = 3;
    program2[1] = 0;
    program2[2] = 1;

    program3[0] = 1;
    program3[1] = 2;
    program3[2] = 1;

    rinStep2[0] = 7;
    rinStep2[1] = 1;

    rincommand[0] = 8;
    rincommand[1] = 1;

    spin = 6; //step3

    drum[0] = 7;
    drum[1] = 0;
    drum[2] = 0;

    check_runing_time[0] = 19;
    check_runing_time[1] = 13;
    check_runing_time[2] = 5;

    TimeCountdowndrum[0] = 1;
    TimeCountdowndrum[1] = 31;

    TimeCountdown1[0] = 0;
    TimeCountdown1[1] = 31;

    TimeCountdown2[0] = 0;
    TimeCountdown2[1] = 31;

    TimeCountdown3[0] = 0;
    TimeCountdown3[1] = 31;
  }else if(CodeMachine == 1){//LG11KgWhite
    program1[0] = 3;
    program1[1] = 2;
    program1[2] = 1;
    
    program2[0] = 3;
    program2[1] = 0;
    program2[2] = 1;

    program3[0] = 1;
    program3[1] = 2;
    program3[2] = 1;

    rinStep2[0] = 7;
    rinStep2[1] = 1;

    rincommand[0] = 7;
    rincommand[1] = 1;

    spin = 6; //step3

    drum[0] = 6;
    drum[1] = 0;
    drum[2] = 0;

    check_runing_time[0] = 19;
    check_runing_time[1] = 13;
    check_runing_time[2] = 5;

    TimeCountdowndrum[0] = 1;
    TimeCountdowndrum[1] = 31;

    TimeCountdown1[0] = 0;
    TimeCountdown1[1] = 31;

    TimeCountdown2[0] = 0;
    TimeCountdown2[1] = 31;

    TimeCountdown3[0] = 0;
    TimeCountdown3[1] = 31;
  }else if(CodeMachine == 2){//LG15KgBlack
    program1[0] = 5;
    program1[1] = 1;
    program1[2] = 0;
    
    program2[0] = 5;
    program2[1] = 0;
    program2[2] = 0;

    program3[0] = 4;
    program3[1] = 0;
    program3[2] = 1;

    rinStep2[0] = 5;
    rinStep2[1] = 1;

    rincommand[0] = 6;
    rincommand[1] = 1;

    spin = 1; //step3

    drum[0] = 6;
    drum[1] = 0;
    drum[2] = 0;

    check_runing_time[0] = 20;
    check_runing_time[1] = 13;
    check_runing_time[2] = 5;

    TimeCountdowndrum[0] = 1;
    TimeCountdowndrum[1] = 31;

    TimeCountdown1[0] = 0;
    TimeCountdown1[1] = 31;

    TimeCountdown2[0] = 0;
    TimeCountdown2[1] = 31;

    TimeCountdown3[0] = 0;
    TimeCountdown3[1] = 31;
  }else if(CodeMachine == 3){//LG24KgBlack
    program1[0] = 3;
    program1[1] = 1;
    program1[2] = 0;
    
    program2[0] = 3;
    program2[1] = 0;
    program2[2] = 0;

    program3[0] = 4;
    program3[1] = 2;
    program3[2] = 1;

    rinStep2[0] = 5;
    rinStep2[1] = 1;

    rincommand[0] = 6;
    rincommand[1] = 1;

    spin = 1; //step3

    drum[0] = 7;
    drum[1] = 0;
    drum[2] = 0;

    check_runing_time[0] = 21;
    check_runing_time[1] = 13;
    check_runing_time[2] = 5;

    TimeCountdowndrum[0] = 2;
    TimeCountdowndrum[1] = 07;

    TimeCountdown1[0] = 0;
    TimeCountdown1[1] = 31;

    TimeCountdown2[0] = 0;
    TimeCountdown2[1] = 31;

    TimeCountdown3[0] = 0;
    TimeCountdown3[1] = 31;
  }else if (CodeMachine == 4)
  { // LG15KgNew
    program1[0] = 2;
    program1[1] = 1;
    program1[2] = 0;

    program2[0] = 2;
    program2[1] = 3;
    program2[2] = 0;

    program3[0] = 0;
    program3[1] = 4;
    program3[2] = 0;

    rinStep2[0] = 1;
    rinStep2[1] = 1;

    rincommand[0] = 1;
    rincommand[1] = 1;

    spin = 1; // step3

    drum[0] = 1;
    drum[1] = 0;
    drum[2] = 0;

    check_runing_time[0] = 21;
    check_runing_time[1] = 13;
    check_runing_time[2] = 5;

    TimeCountdowndrum[0] = 2;
    TimeCountdowndrum[1] = 07;

    TimeCountdown1[0] = 0;
    TimeCountdown1[1] = 31;

    TimeCountdown2[0] = 0;
    TimeCountdown2[1] = 31;

    TimeCountdown3[0] = 0;
    TimeCountdown3[1] = 31;
  }
}

void writePreferences()
{
  // บันทึกค่าตัวแปรลง Preferences
  // preferences.begin("config", false);  // สร้างพื้นที่เก็บข้อมูลชื่อ "config"

  // เปิด Preferences ในโหมดอ่านและเขียน
  if (!preferences.begin("config", false))
  {
    Serial.println("Failed to open preferences");
    return;
  }
  preferences.putInt("Mode", Mode);
  preferences.putInt("pinSlot", pinSlot);
  preferences.putInt("mqttStatus", mqttStatus);
  preferences.putString("IDserver", IDserver);
  // preferences.putInt("gid", gid);
  // Serial.println(" ===== put gid : " + String(gid) + " ======");
  preferences.putInt("CodeMachine", CodeMachine);

  preferences.putInt("price[0]", price[0]);
  preferences.putInt("price[1]", price[1]);
  preferences.putInt("price[2]", price[2]);

  preferences.putInt("program1[0]", program1[0]);
  preferences.putInt("program1[1]", program1[1]);
  preferences.putInt("program1[2]", program1[2]);

  preferences.putInt("program2[0]", program2[0]);
  preferences.putInt("program2[1]", program2[1]);
  preferences.putInt("program2[2]", program2[2]);

  preferences.putInt("program3[0]", program3[0]);
  preferences.putInt("program3[1]", program3[1]);
  preferences.putInt("program3[2]", program3[2]);

  preferences.putInt("rinStep2[0]", rinStep2[0]);
  preferences.putInt("rinStep2[1]", rinStep2[1]);

  preferences.putInt("rincommand[0]", rincommand[0]);
  preferences.putInt("rincommand[1]", rincommand[1]);

  preferences.putInt("spin", spin);

  preferences.putInt("drum[0]", drum[0]);
  preferences.putInt("drum[1]", drum[1]);
  preferences.putInt("drum[2]", drum[2]);

  preferences.putInt("check_time[0]", check_runing_time[0]);
  preferences.putInt("check_time[1]", check_runing_time[1]);
  preferences.putInt("check_time[2]", check_runing_time[2]);

  preferences.putInt("Timedrum[0]", TimeCountdowndrum[0]);
  preferences.putInt("Timedrum[1]", TimeCountdowndrum[1]);

  preferences.putInt("Timedown1[0]", TimeCountdown1[0]);
  preferences.putInt("Timedown1[1]", TimeCountdown1[1]);

  preferences.putInt("Timedown2[0]", TimeCountdown2[0]);
  preferences.putInt("Timedown2[1]", TimeCountdown2[1]);

  preferences.putInt("Timedown3[0]", TimeCountdown3[0]);
  preferences.putInt("Timedown3[1]", TimeCountdown3[1]);

  preferences.putInt("timerDry[0]", timerDry[0]);
  preferences.putInt("timerDry[1]", timerDry[1]);
  preferences.putInt("timerDry[2]", timerDry[2]);

  preferences.putInt("ldr_set", ldr_set / 100);
  preferences.putInt("StateShutdown", StateShutdown);
  preferences.putInt("SetupData", SetupData);

  // โปรโมชั่นหลายช่วง (setPromoSlots) — บันทึกเพื่อไม่หายหลังรีบูต
  preferences.putInt("psCnt", promoSlotCount);
  for (int i = 0; i < MAX_PROMO_SLOTS; i++) {
    char key[8];
    snprintf(key, sizeof(key), "ps%dd", i);
    preferences.putInt(key, promoSlots[i].day);
    snprintf(key, sizeof(key), "ps%dsH", i);
    preferences.putInt(key, (int)promoSlots[i].startHour);
    snprintf(key, sizeof(key), "ps%dsm", i);
    preferences.putInt(key, (int)promoSlots[i].startMin);
    snprintf(key, sizeof(key), "ps%deH", i);
    preferences.putInt(key, (int)promoSlots[i].endHour);
    snprintf(key, sizeof(key), "ps%dem", i);
    preferences.putInt(key, (int)promoSlots[i].endMin);
    snprintf(key, sizeof(key), "ps%dp0", i);
    preferences.putInt(key, promoSlots[i].pricePro[0]);
    snprintf(key, sizeof(key), "ps%dp1", i);
    preferences.putInt(key, promoSlots[i].pricePro[1]);
    snprintf(key, sizeof(key), "ps%dp2", i);
    preferences.putInt(key, promoSlots[i].pricePro[2]);
  }

  preferences.putInt("pricePro[0]", pricePro[0]);
  preferences.putInt("pricePro[1]", pricePro[1]);
  preferences.putInt("pricePro[2]", pricePro[2]);
  preferences.putInt("coinValue", coinValue);
  preferences.putString("otaServer", server);
  preferences.putInt("otaPort", port);

  preferences.putBool("state_wifi_on", state_wifi_on);

  Serial.println("✅ บันทึกข้อมูลลง Preferences หลักสำเร็จ!");

  preferences.end(); // ปิด Preferences
}

// กดปุ่ม FACTORY_RESTORE_BTN_PIN (BOOT) ภายใน 3 วินาที ก่อนอ่าน Preferences = คืนค่าโรงงาน
// ถ้าเครื่องลงทะเบียนแล้ว (มี Noserial ใน NVS) เก็บ ID/WiFi/gid ไว้ — กัน OTA จากส่วนกลางแล้วเผลอกด BOOT ตอน boot
void setupWaitAdminRestoreFactory() {
  const unsigned long windowMs = 3000;
  Serial.println();
  Serial.println(">>> กดปุ่ม BOOT ใน 3 วินาที เพื่อคืนค่าโรงงาน (ข้ามได้)");
  unsigned long t0 = millis();
  while (millis() - t0 < windowMs) {
    if (digitalRead(FACTORY_RESTORE_BTN_PIN) == LOW) {
      while (digitalRead(FACTORY_RESTORE_BTN_PIN) == LOW)
        vTaskDelay(pdMS_TO_TICKS(20));

      String savedNoserial, savedSsid, savedPass, savedIDserver;
      int savedGid = gid;
      bool keepIdentity = false;
      if (preferences.begin("config", true)) {
        if (preferences.isKey("Noserial")) {
          savedNoserial = preferences.getString("Noserial", "");
          keepIdentity = savedNoserial.length() > 0;
        }
        if (keepIdentity) {
          savedSsid = preferences.getString("ssid", ssidStr);
          savedPass = preferences.getString("password", passStr);
          savedGid = preferences.getInt("gid", gid);
          savedIDserver = preferences.getString("IDserver", IDserver);
        }
        preferences.end();
      }

      if (keepIdentity) {
        Noserial = savedNoserial;
        ssidStr = savedSsid;
        passStr = savedPass;
        gid = savedGid;
        IDserver = savedIDserver;
      }

      writePreferencesfirst();
      yield();
      writePreferences();
      runSessionClear();

      if (keepIdentity) {
        Serial.println(">>> ✅ คืนค่าโรงงานแล้ว (เก็บ ID/WiFi/gid เดิม: " + Noserial + ")");
      } else {
        Serial.println(">>> ✅ คืนค่าโรงงานแล้ว (Preferences หลัก + first)");
      }
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(80));
  }
  Serial.println(">>> ไม่กดปุ่ม บรรทัดถัดไป");
}

void readPreferences()
{
  // เริ่มต้น Preferences ในโหมดอ่านอย่างเดียว
  // preferences.begin("config", true);

  // เปิด Preferences ในโหมดอ่านและเขียน
  if (!preferences.begin("config", true))
  {
    Serial.println("Failed to open preferences");
    return;
  }

  // อ่านค่าตัวแปรจาก Preferences
  Mode = preferences.getInt("Mode", Mode);
  pinSlot = preferences.getInt("pinSlot", pinSlot);
  mqttStatus = preferences.getInt("mqttStatus", mqttStatus);
  IDserver = preferences.getString("IDserver", IDserver);
  // gid = preferences.getInt("gid", gid);
  Serial.println(" ===== get gid : " + String(gid) + " ======");
  CodeMachine = preferences.getInt("CodeMachine", CodeMachine);

  price[0] = preferences.getInt("price[0]", price[0]);
  price[1] = preferences.getInt("price[1]", price[1]);
  price[2] = preferences.getInt("price[2]", price[2]);
  program1[0] = preferences.getInt("program1[0]", program1[0]);
  program1[1] = preferences.getInt("program1[1]", program1[1]);
  program1[2] = preferences.getInt("program1[2]", program1[2]);
  program2[0] = preferences.getInt("program2[0]", program2[0]);
  program2[1] = preferences.getInt("program2[1]", program2[1]);
  program2[2] = preferences.getInt("program2[2]", program2[2]);
  program3[0] = preferences.getInt("program3[0]", program3[0]);
  program3[1] = preferences.getInt("program3[1]", program3[1]);
  program3[2] = preferences.getInt("program3[2]", program3[2]);
  rinStep2[0] = preferences.getInt("rinStep2[0]", rinStep2[0]);
  rinStep2[1] = preferences.getInt("rinStep2[1]", rinStep2[1]);
  rincommand[0] = preferences.getInt("rincommand[0]", rincommand[0]);
  rincommand[1] = preferences.getInt("rincommand[1]", rincommand[1]);
  spin = preferences.getInt("spin", spin);
  drum[0] = preferences.getInt("drum[0]", drum[0]);
  drum[1] = preferences.getInt("drum[1]", drum[1]);
  drum[2] = preferences.getInt("drum[2]", drum[2]);
  check_runing_time[0] = preferences.getInt("check_time[0]", check_runing_time[0]);
  check_runing_time[1] = preferences.getInt("check_time[1]", check_runing_time[1]);
  check_runing_time[2] = preferences.getInt("check_time[2]", check_runing_time[2]);
  TimeCountdowndrum[0] = preferences.getInt("Timedrum[0]", TimeCountdowndrum[0]);
  TimeCountdowndrum[1] = preferences.getInt("Timedrum[1]", TimeCountdowndrum[1]);
  TimeCountdown1[0] = preferences.getInt("Timedown1[0]", TimeCountdown1[0]);
  TimeCountdown1[1] = preferences.getInt("Timedown1[1]", TimeCountdown1[1]);
  TimeCountdown2[0] = preferences.getInt("Timedown2[0]", TimeCountdown2[0]);
  TimeCountdown2[1] = preferences.getInt("Timedown2[1]", TimeCountdown2[1]);
  TimeCountdown3[0] = preferences.getInt("Timedown3[0]", TimeCountdown3[0]);
  TimeCountdown3[1] = preferences.getInt("Timedown3[1]", TimeCountdown3[1]);
  timerDry[0] = preferences.getInt("timerDry[0]", timerDry[0]);
  timerDry[1] = preferences.getInt("timerDry[1]", timerDry[1]);
  timerDry[2] = preferences.getInt("timerDry[2]", timerDry[2]);
  ldr_set = preferences.getInt("ldr_set", ldr_set) * 100; // แปลงกลับเป็นหน่วยที่ต้องการ
  StateShutdown = preferences.getInt("StateShutdown", StateShutdown);
  SetupData = preferences.getInt("SetupData", SetupData);
  // โปรโมชั่นหลายช่วง — โหลดจาก Preferences
  promoSlotCount = preferences.getInt("psCnt", 0);
  if (promoSlotCount > MAX_PROMO_SLOTS) promoSlotCount = MAX_PROMO_SLOTS;
  for (int i = 0; i < MAX_PROMO_SLOTS; i++) {
    char key[8];
    snprintf(key, sizeof(key), "ps%dd", i);
    promoSlots[i].day = (uint8_t)preferences.getInt(key, 0);
    snprintf(key, sizeof(key), "ps%dsH", i);
    promoSlots[i].startHour = (uint8_t)preferences.getInt(key, 0);
    snprintf(key, sizeof(key), "ps%dsm", i);
    promoSlots[i].startMin = (uint8_t)preferences.getInt(key, 0);
    snprintf(key, sizeof(key), "ps%deH", i);
    promoSlots[i].endHour = (uint8_t)preferences.getInt(key, 0);
    snprintf(key, sizeof(key), "ps%dem", i);
    promoSlots[i].endMin = (uint8_t)preferences.getInt(key, 0);
    snprintf(key, sizeof(key), "ps%dp0", i);
    promoSlots[i].pricePro[0] = preferences.getInt(key, pricePro[0]);
    snprintf(key, sizeof(key), "ps%dp1", i);
    promoSlots[i].pricePro[1] = preferences.getInt(key, pricePro[1]);
    snprintf(key, sizeof(key), "ps%dp2", i);
    promoSlots[i].pricePro[2] = preferences.getInt(key, pricePro[2]);
  }
  pricePro[0] = preferences.getInt("pricePro[0]", pricePro[0]);
  pricePro[1] = preferences.getInt("pricePro[1]", pricePro[1]);
  pricePro[2] = preferences.getInt("pricePro[2]", pricePro[2]);
  coinValue = preferences.getInt("coinValue", 10);
  if (coinValue < 1) coinValue = 10;
  server = preferences.getString("otaServer", server);
  port = preferences.getInt("otaPort", port);
  normalizeOtaServer();
  host = server;

  state_wifi_on = preferences.getBool("state_wifi_on", state_wifi_on);

  Serial.println("✅ อ่านข้อมูลจาก Preferences ** หลักสำเร็จ!");

  // ปิด Preferences
  preferences.end();
}
static String readStrFromEEPROM(int addr)
{
  String str = "";
  const int endbuf = addr + 24;
  char ch = 0;
  while (ch != '\n' && addr < endbuf)
  {
    ch = (char)EEPROM.read(addr);
    if (ch == '\0')
      break;
    str += ch;
    addr++;
  }
  str.trim();
  return str;
}

static bool eepromSlotLooksValid(int addr)
{
  const uint8_t b = EEPROM.read(addr);
  return b != 0xFF && b != 0x00 && b != '\n';
}

/** อ่าน Noserial/ssid/pass/gid จาก EEPROM แบบ firmware เก่า — คืน true ถ้ามี Noserial */
static bool tryLoadIdentityFromEeprom()
{
  if (!eepromSlotLooksValid(EEPROM_ID_ADDR))
    return false;

  const String id = readStrFromEEPROM(EEPROM_ID_ADDR);
  if (id.length() == 0)
    return false;

  Noserial = id;
  if (eepromSlotLooksValid(EEPROM_SSID_ADDR))
  {
    const String s = readStrFromEEPROM(EEPROM_SSID_ADDR);
    if (s.length() > 0)
      ssidStr = s;
  }
  if (eepromSlotLooksValid(EEPROM_PASS_ADDR))
  {
    const String p = readStrFromEEPROM(EEPROM_PASS_ADDR);
    if (p.length() > 0)
      passStr = p;
  }
  if (eepromSlotLooksValid(EEPROM_GID_ADDR))
  {
    const String g = readStrFromEEPROM(EEPROM_GID_ADDR);
    if (g.length() > 0)
      gid = g.toInt();
  }
  return true;
}

void writePreferencesfirst()
{
  // บันทึกค่าตัวแปรลง Preferences
  // preferences.begin("config", false);  // สร้างพื้นที่เก็บข้อมูลชื่อ "config"

  // เปิด Preferences ในโหมดอ่านและเขียน
  if (!preferences.begin("config", false))
  {
    Serial.println("Failed to open preferences");
    return;
  }

  preferences.putString("Noserial", Noserial);
  preferences.putString("ssid", ssidStr);
  preferences.putString("password", passStr);
  preferences.putInt("gid", gid);
  Serial.println("✅ บันทึกข้อมูลลง Preferencesfirst สำเร็จ!");

  preferences.end(); // ปิด Preferences
}
void readPreferencesfirst()
{
  // preferences.begin("config", true);  // เปิด Preferences ในโหมดอ่านอย่างเดียว

  // เปิด Preferences ในโหมดอ่านและเขียน
  if (!preferences.begin("config", true))
  {
    Serial.println("Failed to open preferences");
    return;
  }

  Noserial = preferences.getString("Noserial", Noserial);
  ssidStr = preferences.getString("ssid", ssidStr);
  passStr = preferences.getString("password", passStr);
  gid = preferences.getInt("gid", gid);
  // Noserial = preferences.getString("Noserial");
  // ssidStr  = preferences.getString("ssid");
  // passStr  = preferences.getString("password");
  // gid = preferences.getInt("gid");
  Serial.println(" ===== Noserial : " + Noserial + " ====== ");
  Serial.println(" ===== ssid : " + ssidStr + " ====== ");
  Serial.println(" ===== password : " + passStr + " ====== ");
  Serial.println(" ===== gid : " + String(gid) + " ====== ");
  Serial.println("✅ อ่านข้อมูลจาก Preferencesfirst ** สำเร็จ!");
  preferences.end(); // ปิด Preferences
}

void sentDatatoAdmin(){
  StaticJsonDocument<200> jsonDoc;
  jsonDoc["status"] = data;
  String jsonString;
  serializeJson(jsonDoc, jsonString);
  Serial.println(jsonString);

  if (httpJob.state == HTTP_IDLE && wifiLinkUsable()) {
    httpJobStart(HTTP_JOB_SENT_ADMIN, "http://mawell.thddns.net:4740/completed", jsonString, "application/json");
  }
}

// ถ้า MQTT หลุด (เช่น หลัง pauseMqttForOta) จะลอง reconnect หลายรอบก่อนส่ง แล้ว flush ด้วย loop()
static bool ensureMqttForOtaStatus() {
  if (mqclient.connected()) return true;
  for (int attempt = 0; attempt < 5; attempt++) {
    mqttreconnect();
    for (int i = 0; i < 40; i++) {
      if (netLockEnter()) {
        mqttPumpLoopLocked(1);
        netLockLeave();
      }
      delay(50);
      if (mqclient.connected()) return true;
    }
    delay(200);
  }
  return false;
}

void sendOtaStatusMqtt(const char* phase, int percent, const char* message) {
  if (!ensureMqttForOtaStatus()) {
    Serial.println("[MQTT] OtaStatus ข้ามส่ง (reconnect ไม่ได้) " + String(phase));
    return;
  }
  StaticJsonDocument<256> doc;
  doc["id"] = Noserial;
  doc["gid"] = gid;
  doc["phase"] = phase;
  doc["percent"] = percent;
  if (message && message[0] != '\0') doc["message"] = message;
  String out;
  serializeJson(doc, out);
  if (netLockEnter()) {
    mqclient.publish("OtaStatus", out.c_str());
    Serial.println("[MQTT] OtaStatus " + String(phase) + " " + String(percent) + "%");
    for (int i = 0; i < 15; i++) {
      mqttPumpLoopLocked(1);
      delay(50);
    }
    netLockLeave();
  }
}

/** ตั้งค่าตัวแปร config เป็นค่าจากโรงงาน แล้วบันทึก — ใช้เมื่อขอ config ทาง MQTT แล้วไม่มีข้อมูล */
void applyFactoryDefaultsConfig() {
  price[0] = 30; price[1] = 40; price[2] = 50;
  pricePro[0] = 40; pricePro[1] = 50; pricePro[2] = 60;
  timerDry[0] = 1; timerDry[1] = 2; timerDry[2] = 3;
  program1[0] = 3; program1[1] = 2; program1[2] = 1;
  program2[0] = 3; program2[1] = 0; program2[2] = 1;
  program3[0] = 1; program3[1] = 2; program3[2] = 1;
  rinStep2[0] = 7; rinStep2[1] = 1;
  rincommand[0] = 7; rincommand[1] = 1;
  spin = 6;
  drum[0] = 6; drum[1] = 0; drum[2] = 0;
  check_runing_time[0] = 19; check_runing_time[1] = 13; check_runing_time[2] = 5;
  TimeCountdowndrum[0] = 1; TimeCountdowndrum[1] = 30;
  TimeCountdown1[0] = 0; TimeCountdown1[1] = 30;
  TimeCountdown2[0] = 0; TimeCountdown2[1] = 30;
  TimeCountdown3[0] = 0; TimeCountdown3[1] = 30;
  promoSlotCount = 0;
  coinValue = 10;
  mqttStatus = 1;
  Mode = 2;
  CodeMachine = 0;
  setRelayType();
  writePreferences();
  setPriceShow();
  Serial.println("✅ ใช้ค่าจากโรงงาน (ไม่มี config จากระบบ)");
}

void GetData()
{
  if (!wifiLinkUsable()) return;

  if (mqclient.connected()) {
    String reqPayload = "{\"controllerId\":\"" + Noserial + "\"}";
    Serial.println(F("[MQTT] >>> ส่ง configRequest"));
    Serial.println(F("       topic  = configRequest"));
    Serial.println("       payload = " + reqPayload);
    if (!netLockEnter())
      return;
    mqclient.publish("configRequest", reqPayload.c_str());
    mqttPumpLoopLocked(2);
    Serial.println(F("       (รอ configResponse จากระบบ สูงสุด 8 วินาที)"));
    const unsigned long waitMs = 8000;
    const unsigned long start = millis();
    while (millis() - start < waitMs) {
      mqttPumpLoopLocked(1);
      netLockLeave();
      vTaskDelay(pdMS_TO_TICKS(50));
      if (!netLockEnter())
        return;
      if (isMelodyBootSetupPhase()) {
        if (bootMelodySyncQuietReady() && bootMelodyConfigPending) {
          netLockLeave();
          Serial.println(F("[MQTT] ✅ ได้ configResponse ครบ debounce — ชุดสุดท้ายจาก Melody"));
          return;
        }
      } else if (stateSetupdata) {
        netLockLeave();
        Serial.println(F("[MQTT] ✅ ได้ configResponse ภายในเวลา -> GetSetupData() จะบันทึก"));
        return;
      }
      yield();
    }
    netLockLeave();
    Serial.println(F("[MQTT] ไม่ได้รับ configResponse ภายใน 8 วินาที (timeout)"));
  } else {
    Serial.println(F("[MQTT] ข้าม configRequest เพราะ MQTT ยังไม่เชื่อมต่อ"));
  }

  Serial.println(F("⚠️ ใช้ค่าจากโรงงาน (ไม่มี config จากระบบ)"));
  applyFactoryDefaultsConfig();
}
void GetSetupData() {
  if (WiFi.status() != WL_CONNECTED)
    return;
  if (mqttPayloadBuffer.length() > 0) {
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, mqttPayloadBuffer);
    mqttPayloadBuffer = "";
    if (!error && doc["id"].as<String>() == Noserial && doc["cm"].as<String>() == "setup") {
      JsonObject v2 = doc["value_str2"];
      if (!v2.isNull()) {
        if (v2.containsKey("id")) { String newId = v2["id"].as<String>(); if (Noserial != newId) { Noserial = newId; writePreferencesfirst(); } }
        if (v2.containsKey("gid")) gid = v2["gid"].as<int>();
        if (v2.containsKey("ssid")) {
          String s = v2["ssid"].as<String>();
          if (s.length() > 0) ssidStr = s;
        } else if (v2.containsKey("wifiname")) {
          String s = v2["wifiname"].as<String>();
          if (s.length() > 0) ssidStr = s;
        }
        if (v2.containsKey("password")) {
          String s = v2["password"].as<String>();
          if (s.length() > 0) passStr = s;
        } else if (v2.containsKey("wifipass")) {
          String s = v2["wifipass"].as<String>();
          if (s.length() > 0) passStr = s;
        }
        if (v2.containsKey("ModeSystem")) Mode = v2["ModeSystem"].as<int>();
        if (v2.containsKey("mqttStatus")) mqttStatus = v2["mqttStatus"].as<int>();
        if (mqttStatus < 1 || mqttStatus > 2) mqttStatus = 1;
        if (v2.containsKey("CodeMachine")) CodeMachine = v2["CodeMachine"].as<int>();
        if (v2.containsKey("Price1")) { price[0] = v2["Price1"].as<int>(); price[1] = v2["Price2"].as<int>(); price[2] = v2["Price3"].as<int>(); }
        if (v2.containsKey("PricePro1")) { pricePro[0] = v2["PricePro1"].as<int>(); pricePro[1] = v2["PricePro2"].as<int>(); pricePro[2] = v2["PricePro3"].as<int>(); }
        if (v2.containsKey("coinValue")) { int cv = v2["coinValue"].as<int>(); if (cv >= 1) coinValue = cv; }
        if (v2.containsKey("timedry1")) { timerDry[0] = v2["timedry1"].as<int>(); timerDry[1] = v2["timedry2"].as<int>(); timerDry[2] = v2["timedry3"].as<int>(); }
        if (v2.containsKey("program1[0]")) { program1[0] = v2["program1[0]"]; program1[1] = v2["program1[1]"]; program1[2] = v2["program1[2]"]; }
        if (v2.containsKey("program2[0]")) { program2[0] = v2["program2[0]"]; program2[1] = v2["program2[1]"]; program2[2] = v2["program2[2]"]; }
        if (v2.containsKey("program3[0]")) { program3[0] = v2["program3[0]"]; program3[1] = v2["program3[1]"]; program3[2] = v2["program3[2]"]; }
        if (v2.containsKey("rinStep2[0]")) { rinStep2[0] = v2["rinStep2[0]"]; rinStep2[1] = v2["rinStep2[1]"]; }
        if (v2.containsKey("rincommand[0]")) { rincommand[0] = v2["rincommand[0]"]; rincommand[1] = v2["rincommand[1]"]; }
        if (v2.containsKey("spin")) spin = v2["spin"];
        if (v2.containsKey("drum[0]")) { drum[0] = v2["drum[0]"]; drum[1] = v2["drum[1]"]; drum[2] = v2["drum[2]"]; }
        if (v2.containsKey("check_runing_time[0]")) { check_runing_time[0] = v2["check_runing_time[0]"]; check_runing_time[1] = v2["check_runing_time[1]"]; check_runing_time[2] = v2["check_runing_time[2]"]; }
        if (v2.containsKey("TimeCountdowndrum[0]")) { TimeCountdowndrum[0] = v2["TimeCountdowndrum[0]"]; TimeCountdowndrum[1] = v2["TimeCountdowndrum[1]"]; }
        if (v2.containsKey("TimeCountdown1[0]")) { TimeCountdown1[0] = v2["TimeCountdown1[0]"]; TimeCountdown1[1] = v2["TimeCountdown1[1]"]; }
        if (v2.containsKey("TimeCountdown2[0]")) { TimeCountdown2[0] = v2["TimeCountdown2[0]"]; TimeCountdown2[1] = v2["TimeCountdown2[1]"]; }
        if (v2.containsKey("TimeCountdown3[0]")) { TimeCountdown3[0] = v2["TimeCountdown3[0]"]; TimeCountdown3[1] = v2["TimeCountdown3[1]"]; }
        if (v2.containsKey("ldr_set")) { int v = v2["ldr_set"].as<int>(); if (v >= 0) ldr_set = v * 100; }
        if (v2.containsKey("pinSlot")) { int v = v2["pinSlot"].as<int>(); if (v == SIG_PIN || v == SIG_PIN2) pinSlot = v; }
        if (v2.containsKey("StateShutdown")) StateShutdown = v2["StateShutdown"].as<int>();
        if (v2.containsKey("statusReportIntervalMinutes")) {
          int v = v2["statusReportIntervalMinutes"].as<int>();
          if (v >= 1 && v <= 60) statusReportIntervalMinutes = v;
        }
        if (v2.containsKey("otaServer")) {
          server = v2["otaServer"].as<String>();
          normalizeOtaServer();
          host = server;
        }
        if (v2.containsKey("otaPort")) {
          int p = v2["otaPort"].as<int>();
          if (p > 0 && p <= 65535) port = p;
          normalizeOtaServer();
        }
        Serial.println("Setup from MQTT applied, writing preferences.");
        setRelayType();
        vTaskDelay(pdMS_TO_TICKS(20));
        writePreferencesfirst();
        vTaskDelay(pdMS_TO_TICKS(20));
        writePreferences();
        vTaskDelay(pdMS_TO_TICKS(20));
        setPriceShow();
      }
    }
    stateSetupdata = false;
    return;
  }
  Serial.println("⚠️ ไม่มี config จาก MQTT -> ใช้ค่าจากโรงงาน");
  applyFactoryDefaultsConfig();
}

// ส่ง config ปัจจุบันไป topic getdataResponse เพื่อให้ server (MelodyWebapp) รับไปบันทึก — ใช้เมื่อแอดมินส่ง getdata (เหมือน ATD_TM_V2_New_Hier)
void PublishConfigViaMqtt() {
  const size_t capacity = 2048;
  DynamicJsonDocument doc(capacity);
  doc["cm"] = "getdataResponse";
  doc["id"] = Noserial;

  JsonObject v2 = doc.createNestedObject("value_str2");
  v2["id"] = Noserial;
  v2["gid"] = gid;
  v2["ModeSystem"] = Mode;
  v2["mqttStatus"] = mqttStatus;
  v2["CodeMachine"] = CodeMachine;
  v2["Price1"] = price[0];
  v2["Price2"] = price[1];
  v2["Price3"] = price[2];
  v2["PricePro1"] = pricePro[0];
  v2["PricePro2"] = pricePro[1];
  v2["PricePro3"] = pricePro[2];
  v2["coinValue"] = coinValue;
  v2["timedry1"] = timerDry[0];
  v2["timedry2"] = timerDry[1];
  v2["timedry3"] = timerDry[2];
  v2["program1[0]"] = program1[0];
  v2["program1[1]"] = program1[1];
  v2["program1[2]"] = program1[2];
  v2["program2[0]"] = program2[0];
  v2["program2[1]"] = program2[1];
  v2["program2[2]"] = program2[2];
  v2["program3[0]"] = program3[0];
  v2["program3[1]"] = program3[1];
  v2["program3[2]"] = program3[2];
  v2["rinStep2[0]"] = rinStep2[0];
  v2["rinStep2[1]"] = rinStep2[1];
  v2["rincommand[0]"] = rincommand[0];
  v2["rincommand[1]"] = rincommand[1];
  v2["spin"] = spin;
  v2["drum[0]"] = drum[0];
  v2["drum[1]"] = drum[1];
  v2["drum[2]"] = drum[2];
  v2["check_runing_time[0]"] = check_runing_time[0];
  v2["check_runing_time[1]"] = check_runing_time[1];
  v2["check_runing_time[2]"] = check_runing_time[2];
  v2["TimeCountdowndrum[0]"] = TimeCountdowndrum[0];
  v2["TimeCountdowndrum[1]"] = TimeCountdowndrum[1];
  v2["TimeCountdown1[0]"] = TimeCountdown1[0];
  v2["TimeCountdown1[1]"] = TimeCountdown1[1];
  v2["TimeCountdown2[0]"] = TimeCountdown2[0];
  v2["TimeCountdown2[1]"] = TimeCountdown2[1];
  v2["TimeCountdown3[0]"] = TimeCountdown3[0];
  v2["TimeCountdown3[1]"] = TimeCountdown3[1];
  v2["ldr_set"] = ldr_set / 100;
  v2["StateShutdown"] = StateShutdown;
  v2["SetupData"] = SetupData;
  v2["pinSlot"] = pinSlot;
  v2["statusReportIntervalMinutes"] = statusReportIntervalMinutes;
  v2["otaServer"] = server;
  v2["otaPort"] = port;
  v2["ssid"] = ssidStr;
  v2["password"] = passStr;

  String output;
  serializeJson(doc, output);
  if (!netLockEnter())
    return;
  bool ok = mqclient.publish("getdataResponse", output.c_str());
  mqttPumpLoopLocked(3);
  netLockLeave();
  if (ok) {
    Serial.println("MQTT getdataResponse published OK");
  } else {
    Serial.print("MQTT getdataResponse publish failed (payload len=");
    Serial.print(output.length());
    Serial.println(" bytes)");
  }
}

void sentVarjson(){
    preferences.begin("config", true);  // เปิด Preferences ในโหมดอ่านอย่างเดียว
    StaticJsonDocument<1024> jsonDoc;
    const char* keys[] = {"Noserial", "ssid", "password", "IDserver"};

    for (const char* key : keys) {
        jsonDoc[key] = preferences.getString(key, "");
    }

    jsonDoc["gid"] = gid;
    jsonDoc["Mode"] = Mode;
    jsonDoc["CodeMachine"] = CodeMachine;

    JsonArray priceArr = jsonDoc.createNestedArray("price");
    for (int i = 0; i < 3; i++) {
        priceArr.add(price[i]);
    }

    JsonArray program1Arr = jsonDoc.createNestedArray("program1");
    for (int i = 0; i < 3; i++) {
        program1Arr.add(program1[i]);
    }

    JsonArray program2Arr = jsonDoc.createNestedArray("program2");
    for (int i = 0; i < 3; i++) {
        program2Arr.add(program2[i]);
    }

    JsonArray program3Arr = jsonDoc.createNestedArray("program3");
    for (int i = 0; i < 3; i++) {
        program3Arr.add(program3[i]);
    }

    JsonArray rinStep2Arr = jsonDoc.createNestedArray("rinStep2");
    for (int i = 0; i < 2; i++) {
        rinStep2Arr.add(rinStep2[i]);
    }

    JsonArray rincommandArr = jsonDoc.createNestedArray("rincommand");
    for (int i = 0; i < 2; i++) {
        rincommandArr.add(rincommand[i]);
    }

    jsonDoc["spin"] = spin;

    JsonArray drumArr = jsonDoc.createNestedArray("drum");
    for (int i = 0; i < 3; i++) {
        drumArr.add(drum[i]);
    }

    JsonArray checkRunTimeArr = jsonDoc.createNestedArray("check_run_time");
    for (int i = 0; i < 3; i++) {
        checkRunTimeArr.add(check_runing_time[i]);
    }

    JsonArray TimeCountDrumArr = jsonDoc.createNestedArray("TimeCountDrum");
    for (int i = 0; i < 2; i++) {
        TimeCountDrumArr.add(TimeCountdowndrum[i]);
    }

    JsonArray TimeCount1Arr = jsonDoc.createNestedArray("TimeCount1");
    for (int i = 0; i < 2; i++) {
        TimeCount1Arr.add(TimeCountdown1[i]);
    }

    JsonArray TimeCount2Arr = jsonDoc.createNestedArray("TimeCount2");
    for (int i = 0; i < 2; i++) {
        TimeCount2Arr.add(TimeCountdown2[i]);
    }

    JsonArray TimeCount3Arr = jsonDoc.createNestedArray("TimeCount3");
    for (int i = 0; i < 2; i++) {
        TimeCount3Arr.add(TimeCountdown3[i]);
    }

    JsonArray timerDryArr = jsonDoc.createNestedArray("timerDry");
    for (int i = 0; i < 3; i++) {
        timerDryArr.add(timerDry[i]);
    }

    jsonDoc["ldr_set"] = ldr_set;
    jsonDoc["StateShutdown"] = StateShutdown;
    jsonDoc["SetupData"] = SetupData;

    jsonDoc["coinValue"] = coinValue;

    JsonArray priceProArr = jsonDoc.createNestedArray("pricePro");
    for (int i = 0; i < 3; i++) {
        priceProArr.add(pricePro[i]);
    }

    preferences.end();  // ปิด Preferences

    String jsonString;
    serializeJson(jsonDoc, jsonString);
    Serial.println(jsonString);
    // mqclient.publish("check", jsonString.c_str());
    vTaskDelay(100 / portTICK_PERIOD_MS);

    if(WiFi.status()== WL_CONNECTED){
      if (!netLockEnter())
        return;
      HTTPClient http;
      http.begin("http://mawell.thddns.net:4740/check");
      http.addHeader("Content-Type", "application/json");
      
      int httpResponseCode = http.POST(jsonString);
      Serial.print(httpResponseCode);
      http.end();
      netLockLeave();
    }else{
      // moneyEEprom = moneyEEprom + paul;
    }

}

void commandApp(){
  if(cm == "cmProgram"){
    // ตั้งค่าโปรโมชั่นหลายช่วงเวลา/หลายวัน ผ่าน MQTT (รูปแบบเดียวกับ ATD_TM_V2_New_Hier)
    if (value_str2 == "setPromoSlots")
    {
      StaticJsonDocument<512> doc;
      DeserializationError err = deserializeJson(doc, value_str1);
      if (err)
      {
        Serial.print(F("deserializeJson(setPromoSlots) failed: "));
        Serial.println(err.f_str());
        return;
      }

      promoSlotCount = 0;
      JsonArray slots = doc["slots"].as<JsonArray>();
      if (!slots.isNull())
      {
        for (JsonObject slot : slots)
        {
          if (promoSlotCount >= MAX_PROMO_SLOTS)
            break;

          PromoSlot &ps = promoSlots[promoSlotCount++];
          ps.day = slot["day"] | 0;

          String startStr = slot["start"] | "00:00";
          String endStr = slot["end"] | "00:00";
          ps.startHour = startStr.substring(0, 2).toInt();
          ps.startMin = startStr.substring(3, 5).toInt();
          ps.endHour = endStr.substring(0, 2).toInt();
          ps.endMin = endStr.substring(3, 5).toInt();

          // ราคาพิเศษเฉพาะช่วงนี้ (ถ้าไม่ส่งมา จะ fallback ไปใช้ pricePro ทั่วไป)
          JsonArray pricesSlot = slot["prices"].as<JsonArray>();
          for (int i = 0; i < 3; i++)
          {
            if (!pricesSlot.isNull() && i < (int)pricesSlot.size())
            {
              ps.pricePro[i] = pricesSlot[i].as<int>();
            }
            else
            {
              ps.pricePro[i] = pricePro[i];
            }
          }
        }
      }

      // อัปเดตราคาโปรโมชั่นพื้นฐาน (default) ถ้ามีส่งมาด้วย
      if (doc.containsKey("Price1Pro")) pricePro[0] = doc["Price1Pro"].as<int>();
      if (doc.containsKey("Price2Pro")) pricePro[1] = doc["Price2Pro"].as<int>();
      if (doc.containsKey("Price3Pro")) pricePro[2] = doc["Price3Pro"].as<int>();

      // อัปเดตราคาปกติถ้ามีส่งมาด้วย
      if (doc.containsKey("Price1")) price[0] = doc["Price1"].as<int>();
      if (doc.containsKey("Price2")) price[1] = doc["Price2"].as<int>();
      if (doc.containsKey("Price3")) price[2] = doc["Price3"].as<int>();

      // เขียนเก็บลง Preferences และอัปเดตราคาแสดงผล
      writePreferences();
      setPriceShow();

      Serial.println("Updated promo slots via MQTT, count: " + String(promoSlotCount));
    }
    else
    {
      pendingUIAction = PENDING_UI_FIRST_SCREEN;
      if(value_str2.toInt() == 0){
        status_machine_run = false;
        status_machine_prepare = false;
        chanel = 0;step = 0;
        Serial.println("command reset program");
        stateWhile = false;
        stateReset = true; 
      }else if(value_str2.toInt() == 7 && status_machine_run){
        if(Mode == 2){
          minn = minn + DRY_EXTEND_MIN_PER_COIN;
          if(minn >= 60){
            hrs++;
            minn = minn - 60;
          }
        }
      }else if(value_str2.toInt() == 9){ // off
        // not thing
      }else{
        if(!status_machine_prepare && !status_machine_run && value_str2.toInt() != 7){
          pendingProgram = value_str2.toInt();
          pendingUIAction = PENDING_UI_SHOW_RUN;
          Serial.println("command run program : " + String(pendingProgram));
        }
      }
    }
  }else if(cm == "cmCommand"){
    // addFlagstoFirstscreen();
    state_relay = true;
    if(value_str2 == "power"){
      Power();
    }else if(value_str2 == "jok"){
      Jok();
    }else if(value_str2 == "start"){
      Start();
    }else if(value_str2 == "temp"){
      Temp();
    }else if(value_str2 == "Spin"){
      Spin();
    }else if(value_str2 == "Shutdown"){
      preferences.begin("config", false);
        StateShutdown = 1;
        preferences.putInt("StateShutdown", StateShutdown);
      preferences.end();
      pendingLabel1 = "ปิดระบบชั่วคราว";
      pendingLabel2 = "Temporarily shut down the system.";
      pendingUIAction = PENDING_UI_SHUTDOWN;
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }else if(value_str2 == "Restart"){
      runSessionClear();
      preferences.begin("config", false);
        StateShutdown = 0;
        preferences.putInt("StateShutdown", StateShutdown);
      preferences.end();
      pendingLabel1 = "กรุณาแตะหน้าจอ เพื่อเข้าสู่ระบบ";
      pendingLabel2 = "Please touch.";
      pendingUIAction = PENDING_UI_RESTART;
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      publishPresenceOfflineGraceful();
      ESP.restart();
    }else if(value_str2 == "slot"){
      if(screenUse == 1){
        if(pinSlot == SIG_PIN){
          pinSlot = SIG_PIN2;
        }else{
          pinSlot = SIG_PIN;
        }
        preferences.begin("config", false);
          preferences.putInt("pinSlot", pinSlot);
        preferences.end();
        pendingLabel1 = "เปลี่ยนช่องอ่านเหรียญ : " + String(pinSlot);
        pendingLabel2 = "Change the coin reader.";
        pendingUIAction = PENDING_UI_SLOT_MSG;
        vTaskDelay(5000 / portTICK_PERIOD_MS);
      }
    }else if(value_str2 == "update"){
      pendingUIAction = PENDING_UI_FIRST_SCREEN;
      stateUpdateFw = true;
      if(chanel == 0 && !UpdateFw){
        suspendMachineTasksForOta();
        pendingLabel1 = "ระบบกำลังอัพเดท..";
        pendingLabel2 = "System is updating.";
        pendingUIAction = PENDING_UI_UPDATE_MSG;
        Serial.println("******* Updating *******");
        Slot(0);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        UpdateFw = true;
      }
    }else if(value_str2 == "ESP"){
      pendingUIAction = PENDING_UI_FIRST_SCREEN;
      pendingLabel1 = "ระบบกำลัง Reboot..";
      pendingLabel2 = "System is Rebooting.";
      pendingUIAction = PENDING_UI_REBOOT_MSG;
      Serial.println("******* rebooting *******");
      vTaskDelay(3000 / portTICK_PERIOD_MS);
      publishPresenceOfflineGraceful();
      ESP.restart();
    }else if(value_str2 == "check"){
      Serial.println("******* Sent Data to Admin *******");
      sentVarjson();
    }else if (value_str2 == "LdrOpen" || value_str2 == "LdrOpen2")
    {
      Serial.println("******* Ldr1 Readed *******");
      chanelLdrCheck = chanel;
      stepLdrCheck = step;
      stateLdr1Screen = screenUse;
      state_status_machine_run = status_machine_run;
      status_machine_run = false;
      pendingUIAction = PENDING_UI_FIRST_SCREEN;
      step = 0;chanel = 0;
      if(value_str2 == "LdrOpen"){
        stateCheckLdr1 = true;
      }else if(value_str2 == "LdrOpen2"){
        stateCheckLdr2 = true;
      }
    }else if (value_str2 == "LdrClose" || value_str2 == "LdrClose2")
    {
      chanel = chanelLdrCheck;
      step = stepLdrCheck;
      if(value_str2 == "LdrClose"){
        stateCheckLdr1 = false;
      }else if(value_str2 == "LdrClose2"){
        stateCheckLdr2 = false;
      }
      if(state_status_machine_run)
      {
        status_machine_run = true;
      }
      pendingUIAction = PENDING_UI_LDR_CLOSE;
    }else if(value_str2 == "Setup"){
        SetupData = 1;

        preferences.begin("config", false);  // เปิด Preferences (โหมดอ่านอย่างเดียว)
        preferences.putInt("SetupData", SetupData);
        preferences.end();  // ปิด Preferences

        Serial.println("*******setup complete and rebooting *******");
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        publishPresenceOfflineGraceful();
        ESP.restart();
    }else if (value_str2 == "BT1"){
      Serial.println("button = BT1");
      chanel = 0; screenUse = 1;
      pendingUIAction = PENDING_UI_BT1_HOME;
    }else if (value_str2 == "BT2"){
      Serial.println("button = BT2");
      if (chanel == 12)
      {
        BT = 2;
      }
    }else if (value_str2 == "BT4"){
      Serial.println("button = BT4");
      if (chanel == 12)
      {
        BT = 4;
      }
      else if (chanel == 0 && screenUse == 1)
      {
        pendingUIAction = PENDING_UI_BT4_SETTING;
      }
    }else if (value_str2 == "BT3"){
      Serial.println("button = BT3");
      if (chanel == 12)
      {
        BT = 3;
      }
    }else if (value_str2 == "BT5"){
      Serial.println("button = BT5");
      stateReset = true;   
    }
    state_relay = false;
  }
}
void SetTimerSend(){
  if (minn < 10) {
    TimeSent = "0" + String(hrs) + ":" + "0" + String(minn);
  } else {
    TimeSent = "0" + String(hrs) + ":" + String(minn);
  }
}
void SetStatusControl(){
  if(StatusControl == "00" || StatusControl == "01" || StatusControl == "02"){
    // not thing
  }else{
    if(program == 1){
      StatusControl = "1on";
    }else if(program == 2){
      StatusControl = "2on";
    }else if(program == 3){
      StatusControl = "3on";
    }else if(program == 4){
      StatusControl = "Drum";
    }else if(program == 5){
      StatusControl = "rin";
    }else if(program == 6){
      StatusControl = "spin";
    }else if(program == 7){
      StatusControl = "+10";
    }else if(program == 0){
      StatusControl = "off";
    }
  }
}

/** Secret ใช้ร่วมกับ backend สำหรับ encode/decode รหัสแสดง (ต้องตรงกัน) */
static const char DISPLAY_CODE_KEY[] = "MelodyDisplayKey";

/** คืนค่ารหัสที่แปลงจาก Noserial สำหรับแสดงบนจอ (XOR + hex; backend decode ได้เพื่อค้นหา controllerId) */
String getDisplayCodeFromNoserial() {
  const size_t keyLen = sizeof(DISPLAY_CODE_KEY) - 1;
  if (keyLen == 0 || Noserial.length() == 0) return String("");
  static const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(Noserial.length() * 2);
  for (size_t i = 0; i < Noserial.length(); i++) {
    uint8_t b = (uint8_t)Noserial.charAt(i) ^ (uint8_t)DISPLAY_CODE_KEY[i % keyLen];
    out += hex[(b >> 4) & 0x0F];
    out += hex[b & 0x0F];
  }
  return out;
}

void setPriceShow() {
  for (size_t i = 0; i < 3; i++) PriceShow[i] = price[i];
}

bool UpdateBalanceV3(int amount) {
  if (wifiLinkUsable() && statewifi){
    HTTPClient http;
    http.begin(ServerSentBalanceV3);
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");

    String httpRequestData = "{\"Title\":\"" + Noserial + "\",\"ID\":\"" + IDserver + "\",\"Program\":\"" + String(program) + "\",\"Price\":\"" + String(amount) + "\",\"Slot\":\"" + String(amount) + "\",\"Qr\":\"" + String("0") + "\"}";
    int httpResponseCode = http.POST(httpRequestData);

    Serial.print("HTTP Balance " + Noserial + " code : ");
    Serial.print(httpResponseCode);
    
    if(httpResponseCode == 202){
      Serial.println(" HTTP Send Data Balance : " + String(amount) + " Bath : Complete...");
      http.end();
      return true;
    }else{
      Serial.println(" HTTP Send Data Balance Not Complete...");
    }
    http.end();
    return false;
  }else{
    // offline, ไว้รอ taskWifiMqtt เรียกใหม่เมื่อออนไลน์
  }
  return false;
}

void CheckPromotion(){
  static unsigned long timerCheckPro = millis();
  if(millis() < timerCheckPro || millis() - timerCheckPro >= 1000){
    if(!lv_obj_has_flag(ui_conS1, LV_OBJ_FLAG_HIDDEN)){
      lv_label_set_text_fmt(ui_lb_datetime, "%04d-%02d-%02d %02d:%02d:%02d",
        rtc.getYear(), rtc.getMonth()+1, rtc.getDay(),
        rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
    }

    int dayNow = rtc.getDayofWeek();
    int hourNow = rtc.getHour(true);
    int minNow = rtc.getMinute();
    if (promoSlotCount > 0) {
      bool promoActive = false;
      PromoSlot *activeSlot = nullptr;
      for (int i = 0; i < promoSlotCount; i++) {
        PromoSlot &ps = promoSlots[i];
        if (ps.day != dayNow) continue;
        bool afterStart = (hourNow > ps.startHour) || (hourNow == ps.startHour && minNow >= ps.startMin);
        bool beforeEnd = (hourNow < ps.endHour) || (hourNow == ps.endHour && minNow <= ps.endMin);
        if (afterStart && beforeEnd) {
          promoActive = true;
          activeSlot = &ps;
          break;
        }
      }
      if (promoActive && activeSlot != nullptr) {
        for (int i = 0; i < 3; i++) PriceShow[i] = activeSlot->pricePro[i];
      } else {
        for (int i = 0; i < 3; i++) PriceShow[i] = price[i];
      }
    } else {
      for (int i = 0; i < 3; i++) PriceShow[i] = price[i];
    }
    timerCheckPro = millis();
  }
}


void printTocore(){
  Serial.print("checkcoin core : ");
  Serial.print(xPortGetCoreID());
  Serial.print(" Piority : ");
  Serial.println(uxTaskPriorityGet(NULL));
}