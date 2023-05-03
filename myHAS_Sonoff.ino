/*
 * TODO: Bug: Rules list is often not complete String/Memory problem probably
 */

//Les prises paires ont un capteur de temperature, les impaires n'en ont pas
#define PRISE_NB 4

#include "myHAS_Sonoff.h"
#include <ESP8266WiFi.h>

#ifdef ACTIVATE_EXTENDER
#include <lwip/napt.h>
#include <lwip/dns.h>

#include <dhcpserver.h>
#endif

#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>

#include <PubSubClient.h>

#include <Environment.h>
#include <ConnectedObjects.h>
#include <WebPage.h>
#include <Logging.h>
#include <Settings.h>

AsyncWebServer server(80);
//Flag that indicates web browser to refresh
bool needRefresh = false;
unsigned long wifiReconnectTime = 0;
//Flag to indicate if prise is a wifi client or an access point
bool wifiAP = false;

WiFiClient wifiClientEnv;
WiFiClient wifiClientPrise;
#if PRISE_NB % 2 == 0
  WiFiClient wifiClientSensor;
#endif
PubSubClient mqttClientEnv(wifiClientEnv);
PubSubClient mqttClientPrise(wifiClientPrise);
#if PRISE_NB % 2 == 0
  PubSubClient mqttClientSensor(wifiClientSensor);
#endif
Settings *mySettings = new Settings();

Environment *myEnv = new Environment(&mqttClientEnv, PRISE_ID);
Logging *myLog = new Logging(PRISE_ID);

WebPage *pWebPage = new WebPage();

PriseIOT_ESP *myPrise = new PriseIOT_ESP(&mqttClientPrise, PRISE_ID);

//#if PRISE_NB==2 || PRISE_NB==4
#if PRISE_NB % 2 == 0
  TempSensorDS18B20 *tempSensor = new TempSensorDS18B20(&mqttClientSensor, TEMP_ID, 1, ADDRESS_SENSOR);
#endif

int Objet::eepromSize{ 512 };
bool Objet::eepromInit{ false };

void callbackEnv(char* topic, byte* payload, unsigned int length) {
  myEnv->handleMqttCallback(topic, payload, length);
}
void callbackPrise(char* topic, byte* payload, unsigned int length) {
  myPrise->handleMqttCallback(topic, payload, length);
}
#if PRISE_NB % 2 == 0
void callbackSensor(char* topic, byte* payload, unsigned int length) {
  tempSensor->handleMqttCallback(topic, payload, length);
}
#endif

ICACHE_RAM_ATTR void onPressButton() 
{
  myPrise->toggle();
}

#ifdef ACTIVATE_EXTENDER
void initWifiExtender()
{
  // give DNS servers to AP side
  dhcps_set_dns(0, WiFi.dnsIP(0));
  dhcps_set_dns(1, WiFi.dnsIP(1));
  
  WiFi.softAPConfig(  // enable AP, with android-compatible google domain
    IPAddress(192, 168, 22, 1),
    IPAddress(192, 168, 22, 1),
    IPAddress(255, 255, 255, 0));
  WiFi.softAP(mySettings->getWifiSSID() + "extender", mySettings->getWifiPWD());
  err_t ret = ip_napt_init(NAPT, NAPT_PORT);
  if (ret == ERR_OK) {
    ret = ip_napt_enable_no(SOFTAP_IF, 1);
    if (ret == ERR_OK) {
#ifdef _DEBUG_  
      Serial.println("\nWifi extender ready");
#endif   
    }
  }
  if (ret != ERR_OK) {
#ifdef _DEBUG_  
      Serial.println("\nWifi extender initialization failed");
#endif
  }  
}
#endif

void checkWifi()
{
  //If wifi connection lost, try to reconnect every 15 seconds (because ESP will keep initiating the connection in the background). 
  if(WiFi.status() != WL_CONNECTED && millis()-wifiReconnectTime>15000)
  {
#ifdef _DEBUG_  
      Serial.print("Wifi connection lost, trying to reconnect");
#endif 
    //To not flood the log, raise up message every hour only
    if(millis()-wifiReconnectTime>3600000)
      myLog->addLogEntry("Wifi connection lost, trying to reconnect");

    WiFi.mode(WIFI_STA);
    WiFi.begin(mySettings->getWifiSSID(), mySettings->getWifiPWD());
    wifiReconnectTime = millis();

#ifdef ACTIVATE_EXTENDER
  initWifiExtender();
#endif

  }
  
}

//Set the ESP as an AP to configure it
void setWifiAP()
{
  wifiAP = true;
  WiFi.mode(WIFI_AP);
  IPAddress local_ip(192,168,0,1);
  IPAddress gateway(192,168,0,1);
  IPAddress subnet(255,255,255,0);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP("myHAS", "12345678");
}

void connectWifi(unsigned long iTimeOut = -1)
{
  if(mySettings->isWifiSetup())
  {
     if(WiFi.status() != WL_CONNECTED)
     {
        WiFi.mode(WIFI_STA);
        WiFi.begin(mySettings->getWifiSSID(), mySettings->getWifiPWD());
#ifdef _DEBUG_  
        Serial.print("Connecting to WiFi ");
        Serial.print(mySettings->getWifiSSID());
#endif 
        unsigned long currentMillis = millis();
        while (WiFi.status() != WL_CONNECTED && ((unsigned long)(millis()-currentMillis)<iTimeOut || iTimeOut==-1) )
        {
          delay(500);
#ifdef _DEBUG_  
          Serial.print(".");
#endif    
        }

        if(WiFi.status() != WL_CONNECTED)
        {
          WiFi.disconnect();
          setWifiAP();
        }

#ifdef _DEBUG_  
        Serial.println("\nConnected !");
#endif     

#ifdef ACTIVATE_EXTENDER
        initWifiExtender();
#endif
     }
  }
  else
  {
    setWifiAP();
  }
  
  
}

void setup()
{    
  Serial.begin(115200);
  blinkOnce();
  myEnv->setLog(myLog);
  String wifiList = "";
  int nbNetworks = WiFi.scanNetworks();
 
  for(int i =0; i<nbNetworks; i++){
      if(wifiList.indexOf(WiFi.SSID(i))==-1)
        wifiList += WiFi.SSID(i)+";";
  }
  mySettings->setWifiList(wifiList);

  connectWifi(30000);

  blinkOnce();
  
  // Print Local IP Address
#ifdef _DEBUG_  
Serial.println(WiFi.localIP());
#endif

  initiatlizeWebServer();
  if(!wifiAP)
  {
    ArduinoOTA.onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
  ArduinoOTA.setHostname(OTA_NAME);
  ArduinoOTA.setPassword(mySettings->getOTAPWD());
  ArduinoOTA.begin();
    
  //initialize MQTT clients
  //mqttClientEnv.setServer(mySettings->getMqttServer().c_str(), mySettings->getMqttPort());
  myEnv->setMqttServer(mySettings->getMqttServer(), mySettings->getMqttPort(), mySettings->getMqttLogin(), mySettings->getMqttPWD());
  mqttClientEnv.setCallback(callbackEnv);

  //mqttClientPrise.setServer(mySettings->getMqttServer().c_str(), mySettings->getMqttPort());
  myPrise->setMqttServer(mySettings->getMqttServer(), mySettings->getMqttPort(), mySettings->getMqttLogin(), mySettings->getMqttPWD());
  mqttClientPrise.setCallback(callbackPrise);
  
  myPrise->setEnv(myEnv);
  myPrise->setLog(myLog);
  myPrise->init();

#if PRISE_NB % 2 == 0
  //mqttClientSensor.setServer(mySettings->getMqttServer().c_str(), mySettings->getMqttPort());
  tempSensor->setMqttServer(mySettings->getMqttServer(), mySettings->getMqttPort(), mySettings->getMqttLogin(), mySettings->getMqttPWD());
  mqttClientSensor.setCallback(callbackSensor);
  tempSensor->setEnv(myEnv);
  tempSensor->setLog(myLog);
  tempSensor->init();
#endif

  pWebPage->setEnv(myEnv);
  pWebPage->setLog(myLog);
  pWebPage->aPrises.add(myPrise, myPrise->Id);
#if PRISE_NB % 2 == 0
  pWebPage->aSensors.add(tempSensor, tempSensor->Id);
#endif
  pWebPage->setTitle(String(myPrise->Id));
  attachInterrupt(digitalPinToInterrupt(0), onPressButton, RISING);
  }
  blinkOnce();
}

void loop()
{
  if(!wifiAP)
  {
    checkWifi();
    ArduinoOTA.handle();
    myEnv->update();
    myPrise->update();
#if PRISE_NB % 2 == 0
    tempSensor->update();
#endif
  }
}

void blinkOnce()
{
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
}

void initiatlizeWebServer()
{
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if(wifiAP) request->redirect("/settings");
    else
    {
      //String sIndexPage = pWebPage->getIndexHTML();
      //request->send_P(200, "text/html", sIndexPage.c_str());
      request->send(SPIFFS, pWebPage->getIndexHTML_file(), "text/html");
    }
  });

  server.on("/log", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, LOG_FILE_PATH, "text/plain");
  });

  server.on("/refresh", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", String(needRefresh).c_str());
    needRefresh = false;
  });

  //Click on edit rules
  server.on("/rules", HTTP_GET, [](AsyncWebServerRequest *request){
    int iID = request->getParam("ID")->value().toInt();
    //request->send_P(200, "text/html", pWebPage->getRulesHTML(iID).c_str());
    request->send(SPIFFS, pWebPage->getRulesHTML_file(iID), "text/html");
  });

  server.on("/saveRules", HTTP_POST, [](AsyncWebServerRequest *request){
    //Publish rules
    int iID = request->getParam("ID", true)->value().toInt();
    myPrise->jsonToRules(request->getParam("output", true)->value());
    myPrise->publishRules();
    myPrise->stopBlink();
    
    request->redirect("/");
  });

  //Change a button status in Webpage
  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request){
#ifdef _DEBUG_      
    Serial.printf("Number of GET param = %d\n", (int)(request->params()));
#endif
    for(int i=0; i<request->params(); i++)
    {
      String paramName = request->getParam(i)->name();
      String paramValue = request->getParam(i)->value();
Serial.printf("Param %d name = %s value = %s\n", i, paramName.c_str(), paramValue.c_str());
      String objID = paramName.substring(paramName.indexOf('_')+1, paramName.lastIndexOf('_'));
      if(paramName.startsWith("prise_"))
      {
        if(paramName.endsWith("_name"))
        {
          if(myPrise->name != paramValue) 
          {
            myPrise->name = paramValue;
            myPrise->publishName();
          }
        }
        else if(paramName.endsWith("_status"))
        {
          if(pWebPage->aPrises.getItem(objID.toInt())->status ^ paramValue.toInt()) {
            myPrise->toggle();
          }
        }
      }
#if PRISE_NB % 2 == 0
      else if(paramName.startsWith("sensor_")) 
      {
        if(paramName.endsWith("_name"))
        {
          if(tempSensor->name != paramValue) 
          {
            tempSensor->name = paramValue;
            tempSensor->publishName();            
          }
        }
      }
#endif      
    }
    request->redirect("/");
  });

  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request){
    String sIndexPage = mySettings->getSettingsHtml();
    request->send_P(200, "text/html", sIndexPage.c_str());
  });

  server.on("/updateSettings", HTTP_POST, [](AsyncWebServerRequest *request){
    //Publish rules
    mySettings->setWifiLogin(request->getParam("wifiSSID", true)->value(), request->getParam("wifiPWD", true)->value());
    mySettings->setmqttServer(request->getParam("mqttServer", true)->value(), request->getParam("mqttPort", true)->value().toInt(),
    request->getParam("mqttLogin", true)->value(), request->getParam("mqttPWD", true)->value());
    mySettings->setOTA(request->getParam("otaPWD", true)->value());
    mySettings->saveSettings();
    request->send_P(200, "text/html", "Restarting...");
    delay(5000);
    request->redirect("/");
    delay(500);
    ESP.restart();
  });

  server.begin();
}
