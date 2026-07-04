#ifndef MAIN_H_
#define MAIN_H_

void checkQrpaymentRead();
void mqttreqest();
int checkLightStart(int);
// void callback(char* topic, byte* payload, unsigned int length);
void mqttreconnect();
void setProgram();

void modeSetting();
void settingMode1();
void settingMode2();
void settingMode3();
void Anothersetting();
void Drysetting();
void updateStepIcon();
void prepareRunMachine();

void setRelayType();

void printTocore();

void GetData();
void GetSetupData();
void PublishConfigViaMqtt();  // ส่ง config ปัจจุบันไป topic getdataResponse
bool publishPresenceOnline();           // MQTT presence online (presence/{Noserial})
void publishPresenceOfflineGraceful();  // presence offline + disconnect ก่อน restart

void commandApp();

void SetTimerSend();
void SetStatusControl();

bool UpdateBalanceV3(int amount);

void CheckPromotion();

void writePreferences();
void readPreferences();
void writePreferencesfirst();
void readPreferencesfirst();
void sentVarjson();
void sentDatatoAdmin();

//oti
void otiUdate();
#endif