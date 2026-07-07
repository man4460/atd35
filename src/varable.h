#ifndef VARABLE_H_
#define VARABLE_H_

// ESP32-S3 touch — OTA ai_touch
#define OldBoard 0

#define WiFi_TIMEOUT_MS 20000

// =============================================================================
//  DEPLOY CONFIG — แก้เฉพาะบล็อกนี้ก่อน pio run / flash
// =============================================================================

const char* fwversion[] = {"Current Firmware\r\n", "Version 3.91\r\n"};

// --- ตัวเครื่อง / Melody ---
int gid = 99;
String Noserial = "65M000000";
String IDserver = "94";
int CodeMachine = 0;
int Mode = 2;
String userID = "ai_touch"; // OTA → fw/ai_touch/

// --- WiFi เริ่มต้น ---
String ssidStr = "melody";
String passStr = "0815418771";

// --- MQTT — v4 WSS หรือ TCP เก่า ---
#define MELODY_PROTOCOL_VERSION 4
#define MQTT_USE_WEBSOCKET 1

#if MQTT_USE_WEBSOCKET
#define MQTT_WS_USE_SSL 1
const char* mqtt_ws_host = "melodymqtt.ma-well.com";
const int mqtt_ws_port = 443;
const char* mqtt_ws_path = "/";
int mqtt_port1 = 443;
#else
const char* mqtt_server1 = "mawell.thddns.net";
const char* mqtt_server2 = "broker.mqtt.cool";
int mqtt_port1 = 4741;
int mqtt_port2 = 1883;
#endif

const char* mqtt_username = "mawell";
const char* mqtt_password = "4460";
int mqttStatus = 1;

// --- LDR ---
int ldr_set = 3000;
int ldrMinus = 1000;

// =============================================================================
//  HARDWARE — ESP32-S3 touch
// =============================================================================

#define LED_Y 5
#define IO_ADDR (0x21)
#define LDR1_PIN 1
#define LDR2_PIN 2
int ldrPin = LDR1_PIN;
#define SIG_PIN 41
#define EN_PIN 42
#define TX_PIN 40
#define SIG_PIN2 39
int pinSlot = SIG_PIN;
int count = 0;
bool count_update_flag = false;
int coinPulse = 30;

// =============================================================================
//  RUNTIME / SHARED
// =============================================================================

#define EEPROM_SIZE 155
#define EEPROM_ID_ADDR 70
#define EEPROM_SSID_ADDR 82
#define EEPROM_PASS_ADDR (EEPROM_SSID_ADDR + 24)
#define EEPROM_IDSHAREPOINT_ADDR (EEPROM_PASS_ADDR + 24)
#define EEPROM_GID_ADDR (EEPROM_IDSHAREPOINT_ADDR + 8)

int coinValue = 10;
#define DRY_EXTEND_MIN_PER_COIN 10

const char* ntpServer = "1.th.pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;
String mch_order_no = "";
String mch_order_no_set = "";

int total_money = 0;
int price[] = {30, 40, 50};
int pricePro[] = {40, 50, 60};
int PriceShow[] = {30, 40, 50};
int item_price = 0;
int revSendingAmount = 0;
String revSendingTxn = "";
unsigned long revTxnSeq = 0;
int pendingBalance = 0;
int chanelPay = 0;
bool chanelcoinStatus = false;
int minn_countdown_wait = 0;
int second_countdown_wait = 0;
bool status_countdown_wait = false;
bool status_machine_run = false;
bool status_machine_prepare = false;
int program = 0;
int value_str1_int;

struct PromoSlot {
  uint8_t day;
  uint8_t startHour;
  uint8_t startMin;
  uint8_t endHour;
  uint8_t endMin;
  int pricePro[3];
};

const int MAX_PROMO_SLOTS = 10;
PromoSlot promoSlots[MAX_PROMO_SLOTS];
int promoSlotCount = 0;

const char* mqtt_server;
int mqtt_port;
#if !MQTT_USE_WEBSOCKET
// mqtt_server1/2, mqtt_port1/2 — อยู่ใน DEPLOY
#else
const char* mqtt_server1 = "mawell.thddns.net";
const char* mqtt_server2 = "broker.mqtt.cool";
int mqtt_port2 = 1883;
#endif

String topic = "V" + String(gid);

bool statusqr = true;
String cm;
String value_str1;
String value_str2;
String idSql;
lv_obj_t* qrcode;
static lv_timer_t* timer;

unsigned long timerstanby;

int chanel = 0;
String StatusControl;
int Screen = 1;

int hrs = 0;
int minn = 0;
int second = 0;
int step = 0;
bool pause_timer = false;
int count_minn_pass = 0;

int program1[] = {3, 2, 1};
int program2[] = {3, 0, 1};
int program3[] = {1, 2, 1};
int rinStep2[] = {7, 1};
int rincommand[] = {7, 1};
int spin = 6;
int drum[] = {6, 0, 0};
int check_runing_time[] = {19, 13, 5};
int TimeCountdowndrum[] = {1, 30};
int TimeCountdown1[] = {0, 30};
int TimeCountdown2[] = {0, 30};
int TimeCountdown3[] = {0, 30};

int TimerA;
int timerDry[] = {1, 2, 3};

bool endProgram = false;
int state_error = 0;

String passAdmin = "4460";
bool stateChange_passAdmin = false;

int BT = 0;
int indexSet = 0;
int Mode1 = 0;
int Mode2 = 0;
int Price;
int R[] = {0, 0, 0};
int T[] = {0, 0};
int SetupData = 0;
int StateShutdown = 0;

static bool state_step2 = false;
static bool state_step3 = false;

bool firstGetdata = false;

bool UpdateFw = false;
String Path_GetVersion = "/ota/version";
String Path_OTI = "/ota/download";
String api_key = "cf860590807a21db3be15ae3f99f706b";
String server = "backend.ma-well.com";
String host = server;
int port = 80;

String Path_MqttReport = "/public/machines/mqtt-report";
String Path_DeviceAck = "/public/machines/device-ack";
String Path_UpdateState = "/public/machines/update-state";
String Path_DeviceRevenue = "/public/machines/device-revenue";
String melodyServer = "backend.ma-well.com";
int melodyPort = 80;
long contentLength = 0;
bool isValidContentType = false;
String ServerSentBalanceV3 = "https://prod-10.southeastasia.logic.azure.com:443/workflows/e2fb5b61149c46bf93c39badf52cfacf/triggers/manual/paths/invoke?api-version=2016-06-01&sp=%2Ftriggers%2Fmanual%2Frun&sv=1.0&sig=MneSpDjmFs8LZpsz0DhH0A-M3uDP9XXrjZA83Yb11j4";
bool stateGetdata = 0;
bool stateSetupdata = 0;
bool stateSendConfigMqtt = false;
bool stateUpdateState = 0;
int statusReportIntervalMinutes = 5;
bool stateSentPriceServer = 0;
String TimeSent;
String mqttPayloadBuffer;
bool stateUpdateFw = 0;

int priceSentVerver = 0;
String data;
bool statewifi = false;
bool stateReset = false;
bool drain_water = false;
int stepHier = 0;
bool stateIntime = false;
bool state_wifi_on = true;

bool stateCheckLdr1 = false;
bool stateCheckLdr2 = false;
int chanelLdrCheck = 0;
int stepLdrCheck = 0;
int displaystandbyLdrCheck = 0;

int screenUse = 1;
int stateLdr1Screen = 0;
int stateLdr2Screen = 0;
bool state_status_machine_run = false;

bool stateWhile = false;
bool stateUpdateBalanceDry = false;
bool state_relay = false;

#define PENDING_UI_NONE 0
#define PENDING_UI_LABEL_MSG 1
#define PENDING_UI_FIRST_SCREEN 2
#define PENDING_UI_SHOW_RUN 3
#define PENDING_UI_SHUTDOWN 4
#define PENDING_UI_RESTART 5
#define PENDING_UI_SLOT_MSG 6
#define PENDING_UI_UPDATE_MSG 7
#define PENDING_UI_REBOOT_MSG 8
#define PENDING_UI_LDR_CLOSE 9
#define PENDING_UI_BT1_HOME 10
#define PENDING_UI_BT4_SETTING 11
#define PENDING_UI_DISPLAY_SETTING 12
#define PENDING_UI_QR_SUCCESS 13
#define PENDING_UI_QR_GEN 14
#define PENDING_UI_SETTING_EXIT_COMMAND 15
#define PENDING_UI_RESUME_RUN 16
int pendingUIAction = PENDING_UI_NONE;
String pendingLabel1 = "";
String pendingLabel2 = "";
int pendingProgram = 0;
String pendingQrPayload = "";

#endif /* VARABLE_H_ */
