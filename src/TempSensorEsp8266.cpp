/*
 *  Simple temperature/humidity sensor based on SHT31 with MQTT integration and IotWebConf support.
 */
#include <Arduino.h>
#include <arduino-timer.h>

#define IOTWEBCONF_PASSWORD_LEN 65
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <IotWebConf.h>
#include <IotWebConfTParameter.h>
#include <IotWebConfUsing.h>  // This loads aliases for easier class names.
#include <MQTT.h>
#include <OneButton.h>

// timer to check light status every second only
auto timer = timer_create_default();

// method to be called by timer every n seconds
bool checkTemp(void *argument);

// ### IotWebConf #############################################################
// ############################################################################

// Name server
DNSServer dnsServer;

// Web server
WebServer server(80);

// Is a reset required?
boolean needReset = false;

// IotWebConf: Modifying the config version will probably cause a loss of the existig configuration. Be careful!
const char *CONFIG_VERSION = "1.0.2";

// IotWebConf: Access point SSID
const char *WIFI_AP_SSID = "TempSensor";

// IotWebConf: Default access point password
const char *WIFI_AP_DEFAULT_PASSWORD = "";

// IotWebConf: Method for handling access to / on web config
void handleRoot();

// IotWebConf: Called when connection to wifi was established
void wifiConnected();

// IotWebConf: Called when configuration saved
void configSaved();

// last known WiFi network state
iotwebconf::NetworkState lastNetWorkState = iotwebconf::NetworkState::OffLine;

IotWebConf iotWebConf(WIFI_AP_SSID, &dnsServer, &server, WIFI_AP_DEFAULT_PASSWORD, CONFIG_VERSION);

// Parameter group for settings
IotWebConfParameterGroup groupSettings = IotWebConfParameterGroup("groupSettings", "Settings");

// Parameter for MQTT server
#define STRING_LEN 128
iotwebconf::TextTParameter<STRING_LEN> settingMqttServerIpParam = iotwebconf::Builder<iotwebconf::TextTParameter<STRING_LEN>>("settingMqttServerParam").label("MQTT server IP").defaultValue("192.168.0.1").build();

// Parameter for MQTT server port
iotwebconf::IntTParameter<int16_t> settingMqttServerPortParam =
    iotwebconf::Builder<iotwebconf::IntTParameter<int16_t>>("settingMqttServerPortParam").label("MQTT server port").defaultValue(1883).min(1).max(32767).step(1).placeholder("1..32767").build();

// Parameter for seconds to delay temperature query
iotwebconf::IntTParameter<int16_t> settingDelayParam =
    iotwebconf::Builder<iotwebconf::IntTParameter<int16_t>>("settingDelayParam").label("Delay query seconds").defaultValue(1).min(1).max(9999).step(1).placeholder("1..9999").build();

// Parameter for topic prefix
iotwebconf::TextTParameter<STRING_LEN> settingMqttTopicPrefixParam = iotwebconf::Builder<iotwebconf::TextTParameter<STRING_LEN>>("settingMqttTopicPrefixParam").label("Topic prefix").defaultValue("sensor1").build();

// Sensor
Adafruit_SHT31 sensor;

// Sensor values
float sensorValueTemp = -1;
float sensorValueHumidity = -1;

String sensorValueTempTopicStr;
String sensorValueHumidityTopicStr;

// WiFi Client
WiFiClient wifiClient;

// Mqtt client
MQTTClient mqttClient;

// Mqtt message buffer
#define MSG_BUFFER_SIZE (50)
char mqttMessage[MSG_BUFFER_SIZE];

char mqttMessageBuffer[64];

void connectMqtt();

// ### OneButton ##############################################################
// ############################################################################

// counts of long button presses
int longPressCount = 0;

// OneButton: Handle long press stop
void handleLongPressStop();

OneButton btn = OneButton(
    D3,     // Input pin for the button (we use the flash button)
    true,  // Button is active LOW
    true   // Enable internal pull-up resistor
);

// ##########################################
// General Setup ############################
// ##########################################

void setup() {
    // PIN Initialisierung
    pinMode(D5, OUTPUT);
    pinMode(D8, OUTPUT);

    Serial.begin(115200);

    // -- Initializing the configuration.
    groupSettings.addItem(&settingMqttServerIpParam);
    groupSettings.addItem(&settingMqttServerPortParam);
    groupSettings.addItem(&settingDelayParam);
    groupSettings.addItem(&settingMqttTopicPrefixParam);
    iotWebConf.addParameterGroup(&groupSettings);

    iotWebConf.setWifiConnectionCallback(&wifiConnected);
    iotWebConf.setConfigSavedCallback(&configSaved);
    iotWebConf.setStatusPin(LED_BUILTIN);
    // iotWebConf.setConfigPin(D5);
    iotWebConf.init();

    // -- Set up required URL handlers on the web server.
    server.on("/", handleRoot);
    server.on("/config", []{ iotWebConf.handleConfig(); });
    server.onNotFound([]() { iotWebConf.handleNotFound(); });

    // init sensor i2c
    // set to 0x45 for alternate I2C address, 0x44 is default for SHT31
    sensor.begin(0x44);

    // attach button
    btn.attachLongPressStop(handleLongPressStop);

    // report sensor values every n seconds
    // 60 seconds is recommended for weather monitoring
    long timeQueryIntervalMs = settingDelayParam.value() * 1000;
    Serial.println("Temp query interval ms: " + timeQueryIntervalMs);
    timer.every(timeQueryIntervalMs, checkTemp);
}

// ##########################################
// Main Loop ################################
// ##########################################

void loop() {
    if (needReset) {
        // config changes require reset
        Serial.println("restart in 1 sec");
        delay(1000);
        ESP.restart();
    }

    timer.tick();
    iotWebConf.doLoop();
    btn.tick();

    if (WiFi.isConnected()) {
        mqttClient.loop();

        if (!mqttClient.connected()) {
            connectMqtt();
        }
    }

}

/**
 * Update temp values
 */
bool checkTemp(void *argument) {
    Serial.print("wifi connected: ");
    Serial.println(WiFi.isConnected());
    if (!WiFi.isConnected()) {
        return true;
    }

    sensorValueTemp = sensor.readTemperature();
    sensorValueHumidity = sensor.readHumidity();

    if (isnan(sensorValueHumidity) || isnan(sensorValueTemp)) {
        Serial.println("Failed to read values from sensor!");
        return true;
    }

    Serial.print("temp value: ");
    Serial.println(sensorValueTemp);
    Serial.print("humidity value: ");
    Serial.println(sensorValueHumidity);

    Serial.print("mqtt conntected: ");
    Serial.println(mqttClient.connected());

    mqttClient.publish(sensorValueTempTopicStr, String(sensorValueTemp));
    mqttClient.publish(sensorValueHumidityTopicStr, String(sensorValueHumidity));

    // keep timer running
    return true;
}

void configSaved() {
    Serial.println("config saved");
    needReset = true;
}

void wifiConnected() {
    Serial.println("wifi connected");

    // prepare topic names
    sensorValueTempTopicStr = "/" + String(settingMqttTopicPrefixParam.value()) + "/temp";
    sensorValueHumidityTopicStr = "/" + String(settingMqttTopicPrefixParam.value()) + "/humidity";
}

void connectMqtt() {
    mqttClient.begin(settingMqttServerIpParam.value(), wifiClient);
    mqttClient.connect(iotWebConf.getThingName());
}

/**
 * Handle web requests to "/" path.
 */
void handleRoot() {
    // -- Let IotWebConf test and handle captive portal requests.
    if (iotWebConf.handleCaptivePortal()) {
        // -- Captive portal request were already served.
        return;
    }
    String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
    s += "<title>Temp sensor parameters and values</title></head><body>Current settings and values";
    s += "<ul>";
    s += "<li>Mqtt server ip param value: ";
    s += settingMqttServerIpParam.value();
    s += "<li>Mqtt server port value: ";
    s += settingMqttServerPortParam.value();
    s += "<li>Delay param value: ";
    s += settingDelayParam.value();
    s += "<li>Mqtt topic prefix param value: ";
    s += settingMqttTopicPrefixParam.value();
    s += "<li>Current value temp: ";
    s += sensorValueTemp;
    s += "<li>Current value humiditiy: ";
    s += sensorValueHumidity;
    s += "</ul>";
    s += "Go to <a href='config'>configure page</a> to change values.";
    s += "</body></html>\n";

    server.send(200, "text/html", s);
}


void handleLongPressStop() {
    longPressCount++;

    if (longPressCount == 1) {
        Serial.println("Button pressed to init reset - press again for 5 secs to reset");
    }

    if (longPressCount > 1) {
        Serial.println("Reset config");

        iotWebConf.getSystemParameterGroup()->applyDefaultValue();
        iotWebConf.saveConfig();

        Serial.println("Reset done, rebooting");
        needReset = true;
        delay(2000);
    }

}