#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncMqttClient.h>
#include <Ticker.h>

#ifndef STRUCT_TREE
#define STRUCT_TREE
// #include <relay_model.cpp>
#endif

#include <FS.h>
#include <LITTLEFS.h>

#define FileFS LittleFS

void connect_wifi();

// ********************************* 继电器部分 *********************************
#include <Arduino.h>
#include "gpio.h"

#include <user_interface.h>

// void espSleep(uint64_t seconds)
// {
//     uint64_t USEC = 1000000;
// #ifdef ESP32
//     Serial.printf("Going to sleep %llu seconds\n", seconds);
//     esp_sleep_enable_timer_wakeup(seconds * USEC);
//     esp_deep_sleep_start();
// #elif ESP8266
//     Serial.printf("Going to sleep %llu s. Waking up only if D0 is connected to RST \n", seconds);
//     ESP.deepSleep(seconds * USEC); // 3600e6 = 1 hour in seconds / ESP.deepSleepMax()
// #endif
// }

// BTN DEF
#define BTN 0
#define LED 2

// STATUS DEF
#define Close 1
#define Open 0

/*
var BTNTimer = 0;
function powerBtn(delay) {
  if (delay > 0) {
    console.log(`power press. delay: ${delay}`);
    clearTimeout(BTNTimer);
    digitalWrite(BTN, Open);
    digitalWrite(LED, 1);
    BTNTimer = setTimeout(() => {
      digitalWrite(BTN, Close);
      digitalWrite(LED, 0);
    }, delay);
  }
}

// service
function handle(msg) {
  if (msg == 'short') {
    powerBtn(shortDelay);
  } else if (msg == 'long') {
    powerBtn(6000);
  } else {
    var delay = Number(msg);
    powerBtn(delay);
  }
}
*/

static uint8 RELAY_IF_EXEC = 0;

static os_timer_t os_timer;

void _open_btn()
{
  RELAY_IF_EXEC = 1;
  Serial.println("step1: Open");
  digitalWrite(BTN, Open);
  digitalWrite(LED, 1);
}

void _close_btn()
{
  digitalWrite(BTN, Close);
  digitalWrite(LED, 0);
  Serial.println("step3: Close");
  RELAY_IF_EXEC = 0;
}

bool power_btn(unsigned long delay)
{
  if (delay < 0 || delay > 60 * 1000)
  {
    return false;
  }
  if (RELAY_IF_EXEC)
  {
    return false;
  }
  Serial.printf("power press. delay: %ld \r\n", delay);
  Serial.println("step0: Clear timer");
  os_timer_disarm(&os_timer);

  // 开启
  _open_btn();

  // 设置定时，一段时间后关闭
  Serial.println("step2: Sleep");
  os_timer_setfn(&os_timer, (ETSTimerFunc *)(_close_btn), NULL);
  os_timer_arm(&os_timer, delay, false);
  return true;
}

// ********************************* 继电器部分END *********************************

// ********************************* MQTT部分 *********************************

const String CONFIG_FILENAME = "mqtt_conf.dat";

struct MQTT_CONFIG
{
  bool enable;
  char host[64];
  int port;
  char username[32];
  char password[32];
  char subscribe_topic[128];
};

MQTT_CONFIG config;

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

void saveConfigData()
{
  File file = FileFS.open(CONFIG_FILENAME, "w");
  Serial.println(F("SaveMqttCfgFile"));

  if (file)
  {
    file.write((uint8_t *)&config, sizeof(config));

    file.close();
    Serial.println(F("OK"));
  }
  else
  {
    Serial.println(F("failed"));
  }
}

void readConfigData()
{
  File file = FileFS.open(CONFIG_FILENAME, "r");
  Serial.println(F("ReadMqttCfgFile "));

  if (file)
  {
    file.read((uint8_t *)&config, sizeof(config));

    file.close();
    Serial.printf("Read OK. host: %s, topic: %s \r\n", config.host, config.subscribe_topic);
  }
  else
  {
    Serial.println(F("failed. file open error! create empty config file."));
    MQTT_CONFIG *new_config = (struct MQTT_CONFIG *)malloc(sizeof(MQTT_CONFIG));
    config = *new_config;
    saveConfigData();
  }
}

void connectToMqtt()
{
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onWifiConnect(const WiFiEventStationModeGotIP &event)
{
  Serial.println("Connected to Wi-Fi.");
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected &event)
{
  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connect_wifi);
}

void onMqttConnect(bool sessionPresent)
{
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);

  uint16_t packetIdSub = mqttClient.subscribe(config.subscribe_topic, 1);
  Serial.print("Subscribing at QoS 1, packetId: ");
  Serial.println(packetIdSub);

  uint16_t packetIdPub1 = mqttClient.publish("device/login", 1, true, "1");
  Serial.print("Publishing at QoS 1, packetId: ");
  Serial.println(packetIdPub1);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected())
  {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos)
{
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void onMqttUnsubscribe(uint16_t packetId)
{
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
  Serial.println("Publish received.");
  Serial.print("  topic: ");
  Serial.println(topic);
  Serial.print("  qos: ");
  Serial.println(properties.qos);
  Serial.print("  dup: ");
  Serial.println(properties.dup);
  Serial.print("  retain: ");
  Serial.println(properties.retain);
  Serial.print("  len: ");
  Serial.println(len);
  Serial.print("  index: ");
  Serial.println(index);
  Serial.print("  total: ");
  Serial.println(total);

  if (strcmp(topic, config.subscribe_topic) == 0)
  {
    unsigned long delay = strtoul(payload, NULL, 0);
    power_btn(delay);
  }
  else
  {
    Serial.printf("not match. [%s]%d  [%s]%d  [%d].", topic, sizeof(topic), config.subscribe_topic, sizeof(config.subscribe_topic), strcmp(topic, config.subscribe_topic));
  }
}

void onMqttPublish(uint16_t packetId)
{
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void init_mqtt()
{
  // Initialize LittleFS
  if (!LittleFS.begin())
  {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  readConfigData();

  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setWill("device/logout", 1, false, "1", 1);

  if (config.enable && config.username && config.username[0])
  {
    mqttClient.setCredentials(config.username, config.password);
  }

  if (config.enable && config.host && config.host[0])
  {
    Serial.printf("Load Mqtt on %s:%d \r\n", config.host, config.port);
    mqttClient.setServer(config.host, config.port);
  }
  else
  {
    Serial.println("Mqtt is ignore");
  }
}

// ********************************* MQTT部分END *********************************

// ********************************* 配网部分END *********************************

void smartconfig_start()
{
  WiFi.mode(WIFI_STA);
  Serial.println("\r\nWait for Smartconfig等待连接");
  delay(500);
  // 等待配网
  WiFi.beginSmartConfig();

  while (1)
  {
    Serial.print(".");
    delay(500);
    if (WiFi.smartConfigDone())
    {
      Serial.println("SmartConfig Success");
      Serial.printf("SSID:%s\r\n", WiFi.SSID().c_str());
      Serial.printf("PSW:%s\r\n", WiFi.psk().c_str());
      WiFi.setAutoConnect(true); // 设置自动连接
      break;
    }
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

// // Replaces placeholder with LED state value
// String processor(const String &var)
// {
//   Serial.println(var);
//   if (var == "STATE")
//   {
//   }
//   else if (var == "TEMPERATURE")
//   {
//     return getTemperature();
//   }
// }

station_config s_staconf;

void printWifiData()
{
  Serial.print("当前工作模式:"); // 告知用户设备当前工作模式
  Serial.println(WiFi.getMode());
  // print your WiFi shield's IP address
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print your MAC address
  byte mac[6];
  WiFi.macAddress(mac);
  char buf[20];
  sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
  Serial.print("MAC address: ");
  Serial.println(buf);

  // print the SSID of the network you're attached to
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print the received signal strength
  long rssi = WiFi.RSSI();
  Serial.print("Signal strength (RSSI): ");
  Serial.println(rssi);
}

void connect_wifi()
{
  wifi_set_opmode(STATION_MODE);
  WiFi.setAutoConnect(true);

  wifi_station_get_config_default(&s_staconf);
  int count = 0;
  if (s_staconf.ssid && s_staconf.ssid[0])
  {
    WiFi.begin();
    Serial.printf("Connecting to WiFi. ssid: %s, password: %s ", s_staconf.ssid, s_staconf.password);
    while (WiFi.status() != WL_CONNECTED && count++ < 30)
    {
      Serial.printf(".");
      delay(1000);
    }
    Serial.printf("Connected WiFi. status: %d\r\n", WiFi.status());
  }
  if (!(s_staconf.ssid && s_staconf.ssid[0]) || count > 30)
  {
    Serial.println("Enter SmartConfig");
    smartconfig_start();
  }

  // Print ESP32 Local IP Address
  Serial.println("Wifi Connected.");
  delay(500);
  printWifiData();
}

// ********************************* 配网部分END *********************************

AsyncWebServer server(80);

void setup()
{
  // Serial port for debugging purposes
  Serial.begin(115200);

  // pin init
  _close_btn();
  pinMode(BTN, OUTPUT);
  pinMode(LED, OUTPUT);

  Serial.printf("\r\nStartup. \r\n");

  // read and init mqtt
  init_mqtt();

  // Connect to Wi-Fi
  connect_wifi();

  // Route for root / web page
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.serveStatic("/images/", LittleFS, "/images/");
  server.serveStatic("/css/", LittleFS, "/css/");
  server.serveStatic("/scripts/", LittleFS, "/scripts/");

  // Route to set GPIO to HIGH
  server.on("/power_btn", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              //List all parameters
              AsyncWebParameter *delayParam = request->getParam("delay");
              Serial.printf("[GET] /power_btn delay: %s \r\n", delayParam->value().c_str());
              unsigned long delay = strtoul(delayParam->value().c_str(), NULL, 0);
              if (!power_btn(delay))
              {
                char *body = (char *)malloc(24);
                sprintf(body, "delay error. %lu", delay);
                request->send_P(500, "text/plain", body);
              }
              request->send_P(200, "text/plain", "success");
            });

  server.on("/mqtt", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              String enable;
              if (config.enable)
              {
                enable = "true";
              }
              else
              {
                enable = "false";
              }
              char *body = (char *)malloc(512);
              sprintf(body,
                      "{\"enable\": %s, \"host\": \"%s\", \"port\": %d, \"username\": \"%s\", \"password\": \"%s\", \"topic\": \"%s\"}",
                      enable.c_str(),
                      config.host,
                      config.port,
                      config.username,
                      config.password,
                      config.subscribe_topic);
              request->send_P(200, "application/json", body);
            });

  server.on("/mqtt", HTTP_POST, [](AsyncWebServerRequest *request)
            {
              AsyncWebParameter *enableParam = request->getParam("enable", true);
              AsyncWebParameter *hostParam = request->getParam("host", true);
              AsyncWebParameter *portParam = request->getParam("port", true);
              AsyncWebParameter *usernameParam = request->getParam("username", true);
              AsyncWebParameter *passwordParam = request->getParam("password", true);
              AsyncWebParameter *topicParam = request->getParam("topic", true);
              Serial.printf("[POST] /mqtt ost: %s, port: %s, username: %s, password: %s, topic: %s \r\n",
                            hostParam->value().c_str(),
                            portParam->value().c_str(),
                            usernameParam->value().c_str(),
                            passwordParam->value().c_str(),
                            topicParam->value().c_str());

              // check
              if (!(hostParam->value().c_str() && hostParam->value().c_str()[0]))
              {
                request->send_P(500, "text/plain", "host不能为空");
                return;
              }
              if (!(portParam->value().c_str() && portParam->value().c_str()[0]))
              {
                request->send_P(500, "text/plain", "port不能为空");
                return;
              }
              if (strlen(hostParam->value().c_str()) > 64)
              {
                request->send_P(500, "text/plain", "host过长");
                return;
              }

              const char *enableStr = enableParam->value().c_str();
              const char *host = hostParam->value().c_str();
              int port = atoi(portParam->value().c_str());
              const char *username = usernameParam->value().c_str();
              const char *password = passwordParam->value().c_str();
              const char *subscribe_topic = topicParam->value().c_str();

              bool enable = false;
              if (strcmp(enableStr, "true") == 0)
              {
                enable = true;
              }
              else if (strcmp(enableStr, "false") == 0)
              {
                enable = false;
              }
              else
              {
                request->send_P(500, "text/plain", "enable非法");
                return;
              }

              if (port > 65534 || port < 1)
              {
                request->send_P(500, "text/plain", "port非法");
                return;
              }

              config.enable = enable;
              strcpy(config.host, host);
              config.port = port;
              strcpy(config.username, username);
              strcpy(config.password, password);
              strcpy(config.subscribe_topic, subscribe_topic);
              saveConfigData();
              init_mqtt();
              request->send_P(200, "text/plain", "success. 等待设备连接");
              ESP.restart();
            });

  // Start server
  server.begin();
  Serial.printf("HTTP Server started. port: 80. IP: %s \r\n", WiFi.localIP().toString().c_str());
}

void loop()
{
  delay(10);
}
