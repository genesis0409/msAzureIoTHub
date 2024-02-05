// ESP32 - (MQTT) - Azure IoT Hub
// https://www.youtube.com/watch?v=aFSYOBvCTmw
#include <stdlib.h>

#include "Arduino.h"
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <ArduinoJson.h>

#include "time.h"
#include <WiFi.h>

#include <EspMQTTClient.h>

#include "arduino_secrets.h"

////////// Enter your sensitive data in arduino_secrets.h
const char *ssid = SECRET_WIFI_SSID;
const char *pass = SECRET_WIFI_PASS;
const char *broker = SECRET_BROKER;
const short mqttServerPort = 1883;
const char *deviceId = SECRET_DEVICE_ID;
const char *devicePass = SECRET_DEVICE_PASSWORD;

const char *username = SECRET_USERNAME;

const char *devicePriKey = SECRET_PRIMARY_KEY;
const char *connectionString = SECRET_CONNECTION_STRING;

const char *ntpServer = "pool.ntp.org"; // Cluster of timeservers; Anyone can use to request the time.
const long gmtOffset_sec = 3600 * 9;    // GMT offset; seoul: +09:00 == 3600*9
const int daylightOffset_sec = 0;       // Summertime; Not Used.

// Set your Board ID (ESP32 Sender #1 = BOARD_ID 1, ESP32 Sender #2 = BOARD_ID 2, etc)
#define BOARD_ID 1

// Digital pin connected to the DHT sensor
#define DHTPIN 4

// Uncomment the type of sensor in use:
// #define DHTTYPE    DHT11     // DHT 11
#define DHTTYPE DHT22 // DHT 22 (AM2302)
// #define DHTTYPE    DHT21     // DHT 21 (AM2301)

DHT dht(DHTPIN, DHTTYPE);
JsonDocument doc; // Allocate the JSON document

unsigned int messageId = 1; // 전송된 판독값 수를 추적

unsigned long previousMillis = 0; // Stores last time temperature was published
const long interval = 5000;       // Interval at which to publish sensor readings 86400/12=7200 + wifi reconnect interval

float readDHTTemperature();
float readDHTHumidity();

void printLocalTime();

void onMessageReceived(int messageSize);
void publishTemperatureHumidity();

unsigned long getTime();

// Enable when you want to see the actual values published over MQTT after each measurement
// #define DEBUG_KALMAN

EspMQTTClient client(
    ssid,
    pass,
    broker,
    deviceId,
    devicePriKey,
    username,
    mqttServerPort);

const bool MQTT_RETAINED = true;

void setup()
{
  Serial.begin(115200); // Init Serial Monitor
  dht.begin();          // Init dht Sensor

  if (WiFi.begin(ssid, pass))
  {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    printLocalTime();
    Serial.println();

    WiFi.disconnect(WIFI_OFF);
  }
  // Init and get the time

  Serial.print("MQTT Client Name: ");
  Serial.println(deviceId);
  Serial.print("MQTT retained: ");
  Serial.println(MQTT_RETAINED ? "true" : "false");

  // Optional functionalities of EspMQTTClient
  client.enableDebuggingMessages(); // Enable debugging messages sent to serial output
  // client.enableHTTPWebUpdater();    // Enable the web updater. User and password default to values of MQTTUsername and MQTTPassword. These can be overrided with enableHTTPWebUpdater("user", "password").
  // client.enableOTA();               // Enable OTA (Over The Air) updates. Password defaults to MQTTPassword. Port is the default OTA port. Can be overridden with enableOTA("password", port).
  // client.enableDrasticResetOnConnectionFailures();
  client.enableLastWillMessage("TestClient/lastwill", "I am going offline", MQTT_RETAINED); // You can activate the retain flag by setting the third parameter to true
}

void loop()
{
  client.loop(); // wifi & mqtt 둘다 핸들링 해줌

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  // Publish a Message Roughly Every 5 seconds; 5초마다 메시지 발행
  if (currentMillis - previousMillis > interval)
  {
    currentMillis = millis();
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

// mqtt (or wifi) 연결 수립 시 setter function callback
// 이건 콜백함수라는데 - 내부 람다표현식
void onConnectionEstablished()
{
  // Subscribe to "mytopic/test" and display received message to Serial
  client.subscribe("devices/" + String(deviceId) + "/messages/devicebound/#", [](const String &payload) { // If we received a Message, Print out the topic and contents
    // '{topic}', * Bytes
    Serial.print("Received a message with topic '");
    Serial.print("devices/" + String(deviceId) + "/messages/devicebound/#");
    Serial.print("', ");
    Serial.print(payload.length());
    Serial.print(" Bytes");

    Serial.print("Message: ");
    Serial.println(payload);
  });

  // Subscribe to "mytopic/wildcardtest/#" and display received message to Serial
  // client.subscribe("mytopic/wildcardtest/#", [](const String &topic, const String &payload)
  //                  { Serial.println("(From wildcard) topic: " + topic + ", payload: " + payload); });

  // Send Message, the Print interface can be used to set the message contents
  client.publish("devices/" + String(deviceId) + "/messages/events/", "Hello MQTT Starts "); // You can activate the retain flag by setting the third parameter to true

  // Execute delayed instructions
  // client.executeDelayed(5 * 1000, []()
  //                       { client.publish("mytopic/wildcardtest/test123", "This is a message sent 5 seconds later"); });
}

// 특정 topic 구독 - 원 함수
/*
void onTestMessageReceived(const String &message)
{
  Serial.print("message received from test/mytopic: " + message);
}

client.subscribe("test/mytopic", onTestMessageReceived);
*/

void publishTemperatureHumidity()
{
  Serial.println("Publishing temp/humi Message");

  // Read Seonsor Data
  float t = readDHTTemperature();
  float h = readDHTHumidity();

  Serial.printf("Temp: %.1f℃", t);
  Serial.printf("Humi: %.1f%%", h);

  // Add values in the JSON document
  doc["messageId"] = messageId++;
  doc["deviceId"] = String(deviceId);
  doc["temperature"] = t;
  doc["humidity"] = h;

  string telemetry;
  serializeJson(doc, telemetry);
  Serial.println(telemetry.c_str());

  // publish Message
  client.publish("devices/" + String(deviceId) + "/messages/events/", telemetry.c_str());
  Serial.println("Message Published.");

  // test
  // serializeJsonPretty(doc, telemetry);
  // Serial.println(telemetry.c_str());
}