// ESP32 - (MQTT) - Azure IoT Hub
// https://www.youtube.com/watch?v=aFSYOBvCTmw

#include <Adafruit_Sensor.h>
#include <DHT.h>

#include "Arduino.h"
// #include "soc/soc.h"          // Disable brownout problems
// #include "soc/rtc_cntl_reg.h" // Disable brownout problems
// #include "driver/rtc_io.h"

#include "time.h"

#include <WiFi.h>
#include <ArduinoMqttClient.h>
#include <ArduinoJson.h>

#include <ArduinoBearSSL.h>               // 필요한가?
#include <ArduinoECCX08.h>                // 필요한가?
#include <utility/ECCX08SelfSignedCert.h> // 필요한가?

#include "arduino_secrets.h"

////////// Enter your sensitive data in arduino_secrets.h
const char ssid[] = SECRET_WIFI_SSID;
const char pass[] = SECRET_WIFI_PASS;
const char broker[] = SECRET_BROKER;
String deviceId = SECRET_DEVICE_ID;
String devicePass = SECRET_DEVICE_PASSWORD;

const char *ntpServer = "pool.ntp.org"; // Cluster of timeservers; Anyone can use to request the time.
const long gmtOffset_sec = 3600 * 9;    // GMT offset; seoul: +09:00 == 3600*9
const int daylightOffset_sec = 0;       // Summertime; Not Used.

WiFiClient wifiClient; // Used for the TCP socket connection
// BearSSLClient sslClient(wifiClient); // Used for SSL/TLS connection, Integrates with ECC508
// MqttClient mqttClient(sslClient);
MqttClient mqttClient(wifiClient);

// Set your Board ID (ESP32 Sender #1 = BOARD_ID 1, ESP32 Sender #2 = BOARD_ID 2, etc)
#define BOARD_ID 1

// Digital pin connected to the DHT sensor
#define DHTPIN 4

// Uncomment the type of sensor in use:
// #define DHTTYPE    DHT11     // DHT 11
#define DHTTYPE DHT22 // DHT 22 (AM2302)
// #define DHTTYPE    DHT21     // DHT 21 (AM2301)

DHT dht(DHTPIN, DHTTYPE);

// 보내려는 데이터 유형이 포함된 구조체 -> JSON으로 대체
// Must match the receiver structure

unsigned int messageId = 1; // 전송된 판독값 수를 추적

unsigned long previousMillis = 0; // Stores last time temperature was published
const long interval = 5000;       // Interval at which to publish sensor readings 86400/12=7200 + wifi reconnect interval

float readDHTTemperature();
float readDHTHumidity();

void initWiFi();
void printLocalTime();
void initMQTT();
void publishMessage();
void publishTemperatureHumidity();
void onMessageReceived(int messageSize);

unsigned long getTime();

void setup()
{
  Serial.begin(115200); // Init Serial Monitor
  dht.begin();          // Init dht Sensor
  initWiFi();           // Init WiFi
  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime();
  /*
  configTime(0, 0, ntpServer);

    if (!ECCX08.begin())
    {
      Serial.println("No ECCX08 present!");
      while (1)
        ;
    }

    // Reconstruct the self signed cert; 자체 서명된 인증서 재구성
    ECCX08SelfSignedCert.beginReconstruction(0, 8);
    ECCX08SelfSignedCert.setCommonName(ECCX08.serialNumber());
    ECCX08SelfSignedCert.endReconstruction();

    // Set a callback to get the current time; 현재 시간 가져오는 콜백
    // Used to validate the servers certificate; 서버 인증서의 유효성 검사하는데 사용
    ArduinoBearSSL.onGetTime(getTime); // 함수 포인터를 인자로 받네

    // Set the ECCX08 slot to use for the private key; 개인 키(비밀 키)에 사용할 ECCX08 슬롯 설정
    // and the accompanying public certificate for it; 그리고 그걸 위해 첨부된 공인 인증서
    sslClient.setEccSlot(0, ECCX08SelfSignedCert.bytes(), ECCX08SelfSignedCert.length());
  */
  // Set the username to "<broker>/<device id>/api-version=2018-06-30"
  // with Shared Access Signature (SAS) as Device Password
  String username;

  username += broker;
  username += "/";
  username += deviceId;
  username += "/api-version=2024-02-02";

  Serial.print("Username: ");
  Serial.println(username);
  Serial.print("Device PW: ");
  Serial.println(devicePass);

  mqttClient.setUsernamePassword(username, devicePass);

  // Set the 'message callback', this function is
  // called when the MQTTClient receives a message
  // MQTT 클라이언트가 메시지를 받았을 때 실행할 콜백함수 설정
  mqttClient.onMessage(onMessageReceived);
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    initWiFi();
  }
  if (!mqttClient.connected())
  {
    initMQTT();
  }

  // poll for new MQTT Messages and Send keep alives
  // 연결을 활성 상태로 유지하며 loop() 안에서 사용됩니다.
  mqttClient.poll();

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  // Publish a Message Roughly Every 5 seconds; 5초마다 메시지 발행
  if (currentMillis - previousMillis > interval)
  {
    currentMillis = millis();
    // publishMessage();
    publishTemperatureHumidity();
  }
}

float readDHTTemperature()
{
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  // Read temperature as Celsius (the default)
  float t = dht.readTemperature();
  // Read temperature as Fahrenheit (isFahrenheit = true)
  // float t = dht.readTemperature(true);
  // Check if any reads failed and exit early (to try again).
  if (isnan(t))
  {
    Serial.println("Failed to read from DHT sensor!");
    return 0;
  }
  else
  {
    // Serial.println(t);
    return t;
  }
}

float readDHTHumidity()
{
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  float h = dht.readHumidity();
  if (isnan(h))
  {
    Serial.println("Failed to read from DHT sensor!");
    return 0;
  }
  else
  {
    // Serial.println(h);
    return h;
  }
}

// Initialize WiFi
void initWiFi()
{
  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, pass);
  Serial.println("Connecting to WiFi...");

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while (WiFi.status() != WL_CONNECTED)
  {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval)
    {
      WiFi.reconnect();
      Serial.print(".");
    }
  }
  Serial.print("Connected to: ");
  Serial.println(ssid);
  Serial.print("Connected IP: ");
  Serial.print(WiFi.localIP());
  Serial.println(" (Station Mode)");
}

void printLocalTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%Y %b %d %a, %H:%M:%S");
}

void initMQTT()
{
  Serial.print("Attempting to MQTT broker: ");
  Serial.print(broker);
  Serial.println();

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while (!mqttClient.connect(broker, 8883))
  {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval)
    {
      Serial.print("Connecting to MQTT broker - Failed Error Code: ");
      Serial.println(mqttClient.connectError());
    }
  }
  Serial.println();

  Serial.println("You're connected to the MQTT broker");
  Serial.println();

  // subscribe to a topic; 토픽 구독
  mqttClient.subscribe("devices/" + deviceId + "/messages/devicebound/#");
}

// Publishing test
void publishMessage()
{
  Serial.println("Publishing Message");

  // Send Message, the Print interface can be used to set the message contents
  mqttClient.beginMessage("devices/" + deviceId + "/messages/events/");
  mqttClient.print("Hello ");
  mqttClient.print(millis());
  mqttClient.endMessage();
}

void publishTemperatureHumidity()
{
  Serial.print("Publishing message\t");

  // Read Sensor Data
  float temperature = readDHTTemperature();
  float humidity = readDHTHumidity();

  Serial.print(temperature);
  Serial.print("℃ :Temprature <=> Humidity: ");
  Serial.print(humidity);
  Serial.println("%");

  // 오 json 사용?
  DynamicJsonDocument doc(1024);
  doc["messageId"] = messageId++;
  doc["deviceId"] = "ESP32 Client";
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;

  string telemetry;
  serializeJson(doc, telemetry);

  Serial.println(telemetry.c_str());

  // Send Message, the Print interface can be used to set the message contents
  mqttClient.beginMessage("devices/" + deviceId + "/messages/events/");
  mqttClient.print(telemetry.c_str());
  mqttClient.endMessage();
}

void onMessageReceived(int messageSize)
{
  // If we received a Message, Print out the topic and contents
  // '{topic}', * Bytes
  Serial.print("Received a message with topic '");
  Serial.print(mqttClient.messageTopic());
  Serial.print("', ");
  Serial.print(messageSize);
  Serial.print(" Bytes");

  // Use the Stream interface to print the contents
  while (mqttClient.available())
  {
    Serial.print((char)mqttClient.read());
  }
  Serial.println();
  Serial.println();
}

// ESP32로 Epoch/Unix 시간을 얻으려면 getTime() 함수 필요 -> 서버 유효성 인증에 사용됨
// 다른 wifi 라이브러리의 WiFi.getTime() 대체
// Function that gets current epoch time
unsigned long getTime()
{
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    // Serial.println("Failed to obtain time");
    return (0);
  }
  time(&now);
  return now;
}