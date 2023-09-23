#include <limits.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h> 

#include <OneWire.h>
#include <DallasTemperature.h>
#define ONE_WIRE_BUS 2  // ESP D4
#define TEMPERATURE_PRECISION 11

#include <Wire.h>
#include "SparkFunHTU21D.h"
#include <Adafruit_BMP085.h>

#define numSensors 6
const int   publishInterval = 60; // seconds

const char* projectName     = "ClimateHub";

// WiFi settings
const char ssid[] = "";                  // your network SSID (name)
const char pass[] = "";                      // your network password
const char* mqtt_server = "mqtt.bitlair.nl";

const int BAUD_RATE   = 115200;                       // serial baud rate

// MQTT stuff
char ID[9] = {0};
WiFiClient espClient;
PubSubClient client(espClient);
const char* mqttDebugTopic = "bitlair/debug";
const char* mqttTopic = "bitlair/climate"; // post /{sensorName} {sensorValue}

// Sensor stuff
HTU21D HUD21D_sensor;
Adafruit_BMP085 bmp;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// DS18b20 IDs
DeviceAddress DS18B20_Floor = {0x28, 0xFF, 0x83, 0xB6, 0x60, 0x16, 0x05, 0x89};
DeviceAddress DS18B20_Ceiling = {0x28, 0xFF, 0x9F, 0x26, 0x64, 0x16, 0x04, 0xF5};

int value = 0;

// ============================ setup =============================
void setup()
{
  Serial.begin(BAUD_RATE);
  Serial.println("Bitlair climate sensor hub");

  if (!bmp.begin()) {
    Serial.println("Could not find BMP180 or BMP085 sensor at 0x77!");
  }

  HUD21D_sensor.begin();

  uint32_t chipid = ESP.getChipId();
  snprintf(ID, sizeof(ID), "%x", chipid);

  sensors.begin();

  Serial.print("Locating DS18b20 devices...");
  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" devices.");

  if (sensors.getDeviceCount() != 2) {
    Serial.println("Found an invalid number of sensors!");
  }

  sensors.setResolution(DS18B20_Floor, TEMPERATURE_PRECISION);
  sensors.setResolution(DS18B20_Ceiling, TEMPERATURE_PRECISION);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.begin(BAUD_RATE);
  Serial.println();
  Serial.println(projectName);

  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");

  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(50);
    Serial.print(".");
  }
  Serial.println("");

  Serial.print("WiFi connected to: ");
  Serial.println(ssid);

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}


// =========================  Incomming ===================
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

// ============================ reconect ===================
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(ID)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      char msg[50] = {0};
      snprintf (msg, 75, "%s (re)connect #%ld", projectName, value);
      Serial.print("Publish message: ");
      Serial.println(msg);
      client.publish(mqttDebugTopic, msg);
      ++value;
      // ... and resubscribe
      client.subscribe(mqttTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


// ======================= Handle measurements ===============
void sendData( float T_HTU21D, float T_BMP180, float RH, float P, float T_floor, float T_ceiling) {
      const char *sensorNames[numSensors] = {
        "temperature-HTU21D",
        "temperature-BMP180",
        "RH-HTU21D",
        "BMP-BMP180",
        "temperature-floor",
        "temperature-ceiling"
      };

      float sensorValues[numSensors] = {
        T_HTU21D,
        T_BMP180,
        RH,
        P,
        T_floor,
        T_ceiling
      };

      char newState[50] = {0}; // MQTT topic to push to
      char newStateTopic[50] = {0}; // MQTT topic to push to

      Serial.println("------------------------------------");

      for(int i = 0; i < numSensors; i++) {
        sprintf(newStateTopic, "%s/%s", mqttTopic, sensorNames[i]);

        char temp[9];
        /* 4 is mininum width, 2 is precision; float value is copied onto str_temp*/
        dtostrf(sensorValues[i], 4, 2, temp);
        sprintf(newState, "%s", temp);

        Serial.print(newStateTopic);
        Serial.print(": ");
        Serial.println(newState);

        if (client.publish(newStateTopic, newState, true)) {
          Serial.println("MQTT publish succesful!");
          delay(1000);
        } else {
          Serial.println("MQTT publish unsuccesful! Retrying later.");
          delay(5000);
        }
      }
      client.subscribe(mqttTopic); 
}




void loop()
{
  float humd_HTU21D = HUD21D_sensor.readHumidity();
  float temp_HTU21D = HUD21D_sensor.readTemperature();
  float temp_BMP = bmp.readTemperature();
  float pres_BMP = bmp.readPressure();
  // Calculate altitude assuming 'standard' barometric
  // pressure of 1013.25 millibar = 101325 Pascal
  float alt_BMP = bmp.readAltitude();
  float SeaP_BMP=bmp.readSealevelPressure();

  sensors.requestTemperatures();

  float T_ceiling = sensors.getTempC(DS18B20_Ceiling);
  float T_floor = sensors.getTempC(DS18B20_Floor);


  // float absHum = (6.112*)/(273.15+temp);
  // ==== show all data ======
  Serial.print("Time:");
  Serial.print(millis());
  Serial.print(" Temperature:");
  Serial.print(temp_HTU21D, 1);
  Serial.print("C");
  Serial.print(" Humidity:");
  Serial.print(humd_HTU21D, 1);
  Serial.print("%");
  Serial.print("Temperature = ");
  Serial.print(temp_BMP);
  Serial.println(" °C");

  Serial.print("Pressure = ");
  Serial.print(pres_BMP);
  Serial.println(" Pa");
  Serial.print("Altitude = ");
  Serial.print(alt_BMP);
  Serial.println(" meters");
  Serial.print("Pressure at sea level (calculated) = ");
  Serial.print(SeaP_BMP);
  Serial.println(" Pa");

  // you can get a more precise measurement of altitude
  // if you know the current sea level pressure which will
  // vary with weather and such. If it is 1015 millibars
  // that is equal to 101500 Pascals.
  Serial.print("Real altitude = ");
  Serial.print(bmp.readAltitude(101500));
  Serial.println(" meters");

  Serial.println();

  // ==== wifi === 
  if (!client.connected()) {
    reconnect();
  }

  sendData( temp_HTU21D, temp_BMP, humd_HTU21D, pres_BMP, T_floor, T_ceiling);
  client.loop();
  yield();

  delay(publishInterval * 1000);
}
