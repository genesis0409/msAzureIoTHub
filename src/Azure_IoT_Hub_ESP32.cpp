// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*
 * This is an Arduino-based Azure IoT Hub sample for ESPRESSIF ESP32 boards.
 * It uses our Azure Embedded SDK for C to help interact with Azure IoT.
 * For reference, please visit https://github.com/azure/azure-sdk-for-c.
 *
 * To connect and work with Azure IoT Hub you need an MQTT client, connecting, subscribing
 * and publishing to specific topics to use the messaging features of the hub.
 * Our azure-sdk-for-c is an MQTT client support library, helping composing and parsing the
 * MQTT topic names and messages exchanged with the Azure IoT Hub.
 *
 * This sample performs the following tasks:
 * - Synchronize the device clock with a NTP server;
 * - Initialize our "az_iot_hub_client" (struct for data, part of our azure-sdk-for-c);
 * - Initialize the MQTT client (here we use ESPRESSIF's esp_mqtt_client, which also handle the tcp
 * connection and TLS);
 * - Connect the MQTT client (using server-certificate validation, SAS-tokens for client
 * authentication);
 * - Periodically send telemetry data to the Azure IoT Hub.
 *
 * To properly connect to your Azure IoT Hub, please fill the information in the `iot_configs.h`
 * file.
 */

// C99 libraries
#include <cstdlib>
#include <string.h>
#include <time.h>

// Libraries for MQTT client and WiFi connection
#include <WiFi.h>
#include <mqtt_client.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Additional sample headers
#include "AzIoTSasToken.h"
#include "SerialLogger.h"
#include "iot_configs.h"

// Sensors
#include <Adafruit_Sensor.h>
#include <DHT.h>

// Data format
#include <ArduinoJson.h>

// When developing for your own Arduino-based platform,
// please follow the format '(ard;<platform>)'.
#define AZURE_SDK_CLIENT_USER_AGENT "c%2F" AZ_SDK_VERSION_STRING "(ard;esp32)"

// Utility macros and defines
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"
#define MQTT_QOS1 1
#define DO_NOT_RETAIN_MSG 0
#define SAS_TOKEN_DURATION_IN_MINUTES 60
#define UNIX_TIME_NOV_13_2017 1510592825

#define PST_TIME_ZONE 9
#define PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF 0

#define GMT_OFFSET_SECS (PST_TIME_ZONE * 3600)
#define GMT_OFFSET_SECS_DST ((PST_TIME_ZONE + PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * 3600)

// Translate iot_configs.h defines into variables used by the sample
static const char *ssid = IOT_CONFIG_WIFI_SSID;
static const char *password = IOT_CONFIG_WIFI_PASSWORD;
static const char *host = IOT_CONFIG_IOTHUB_FQDN;
static const char *mqtt_broker_uri = "mqtts://" IOT_CONFIG_IOTHUB_FQDN;
static const char *device_id = IOT_CONFIG_DEVICE_ID;
static const int mqtt_port = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;

// Memory allocated for the sample's variables and structures.
static esp_mqtt_client_handle_t mqtt_client;
static az_iot_hub_client client;

static char mqtt_client_id[128];
static char mqtt_username[128];
static char mqtt_password[200];
static uint8_t sas_signature_buffer[256];
static unsigned long next_telemetry_send_time_ms = 0;

// Topic 설정
static char telemetry_topic[128];

static uint32_t telemetry_send_count = 0;
static String telemetry_payload = "{}";

#define INCOMING_DATA_BUFFER_SIZE 128
static char incoming_data[INCOMING_DATA_BUFFER_SIZE];

// Auxiliary functions; 보조 함수
#ifndef IOT_CONFIG_USE_X509_CERT
static AzIoTSasToken sasToken(
    &client,
    AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY),
    AZ_SPAN_FROM_BUFFER(sas_signature_buffer),
    AZ_SPAN_FROM_BUFFER(mqtt_password));
#endif // IOT_CONFIG_USE_X509_CERT

static void connectToWiFi();                                            // WiFi 연결, loop()에서 핸들링
static void initializeTime();                                           // getTime() 인증서 유효성 검사용 - print하는 함수 추가: printLocalTime()
void receivedCallback(char *topic, byte *payload, unsigned int length); // 메시지 수신 콜백
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event);     // MQTT 이벤트 핸들러
static void initializeIoTHubClient();                                   // IoT Hub Client 초기화
static int initializeMqttClient();                                      // MQTT Client 초기화, SAS 토큰 사용하네
static uint32_t getEpochTimeInSecs();
static void establishConnection();      // 각종 연결 수립(WiFi, time, iothub, mqtt)
static void generateTelemetryPayload(); // payload 생성; telemetry_payload
static void sendTelemetry();            // publish Message; telemetry_topic

// DHT Sensor config
// Set your Board ID (ESP32 Sender #1 = BOARD_ID 1, ESP32 Sender #2 = BOARD_ID 2, etc)
#define BOARD_ID 0

// Digital pin connected to the DHT sensor
#define DHTPIN 4

// Uncomment the type of sensor in use:
// #define DHTTYPE    DHT11     // DHT 11
#define DHTTYPE DHT22 // DHT 22 (AM2302)
// #define DHTTYPE    DHT21     // DHT 21 (AM2301)

static DHT dht(DHTPIN, DHTTYPE);
static JsonDocument doc; // Allocate the JSON document
static void printLocalTime();

static float readDHTTemperature();
static float readDHTHumidity();
static void publishTemperatureHumidity();

static void connectToWiFi()
{
  Logger.Info("Connecting to WIFI SSID " + String(ssid));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");

  Logger.Info("WiFi connected, IP address: " + WiFi.localIP().toString());
}

static void initializeTime()
{
  Logger.Info("Setting time using SNTP");

  configTime(GMT_OFFSET_SECS, GMT_OFFSET_SECS_DST, NTP_SERVERS);
  time_t now = time(NULL);
  while (now < UNIX_TIME_NOV_13_2017)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  Logger.Info("Time initialized!");
}

void receivedCallback(char *topic, byte *payload, unsigned int length)
{
  Logger.Info("Received [");
  Logger.Info(topic);
  Logger.Info("]: ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Logger.Info(String(length) + " Bytes");
  Serial.println("");
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
  switch (event->event_id)
  {
    int i, r;

  case MQTT_EVENT_ERROR:
    Logger.Info("MQTT event MQTT_EVENT_ERROR");
    break;
  case MQTT_EVENT_CONNECTED:
    Logger.Info("MQTT event MQTT_EVENT_CONNECTED");

    r = esp_mqtt_client_subscribe(mqtt_client, AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC, 1);
    if (r == -1)
    {
      Logger.Error("Could not subscribe for cloud-to-device messages.");
    }
    else
    {
      Logger.Info("Subscribed for cloud-to-device messages; message id:" + String(r));
    }

    break;
  case MQTT_EVENT_DISCONNECTED:
    Logger.Info("MQTT event MQTT_EVENT_DISCONNECTED");
    break;
  case MQTT_EVENT_SUBSCRIBED:
    Logger.Info("MQTT event MQTT_EVENT_SUBSCRIBED");
    break;
  case MQTT_EVENT_UNSUBSCRIBED:
    Logger.Info("MQTT event MQTT_EVENT_UNSUBSCRIBED");
    break;
  case MQTT_EVENT_PUBLISHED:
    Logger.Info("MQTT event MQTT_EVENT_PUBLISHED");
    break;
  case MQTT_EVENT_DATA:
    Logger.Info("MQTT event MQTT_EVENT_DATA");

    for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->topic_len; i++)
    {
      incoming_data[i] = event->topic[i];
    }
    incoming_data[i] = '\0';
    Logger.Info("Topic: " + String(incoming_data));

    for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->data_len; i++)
    {
      incoming_data[i] = event->data[i];
    }
    incoming_data[i] = '\0';
    Logger.Info("Data: " + String(incoming_data));

    break;
  case MQTT_EVENT_BEFORE_CONNECT:
    Logger.Info("MQTT event MQTT_EVENT_BEFORE_CONNECT");
    break;
  default:
    Logger.Error("MQTT event UNKNOWN");
    break;
  }

  return ESP_OK;
}

static void initializeIoTHubClient()
{
  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);

  if (az_result_failed(az_iot_hub_client_init(
          &client,
          az_span_create((uint8_t *)host, strlen(host)),
          az_span_create((uint8_t *)device_id, strlen(device_id)),
          &options)))
  {
    Logger.Error("Failed initializing Azure IoT Hub client");
    return;
  }

  size_t client_id_length;
  if (az_result_failed(az_iot_hub_client_get_client_id(
          &client, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length)))
  {
    Logger.Error("Failed getting client id");
    return;
  }

  if (az_result_failed(az_iot_hub_client_get_user_name(
          &client, mqtt_username, sizeofarray(mqtt_username), NULL)))
  {
    Logger.Error("Failed to get MQTT clientId, return code");
    return;
  }

  Logger.Info("Client ID: " + String(mqtt_client_id));
  Logger.Info("Username: " + String(mqtt_username));
}

static int initializeMqttClient()
{
#ifndef IOT_CONFIG_USE_X509_CERT
  if (sasToken.Generate(SAS_TOKEN_DURATION_IN_MINUTES) != 0)
  {
    Logger.Error("Failed generating SAS token");
    return 1;
  }
#endif

  esp_mqtt_client_config_t mqtt_config;
  memset(&mqtt_config, 0, sizeof(mqtt_config));
  mqtt_config.uri = mqtt_broker_uri;
  mqtt_config.port = mqtt_port;
  mqtt_config.client_id = mqtt_client_id;
  mqtt_config.username = mqtt_username;

#ifdef IOT_CONFIG_USE_X509_CERT
  Logger.Info("MQTT client using X509 Certificate authentication");
  mqtt_config.client_cert_pem = IOT_CONFIG_DEVICE_CERT;
  mqtt_config.client_key_pem = IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY;
#else // Using SAS key
  mqtt_config.password = (const char *)az_span_ptr(sasToken.Get());
#endif

  mqtt_config.keepalive = 240;
  mqtt_config.disable_clean_session = 0;
  mqtt_config.disable_auto_reconnect = false;
  mqtt_config.event_handle = mqtt_event_handler;
  mqtt_config.user_context = NULL;
  mqtt_config.cert_pem = (const char *)ca_pem;

  mqtt_client = esp_mqtt_client_init(&mqtt_config);

  if (mqtt_client == NULL)
  {
    Logger.Error("Failed creating mqtt client");
    return 1;
  }

  esp_err_t start_result = esp_mqtt_client_start(mqtt_client);

  if (start_result != ESP_OK)
  {
    Logger.Error("Could not start mqtt client; error code:" + start_result);
    return 1;
  }
  else
  {
    Logger.Info("MQTT client started");
    return 0;
  }
}

/*
 * @brief           Gets the number of seconds since UNIX epoch until now.
 * @return uint32_t Number of seconds.
 */
static uint32_t getEpochTimeInSecs() { return (uint32_t)time(NULL); }

static void establishConnection()
{
  connectToWiFi();
  initializeTime();

  printLocalTime();

  initializeIoTHubClient();
  (void)initializeMqttClient();
}

static void generateTelemetryPayload()
{
  // You can generate the JSON using any lib you want. Here we're showing how to do it manually, for simplicity.
  // This sample shows how to generate the payload using a syntax closer to regular delevelopment for Arduino, with
  // String type instead of az_span as it might be done in other samples. Using az_span has the advantage of reusing the
  // same char buffer instead of dynamically allocating memory each time, as it is done by using the String type below.
  // 원하는 lib를 사용하여 JSON을 생성할 수 있습니다.여기서는 단순화를 위해 수동으로 수행하는 방법을 보여줍니다.
  // 이 샘플은 다른 샘플에서 할 수 있는 것처럼 az_span 대신 String type을 사용하여 Arduino에 대한 일반 디벨롭먼트에
  // 가까운 구문을 사용하여 페이로드를 생성하는 방법을 보여줍니다. az_span을 사용하면 아래 String 유형을 사용하여
  // 수행되는 것처럼 매번 동적으로 메모리를 할당하는 대신 동일한 char 버퍼를 재사용할 수 있는 장점이 있습니다.
  // telemetry_payload = "{ \"msgCount\": " + String(telemetry_send_count++) + " }";

  // Read Seonsor Data
  float t = readDHTTemperature();
  float h = readDHTHumidity();

  Serial.printf("Temp: %.1f℃\n", t);
  Serial.printf("Humi: %.1f%%\n", h);

  doc["id"] = BOARD_ID;
  doc["msgCount"] = telemetry_send_count++;
  doc["temperature"] = t;
  doc["humidity"] = h;

  serializeJsonPretty(doc, telemetry_payload);
}

static void sendTelemetry()
{
  Logger.Info("Sending telemetry ...");

  // The topic could be obtained just once during setup,
  // however if properties are used the topic need to be generated again to reflect the
  // current values of the properties.
  // setup()동안 한 번만 주제를 얻을 수 있었습니다,
  // 그러나 properties을 사용하는 경우 속성의 현재 값을 반영하기 위해 항목을 다시 생성해야 합니다.
  // az_iot_hub_client_telemetry_get_publish_topic() 함수로 미리 정해진 topic을 가져오네
  if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
          &client, NULL, telemetry_topic, sizeof(telemetry_topic), NULL)))
  {
    Logger.Error("Failed az_iot_hub_client_telemetry_get_publish_topic");
    return;
  }

  // 이스케이프 문 사용 " 출력을 위한 \"
  // "{ \"msgCount\": " + String(telemetry_send_count++) + " }";
  // { "msgCount": 1557}
  generateTelemetryPayload();

  // Publish 부분, QoS설정 가능, topic 수정은 어떻게 하지?
  if (esp_mqtt_client_publish(
          mqtt_client,
          telemetry_topic,
          (const char *)telemetry_payload.c_str(),
          telemetry_payload.length(),
          MQTT_QOS1,
          DO_NOT_RETAIN_MSG) == 0)
  {
    Logger.Error("Failed publishing");
  }
  else
  {
    Logger.Info("Publish Topic: " + String(telemetry_topic));
    Logger.Info("Message published successfully");
  }
}

// Arduino setup and loop main functions.

void setup()
{
  establishConnection();

  Serial.begin(115200); // Init Serial Monitor
  dht.begin();          // Init dht Sensor

  // 추가
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    connectToWiFi();
  }
#ifndef IOT_CONFIG_USE_X509_CERT
  else if (sasToken.IsExpired())
  {
    Logger.Info("SAS token expired; reconnecting with a new one.");
    (void)esp_mqtt_client_destroy(mqtt_client);
    initializeMqttClient();
  }
#endif
  // 일정 시간마다 보냄
  else if (millis() > next_telemetry_send_time_ms)
  {
    sendTelemetry();
    next_telemetry_send_time_ms = millis() + TELEMETRY_FREQUENCY_MILLISECS;
  }

  // 구현 내용 추가
  // telemetry_topic = "device";
}

// 함수 추가 영역

// 시간 출력 함수
static void printLocalTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Logger.Info("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%Y %b %d %a, %H:%M:%S");
}

// 온도센서
static float readDHTTemperature()
{
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  // Read temperature as Celsius (the default)
  float t = dht.readTemperature();
  // Read temperature as Fahrenheit (isFahrenheit = true)
  // float t = dht.readTemperature(true);
  // Check if any reads failed and exit early (to try again).
  if (isnan(t))
  {
    Logger.Info("Failed to read from DHT sensor!");
    return 0;
  }
  else
  {
    // Serial.println(t);
    return t;
  }
}

// 습도센서
static float readDHTHumidity()
{
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  float h = dht.readHumidity();
  if (isnan(h))
  {
    Logger.Info("Failed to read from DHT sensor!");
    return 0;
  }
  else
  {
    // Serial.println(h);
    return h;
  }
}

static void publishTemperatureHumidity()
{
  // Serial.println("Publishing temp/humi Message");

  // // Read Seonsor Data
  // float t = readDHTTemperature();
  // float h = readDHTHumidity();

  // Serial.printf("Temp: %.1f℃", t);
  // Serial.printf("Humi: %.1f%%", h);

  // // Add values in the JSON document
  // doc["messageId"] = messageId++;
  // doc["deviceId"] = String(deviceId);
  // doc["temperature"] = t;
  // doc["humidity"] = h;

  // std::string telemetry;
  // serializeJson(doc, telemetry);
  // Serial.println(telemetry.c_str());

  // // publish Message
  // client.publish("devices/" + String(deviceId) + "/messages/events/", telemetry.c_str());
  // Serial.println("Message Published.");

  // // test
  // // serializeJsonPretty(doc, telemetry);
  // // Serial.println(telemetry.c_str());
}