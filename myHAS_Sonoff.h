#ifndef myHAS_Sonoff
#define myHAS_Sonoff

//Comment for not having serial trace (prod)
#define _DEBUG_

//Uncomment for using socket as wifi extender
//#define ACTIVATE_EXTENDER
//#define NAPT 1000
//#define NAPT_PORT 10

#define ADDRESS_SENSOR 480

#define MAKESTRING2(x) #x
#define MAKESTRING(x) MAKESTRING2(x)

#define mk_OTA_NAME2(x) Sonoff_ ##x
#define mk_OTA_NAME(x) mk_OTA_NAME2(x)
#define OTA_NAME MAKESTRING(mk_OTA_NAME(PRISE_NB))
#define PRISE_ID 20000 + PRISE_NB *10 + 0
#define TEMP_ID 20000 + PRISE_NB *10 + 1

#endif
