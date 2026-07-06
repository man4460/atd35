#ifndef VARABLE_H_
#define VARABLE_H_

// บอร์ดนี้คือบอร์ดใหม่ (ESP32-S3 touch) — run_session.h ใช้ค่านี้เลือก logic LDR (bright = val <= ldr_set)
#define OldBoard 0

// #define EN_PIN  1
// #define SIG_PIN 2
#define LED_Y 5

#define WiFi_TIMEOUT_MS 20000

//eeprom
#define EEPROM_SIZE 155 // size of eeprom
#define EEPROM_ID_ADDR 70
#define EEPROM_SSID_ADDR 82
#define EEPROM_PASS_ADDR (EEPROM_SSID_ADDR + 24)
#define EEPROM_IDSHAREPOINT_ADDR (EEPROM_PASS_ADDR + 24)
#define EEPROM_GID_ADDR (EEPROM_IDSHAREPOINT_ADDR + 8)

//relay
#define IO_ADDR (0x21)

//ldr
#define LDR1_PIN    1
#define LDR2_PIN    2
int ldrPin = LDR1_PIN;

//coin
#define SIG_PIN    41 // เธเธณเธซเธเธ”เธเธฒเธ—เธตเนเธ•เนเธญ SIG (RX)
#define EN_PIN     42 // เธเธณเธซเธเธ”เธเธฒเธ—เธตเนเธ•เนเธญ EN
#define TX_PIN     40 // เธเธณเธซเธเธ”เธเธฒ TX
// เธเนเธฒเธซเธเธถเนเธเน€เธซเธฃเธตเธขเธ (เธเธฒเธ—) โ€” เนเธเนเธเธฒเธเนเธ”เธเธเธญเธฃเนเธ”เธซเธฃเธทเธญ config MQTT เนเธ”เน
int coinValue = 10;
#define DRY_EXTEND_MIN_PER_COIN 10  // นาทีต่อ 1 pulse ต่อเวลาอบ (1 pulse = coinValue บาท)
#define SIG_PIN2   39 // เธเธณเธซเธเธ”เธเธฒเธ—เธตเนเธ•เนเธญ SIG (RX)
int pinSlot = SIG_PIN ;
int count = 0; // เธ•เธฑเธงเนเธเธฃเธเธฑเธ Pulse เธ—เธตเนเน€เธเธฃเธทเนเธญเธเธฃเธฑเธเน€เธซเธฃเธตเธขเธเธชเนเธเน€เธเนเธฒเธกเธฒ 
bool count_update_flag = false; // เธ•เธฑเธงเนเธเธฃเน€เธเนเธเธเนเธฒเธงเนเธฒ count เธญเธฑเธเน€เธ”เธ—เนเธฅเนเธง
int coinPulse = 30;

//wifi setup
// String ssidStr = "man4460base_2.4G";
// String passStr = "Man0815418771";
String ssidStr = "melody";
String passStr = "0815418771";

const char* fwversion[] = {"Current Firmware\r\n", "Version 3.79\r\n"};
// v3.50: recovery dry -> PENDING_UI_RESUME_RUN โชว์ timer แทน standby

//esp32time
const char* ntpServer = "1.th.pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600; // GMT+7
const int   daylightOffset_sec = 0;
// ESP32Time rtc(0);  // offset in seconds GMT
// struct tm timeinfo;
String mch_order_no = "";
String mch_order_no_set ="";


//price
// volatile bool pulse_trigger_flag = false;
int total_money = 0;
int price[] = {30,40,50};
int pricePro[] = {40,50,60};
int PriceShow[] = {30,40,50};
int item_price = 0;
// รายรับส่งทาง HTTP แบบ idempotent — batch ที่กำลังส่ง (freeze amount+txnId ตอนเริ่มส่ง)
int revSendingAmount = 0; // ยอดของ batch ที่ freeze ไว้ส่ง (0 = ยังไม่ freeze)
String revSendingTxn = ""; // txnId ของ batch ที่กำลังส่ง (idempotency key)
unsigned long revTxnSeq = 0; // ตัวนับ txn ต่อเครื่อง (persistent) เพื่อสร้าง txnId ไม่ซ้ำ
int pendingBalance = 0;   // เธขเธญเธ”เธฃเธฒเธขเธฃเธฑเธเธ—เธตเนเธขเธฑเธเธชเนเธเธเธถเนเธเน€เธเธดเธฃเนเธเน€เธงเธญเธฃเนเนเธกเนเธชเธณเน€เธฃเนเธ (เนเธเนเน€เธเนเธ buffer เธฃเธงเธกเธ—เธธเธเธเนเธญเธเธ—เธฒเธเธเนเธฒเธข)
int chanelPay = 0; bool chanelcoinStatus = false;
int minn_countdown_wait = 0;
int second_countdown_wait = 0;
bool status_countdown_wait = false;
bool status_machine_run = false;
bool status_machine_prepare = false;
int program = 0;
int value_str1_int;

// เนเธเธฃเนเธกเธเธฑเนเธเนเธเธเธซเธฅเธฒเธขเธเนเธงเธเน€เธงเธฅเธฒ/เธซเธฅเธฒเธขเธงเธฑเธ (เนเธเธฃเธเธชเธฃเนเธฒเธเน€เธ”เธตเธขเธงเธเธฑเธเนเธเธฃเน€เธเธเธ•เน ATD_TM_V2_New_Hier)
struct PromoSlot {
  uint8_t day;       // 0=Sun..6=Sat (เธ•เธฒเธก rtc.getDayofWeek())
  uint8_t startHour; // 0-23
  uint8_t startMin;  // 0-59
  uint8_t endHour;   // 0-23
  uint8_t endMin;    // 0-59
  int pricePro[3];   // เธฃเธฒเธเธฒเนเธเธฃ 3 เนเธเธฃเนเธเธฃเธกเน€เธเธเธฒเธฐเธเนเธงเธเธเธตเน
};

const int MAX_PROMO_SLOTS = 10;
PromoSlot promoSlots[MAX_PROMO_SLOTS];
int promoSlotCount = 0;

//mqtt
const char* mqtt_server;
int mqtt_port;
const char* mqtt_server1 = "mawell.thddns.net"; // server
const char* mqtt_server2 = "broker.mqtt.cool"; // server
const char* mqtt_username = "mawell"; // replace with your Username
const char* mqtt_password = "4460"; // replace with your Password
int mqtt_port1 = 4741; // เน€เธฅเธ port
int mqtt_port2 = 1883; // เน€เธฅเธ port

int gid = 99;
String IDserver = "94";//ID for sharepoint
int CodeMachine = 0;
int Mode = 2;
String topic = "V" + String(gid);
// String userID = "ai_touch";
String userID = "ai_old";
String Noserial = "65M000000";


//qr payment
bool statusqr = true;
String cm;
String value_str1;
String value_str2;
String idSql;
lv_obj_t * qrcode;
static lv_timer_t * timer;
// int value_int;

//mills()
unsigned long timerstanby;

//var global
int chanel = 0;

// int Cost[] = {50,60,70};
// int hrs; int minn;
String StatusControl;
int Screen = 1;
int mqttStatus = 1;

//var timer
int hrs = 0; int minn = 0; int second = 0;
int step = 0;
bool pause_timer = false;
int count_minn_pass = 0;

//var program // lg 11kg
int program1[] = {3,2,1}; //step1
int program2[] = {3,0,1}; //step1
int program3[] = {1,2,1}; //step1
int rinStep2[] = {7,1}; //rin step2
int rincommand[] = {7,1}; //rin command
int spin = 6; //step3

int drum[] = {6, 0, 0}; // drum only

int check_runing_time[] = {19,13,5}; // Time off Quick Program

int TimeCountdowndrum[] = {1,30};
int TimeCountdown1[] = {0,30};
int TimeCountdown2[] = {0,30};
int TimeCountdown3[] = {0,30}; 

//var dry
int TimerA;
int timerDry[] = {1, 2, 3};

// sensor
int ldr_set = 3000;

// var reset
bool endProgram = false;

//var error
int state_error = 0;

//admin
// const char * passAdmin  = "4460";
String passAdmin  = "4460";
bool stateChange_passAdmin  = false;

//setting
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

// var oti โ€” server/port เธญเนเธฒเธเธเธฒเธ Preferences เธซเธฃเธทเธญ configResponse; เธเนเธฒเน€เธฃเธดเนเธกเธ•เนเธเธ”เนเธฒเธเธฅเนเธฒเธ (เนเธเธเธ—เธตเน 1 Cloudflare / เนเธเธเธ—เธตเน 2 เน€เธเธดเธฃเนเธเน€เธงเธญเธฃเนเน€เธ”เธดเธก)

bool UpdateFw = false;
String Path_GetVersion = "/ota/version";
String Path_OTI = "/ota/download";
String api_key = "cf860590807a21db3be15ae3f99f706b";
String server = "backend.ma-well.com"; // Melody OTA (Cloudflare โ’ backend)
String host = server;
int port = 80;

// === Melody backend (HTTP fallback เน€เธกเธทเนเธญ MQTT เธฅเนเธก) ===
// เธเธฃเธฑเธ melodyServer/melodyPort เนเธซเนเธ•เธฃเธเธเธฑเธ host เธเธฃเธดเธเธ—เธตเน deploy backend (เธเนเธฒเน€เธฃเธดเนเธกเธ•เนเธเธ•เธฒเธก ATD_TM)
String Path_MqttReport = "/public/machines/mqtt-report";
String Path_DeviceAck = "/public/machines/device-ack";
String Path_UpdateState = "/public/machines/update-state";
/** รายรับทาง HTTP (แทน MQTT postSQL) — idempotent ด้วย txnId กันซ้ำตอน retry */
String Path_DeviceRevenue = "/public/machines/device-revenue";
String melodyServer = "backend.ma-well.com";
int melodyPort = 80;
long contentLength = 0;
bool isValidContentType = false;
// เธ”เธถเธ config เนเธเน MQTT (configRequest/configResponse) เน€เธ—เนเธฒเธเธฑเนเธ เธ–เนเธฒเนเธกเนเธกเธตเธเนเธญเธกเธนเธฅเนเธเนเธเนเธฒเธเธฒเธเนเธฃเธเธเธฒเธ
String ServerSentBalanceV3 = "https://prod-10.southeastasia.logic.azure.com:443/workflows/e2fb5b61149c46bf93c39badf52cfacf/triggers/manual/paths/invoke?api-version=2016-06-01&sp=%2Ftriggers%2Fmanual%2Frun&sv=1.0&sig=MneSpDjmFs8LZpsz0DhH0A-M3uDP9XXrjZA83Yb11j4";
bool stateGetdata = 0;
bool stateSetupdata = 0;
bool stateSendConfigMqtt = false;  // เน€เธกเธทเนเธญเธฃเธฑเธ getdata เธเธฒเธเนเธญเธ”เธกเธดเธ เนเธซเนเธชเนเธ config เธเธฅเธฑเธเธ—เธฒเธ MQTT (topic getdataResponse) เน€เธซเธกเธทเธญเธ ATD_TM_V2_New_Hier
bool stateUpdateState = 0;
// เธชเนเธ status (UpdateState) เธ—เธธเธ N เธเธฒเธ—เธต เธเธ“เธฐเน€เธเธฃเธทเนเธญเธเธ—เธณเธเธฒเธ โ€” เธเธฃเธฑเธเนเธ”เนเธเธฒเธเธเธญเธฃเนเธก ESP32 (เธเนเธฒเน€เธฃเธดเนเธกเธ•เนเธ 5)
int statusReportIntervalMinutes = 5;
bool stateSentPriceServer = 0;
String TimeSent;
String mqttPayloadBuffer;  // เน€เธเนเธ payload เธเธฒเธ MQTT เน€เธกเธทเนเธญเธฃเธฑเธ setup เธซเธฃเธทเธญ configResponse
bool stateUpdateFw = 0;
// bool stateUpdatTime = 0;

int priceSentVerver = 0;
String data;
bool statewifi = false;
bool stateReset = false;
bool drain_water = false;
int stepHier = 0;
bool stateIntime = false;
int ldrMinus = 1000; // เธ•เธฑเธงเนเธเธฃเน€เธเนเธเธเนเธฒ LDR เธ—เธตเนเนเธเนเนเธเธเธฒเธฃเธฅเธเธเธฒเธเธเนเธฒ LDR เธซเธฅเธฑเธ
// เน€เธเธดเธ”/เธเธดเธ” WiFi (taskWifiMqtt เนเธเนเน€เธเนเธเธเนเธญเธเธ—เธณ OTA/MQTT/HTTP)
bool state_wifi_on = true;

bool stateCheckLdr1 = false; // เธ•เธฑเธงเนเธเธฃเน€เธเนเธเธชเธ–เธฒเธเธฐเธเธฒเธฃเธ•เธฃเธงเธเธชเธญเธ LDR
bool stateCheckLdr2 = false; // เธ•เธฑเธงเนเธเธฃเน€เธเนเธเธชเธ–เธฒเธเธฐเธเธฒเธฃเธ•เธฃเธงเธเธชเธญเธ LDR
int chanelLdrCheck = 0; // เธ•เธฑเธงเนเธเธฃเน€เธเนเธเธเนเธญเธ LDR เธ—เธตเนเธ•เนเธญเธเธ•เธฃเธงเธเธชเธญเธ
int stepLdrCheck = 0; // เธ•เธฑเธงเนเธเธฃเน€เธเนเธเธเนเธฒ LDR เธ—เธตเนเธ•เนเธญเธเธ•เธฃเธงเธเธชเธญเธ
int displaystandbyLdrCheck = 0; // เธ•เธฑเธงเนเธเธฃเน€เธเนเธเธชเธ–เธฒเธเธฐเธเธฒเธฃเนเธชเธ”เธเธเธฅ LDR

int screenUse = 1; // เธ•เธฑเธงเนเธเธฃเน€เธเนเธเธซเธเนเธฒเธเธญเธ—เธตเนเนเธเนเธเธฒเธ
int stateLdr1Screen = 0; // เธ•เธฑเธงเนเธเธฃเน€เธเนเธเธชเธ–เธฒเธเธฐเธเธฒเธฃเนเธชเธ”เธเธเธฅ LDR1
int stateLdr2Screen = 0; // เธ•เธฑเธงเนเธเธฃเน€เธเนเธเธชเธ–เธฒเธเธฐเธเธฒเธฃเนเธชเธ”เธเธเธฅ LDR2
bool state_status_machine_run = false; // เธ•เธฑเธงเนเธเธฃเน€เธเนเธเธชเธ–เธฒเธเธฐเธเธฒเธฃเธ—เธณเธเธฒเธเธเธญเธเน€เธเธฃเธทเนเธญเธ

bool stateWhile = false;
bool stateUpdateBalanceDry = false; // เธ•เธฑเธงเนเธเธฃเน€เธเนเธเธชเธ–เธฒเธเธฐเธเธฒเธฃเธญเธฑเธเน€เธ”เธ—เธเนเธฒเธเธฒเธฃเธ•เนเธญเน€เธงเธฅเธฒ

bool state_relay = false;

// Pending UI: เนเธซเน taskWifiMqtt เธ•เธฑเนเธเธเนเธฒ เนเธฅเนเธง taskDisplay เน€เธเนเธเธเธเธญเธฑเธเน€เธ”เธ• LVGL เน€เธ—เนเธฒเธเธฑเนเธ (เธเนเธญเธเธเธฑเธเธซเธฅเธฒเธข task เนเธ•เธฐ LVGL)
#define PENDING_UI_NONE            0
#define PENDING_UI_LABEL_MSG       1  // เนเธชเธ”เธเธเนเธญเธเธงเธฒเธก ui_Label1, ui_Label2
#define PENDING_UI_FIRST_SCREEN    2  // เน€เธฃเธตเธขเธ addFlagstoFirstscreen()
#define PENDING_UI_SHOW_RUN        3  // เน€เธเธดเธ”เธซเธเนเธฒเธงเธดเนเธเนเธเธฃเนเธเธฃเธก (screenUse=6, setStartMachine)
#define PENDING_UI_SHUTDOWN        4  // เธซเธเนเธฒ Shutdown (เธเนเธญเธเธเธธเนเธก, เนเธชเธ”เธเธเนเธญเธเธงเธฒเธก)
#define PENDING_UI_RESTART         5  // เธซเธเนเธฒ Restart (เนเธชเธ”เธเธเธธเนเธก, เนเธชเธ”เธเธเนเธญเธเธงเธฒเธก)
#define PENDING_UI_SLOT_MSG        6  // เธเนเธญเธเธงเธฒเธกเน€เธเธฅเธตเนเธขเธเธเนเธญเธ coin
#define PENDING_UI_UPDATE_MSG      7  // เธเนเธญเธเธงเธฒเธกเธเธณเธฅเธฑเธเธญเธฑเธเน€เธ”เธ—
#define PENDING_UI_REBOOT_MSG      8  // เธเนเธญเธเธงเธฒเธกเธเธณเธฅเธฑเธ Reboot
#define PENDING_UI_LDR_CLOSE       9  // เธเธทเธเธซเธเนเธฒเธเธญเธซเธฅเธฑเธ LdrClose (เนเธเน stateLdr1Screen)
#define PENDING_UI_BT1_HOME        10 // เธเธธเนเธก BT1 เธเธฅเธฑเธเธซเธเนเธฒเนเธฃเธ
#define PENDING_UI_BT4_SETTING     11 // เธเธธเนเธก BT4 เน€เธเธดเธ”เน€เธกเธเธนเธ•เธฑเนเธเธเนเธฒ
#define PENDING_UI_DISPLAY_SETTING      12 // เธญเธฑเธเน€เธ”เธ• ui_lb_display_setting (เธเนเธญเธเธงเธฒเธกเน€เธ”เธตเธขเธง)
#define PENDING_UI_QR_SUCCESS           13 // QR เธเธณเธฃเธฐเธชเธณเน€เธฃเนเธ (เธเนเธญเธ S5 เนเธชเธ”เธ S6 + logic เธ•เนเธญเน€เธงเธฅเธฒ/เน€เธฃเธดเนเธกเน€เธเธฃเธทเนเธญเธ)
#define PENDING_UI_QR_GEN               14 // เธชเธฃเนเธฒเธ QR code เธเธฒเธ payload
#define PENDING_UI_SETTING_EXIT_COMMAND 15 // เธญเธญเธเธเธฒเธเธซเธเนเธฒ setting เธเธฅเธฑเธเธซเธเนเธฒเธเธญเธเธณเธชเธฑเนเธ (ui_con_command)
#define PENDING_UI_RESUME_RUN           16 // กู้รอบหลังรีบูท: โชว์หน้า run (screenUse=6) โดยไม่ setStartMachine (ใช้ timer ที่กู้มา)
int pendingUIAction = PENDING_UI_NONE;
String pendingLabel1 = "";
String pendingLabel2 = "";
int pendingProgram = 0;
String pendingQrPayload = "";      // เนเธเนเธเธฑเธ PENDING_UI_QR_GEN

#endif /* VARABLE_H_ */
