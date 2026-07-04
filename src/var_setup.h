// sent mqtt setup (template JSON สำหรับ MQTT setup; ยังไม่มีที่ include ใช้)
#ifndef VAR_SETUP_H_
#define VAR_SETUP_H_
#include <ArduinoJson.h>
const char* json = R"(
    {
        "cm" : "setup",
        "id" : "65M000000",
        "value_str1" : "V0",
        "value_str2" : {
            "id" : "65M000000",
            "gid" : "0",

            "program1[0]" : 3,
            "program1[1]" : 2,
            "program1[2]" : 1,
            
            "program2[0]" : 3,
            "program2[1]" : 0,
            "program2[2]" : 1,

            "program3[0]" : 1,
            "program3[1]" : 2,
            "program3[2]" : 1,

            "rinStep2[0]" : 7,
            "rinStep2[1]" : 0,

            "rincommand[0]" : 8,
            "rincommand[1]" : 0,

            "spin" : 6,

            "drum[0]" : 7,
            "drum[1]" : 0,
            "drum[2]" : 0,

            "check_runing_time[0]" : 19,
            "check_runing_time[1]" : 13,
            "check_runing_time[2]" : 5,

            "TimeCountdowndrum[0]" : 1,
            "TimeCountdowndrum[1]" : 30,

            "TimeCountdown1[0]" : 0,
            "TimeCountdown1[1]" : 30,

            "TimeCountdown2[0]" : 0,
            "TimeCountdown2[1]" : 30,

            "TimeCountdown3[0]" : 0,
            "TimeCountdown3[1]" : 30
        }
    }
)";

#endif /* VAR_SETUP_H_ */
