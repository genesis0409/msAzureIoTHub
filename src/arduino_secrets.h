#pragma once
#include <string>

using namespace std;

// Fill in your WiFi networks info
#define SECRET_WIFI_SSID "dinfo"
#define SECRET_WIFI_PASS "daon7521"

// Fill in the hostname of your Azure IoT Hub broker
#define SECRET_BROKER "daonHubGroup.azure-devices.net"

#define SECRET_PRIMARY_KEY "n7P/BxFrLnOXsieojHAivfv2FKsgnREwPAIoTOjYgyw="
#define SECRET_CONNECTION_STRING "HostName=daonHubGroup.azure-devices.net;DeviceId=esp32_Seonsor_temp;SharedAccessKey=n7P/BxFrLnOXsieojHAivfv2FKsgnREwPAIoTOjYgyw="

// Fill in the device id
#define SECRET_DEVICE_ID "esp32_Seonsor_temp"

// Fill in the device password: SAS token; 10000hours... === 416days
#define SECRET_DEVICE_PASSWORD "SharedAccessSignature sr = daonHubGroup.azure - devices.net % 2Fdevices % 2Fesp32_Seonsor_temp &sig = io2otXaQAlujBnytNDjHdgih0TlIsjeRBWEfztE3Lok % 3D &se = 1742683722"