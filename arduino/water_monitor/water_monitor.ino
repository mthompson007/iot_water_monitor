/*
   ESP8266-based WiFi Water Chemistry Monitor

   Version:     1.0

   Author:      Michael Thompson

   Purpose:     Create a device that can measure, record and wirelessly transmit water chemistry data to the Amazon Cloud (AWS) and allow
                web-based monitoring and alerting via email and text of data conditions that fall below defined thresholds.
                Industrial sensors for Temperature, water level, pH, and ORP will be placed into the pool water to analyze pool chemistry
                data on a configurable periodic basis.

   Libraries:   ESP8266WiFi.h
                Adafruit_SSD1306.h
                Adafruit_GFX.h
                gfxfont.h
                PubSubClient.h
                SPI.h
                SD.h
                Wire.h
                RTClib.h
                OneWire.h
                DallasTemperature.h
                NTPClient.h



   Credit:      As a newbie to electronics and the Arduino platform, thanks to Patrick's ORP / PH / Temperature Data Logger for ideas on
                materials needed for sampling pool water (http://www.instructables.com/id/ORP-pH-Temperature-Data-Logger/).

   Materials:   (1) Adafruit Feather ESP8266
                (1) Adafruit Adalogger FeatherWing - RTC + SD Add-on For All Feather Boards
                (1) Adafruit FeatherWing OLED - 128x32 OLED Add-on For All Feather Boards
                (1) Adafruit FeatherWing Tripler Mini Kit - Prototyping Add-on For Feathers
                (1) Temperature sensor
                (1) pH sensor
                (1) ORP sensor
                (1) Water level sensor
                (1) Lithium Ion Polymer 3.7v Rechargeable Battery 2500mAh with 2-pin JST-PH connector
                (1)
                (1)

   Data Format: CSV (comma separated) file named as DATE_TIME with following format:
                1) long Measurement Event ID - unique identifier for each recorded event
                2) datetime Date Time - time stamp for each recorded set of measurements
                3) float Temperature (F or C) - temperature in Farhenheight or Celcius
                4) float pH - current pH measurement
                5) float ORP - current ORP measurement
                6) float Remaining Battery Voltage - remaining battery voltage

  License:        Copyright (C) 2017 by Michael Thompson

                This sketch is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License
                as published by the Free Software Foundation.

                This sketch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
                MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details, http://www.gnu.org/licenses/.
*/

#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266WiFi.h>
#include <AmazonIOTClient.h>
#include "ESP8266AWSImplementations.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <RTClib.h>
#include <Time.h>
#include <TimeLib.h>
#include <ArduinoJson.h>


#define ENABLE_AWS                // uncomment to enable AWS connectivity 
#define DEBUG_MODE                // uncomment to receive debug statements in Serial monitor

#define LED_ERROR 0               // LED to indicate error - GPIO 0 
#define LED_STATUS 2              // LED to indicate error - GPIO 2

#define VBATPIN 9                 // Battery PIN
#define ONE_WIRE_BUS 2            // Temperature PIN

#define ERROR_CODE_000 0          // Error: RTC not found
#define ERROR_CODE_001 1          // Error: RTC not running
#define ERROR_CODE_002 2          // Error: SD card not found
#define ERROR_CODE_003 3          // Error: WiFi not present
#define ERROR_CODE_004 4          // Error: Error opening CSV data file
#define ERROR_CODE_005 5          // Error: Error opening Error Log file
#define ERROR_CODE_006 6          // Error: TBD

#define MEASUREMENT_DELAY 3600000000 // 60 min wait time
#define VOLT_REF  4.2             // Reference voltage (3.3v if using USB, 4.2v if using lithium polymer battery source)

// Constants and Macros for getting elapsed time
#define SECS_PER_MIN  (60UL)
#define SECS_PER_HOUR (3600UL)
#define SECS_PER_DAY  (SECS_PER_HOUR * 24L)
#define numberOfSeconds(_time_) (_time_ % SECS_PER_MIN)
#define numberOfMinutes(_time_) ((_time_ / SECS_PER_MIN) % SECS_PER_MIN)
#define numberOfHours(_time_) (( _time_% SECS_PER_DAY) / SECS_PER_HOUR)
#define elapsedDays(_time_) ( _time_ / SECS_PER_DAY)
// Constants for network
char daysOfTheWeek[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* ssid = "Thompson Guest Network";
const char* password = "beourguest!";
const char* awskeyid = "AKIAIWJJ2AOK2MZ2IJQQ";
const char* awssecretkey = "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";
const char* awsregion = "us-east-1";
const char* awsendpoint = "amazonaws.com";
const char* awsdomain = "a352evesiq1nh7.iot.us-east-1.amazonaws.com";
const char* awspath = "/topics/monitor/waterquality";   // path to publish and subscribe to topics
//const char* awspath = "/things/PoolMonitor/shadow";   // path to thing shadow
const char* rtcHost = "time.nist.gov"; // Round-robin DAYTIME protocol

// global program variables
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
Esp8266HttpClient httpClient;
Esp8266DateTimeProvider dateTimeProvider;
AmazonIOTClient iotClient;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);


String getLeadingDigits(byte digits) {
  // utility function: prints leading 0 for decimal
  if (digits < 10)
    return ("0" + String(digits, DEC));
  else
    return (String(digits, DEC));
}

String getLeadingHEX(byte hex) {
  // utility function: prints leading 0 for hex
  if (hex < 16)
    return ("0" + String(hex, HEX));
  else
    return (String(hex, HEX));
}

String getFormattedDateTime(unsigned long ulCurrentTime) {
  // return the current time and day
  DateTime now = ulCurrentTime;

  String strVal = "";
  strVal = String(now.month(), DEC) + "/" + String(now.day(), DEC) + "/" + String(now.year(), DEC) + " ";
  strVal += getLeadingDigits(now.hour()) + ":" + getLeadingDigits(now.minute()) + ":" + getLeadingDigits(now.second());
  return strVal;
}

// log error
void logError(uint8_t codeNum, String errorMsg) {
  //  String strLogEntry = getRTC() + ", Error: " + String(codeNum) + " " + errorMsg;

  String strLogEntry = "Error: " + String(codeNum) + " " + errorMsg;

  // log error to Serial window
  Serial.print(strLogEntry);

  while (1) {
    blinkLEDCode(LED_ERROR, codeNum);
  }
}

// blink out a LED code
void blinkLEDCode(uint8_t ledNum, uint8_t codeNum) {
  for (uint8_t i = 0; i < codeNum; i++) {
    digitalWrite(ledNum, LOW);
    delay(250);
    digitalWrite(ledNum, HIGH);
    delay(250);
  }

  delay(500);
}

String getSerialNumber() {
  // return the MAC address
  byte mac[6];
  WiFi.macAddress(mac);

  String strVal = "";
  strVal = getLeadingHEX(mac[0]) + getLeadingHEX(mac[1]) + getLeadingHEX(mac[2]);
  strVal += getLeadingHEX(mac[3]) + getLeadingHEX(mac[4]) + getLeadingHEX(mac[5]);
  return strVal;
}

String getMACAddress() {
  // return the MAC address
  byte mac[6];
  WiFi.macAddress(mac);

  String strVal = "";
  strVal = getLeadingHEX(mac[0]) + ":" + getLeadingHEX(mac[1]) + ":" + getLeadingHEX(mac[2]) + ":";
  strVal += getLeadingHEX(mac[3]) + ":" + getLeadingHEX(mac[4]) + ":" + getLeadingHEX(mac[5]);
  return strVal;
}

String getIP() {
  return String(WiFi.localIP()[0]) + "." + String(WiFi.localIP()[1]) + "." + String(WiFi.localIP()[2]) + "." + String(WiFi.localIP()[3]);
}

float getBatteryVoltage() {
  float measuredvbat = analogRead(VBATPIN);
  measuredvbat *= 2;    // we divided by 2, so multiply back
  measuredvbat *= 3.3;  // Multiply by 3.3V, our reference voltage
  measuredvbat /= 1024; // convert to voltage
  return measuredvbat;
}

String getLastResetTime() {
  long millisSinceStart = millis() / 1000;

  String strVal = "";
  strVal = String(elapsedDays(millisSinceStart), DEC) + "d:";
  strVal += getLeadingDigits(numberOfHours(millisSinceStart)) + "h:";
  strVal += getLeadingDigits(numberOfMinutes(millisSinceStart)) + "m:";
  strVal += getLeadingDigits(numberOfSeconds(millisSinceStart)) + "s";
  return strVal;
}

void setup() {
  Serial.begin(115200);
  delay(15000);

#if defined(DEBUG_MODE)
  Serial.println("START SETUP");
#endif

  // initialize LEDs
  pinMode(LED_ERROR, OUTPUT);
  digitalWrite(LED_ERROR, HIGH);
  pinMode(LED_STATUS, OUTPUT);
  digitalWrite(LED_STATUS, HIGH);

  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    logError(ERROR_CODE_003, "WiFi not present");
  }

  float fltTemp = 0;                          // water temperature
  float fltPH = 0;                            // pH
  float fltORP = 0;                           // ORP
  String strMACAddress = getMACAddress(); // time since last reset
  String strSerialNumber = getSerialNumber();
  String strBattVoltage = String(getBatteryVoltage()); // remaining battery voltage


  // show debug initialization information
#if defined(DEBUG_MODE)
  // show the MAC address
  Serial.println("MAC: " + strMACAddress);
  // Show available networks
  Serial.println("***** Scan Networks ******");
  byte numSsid = WiFi.scanNetworks();
  // print the list of networks seen:
  Serial.println("SSID List: " + String(numSsid) + " available networks");
  // print the network number and name for each network found:
  for (int thisNet = 0; thisNet < numSsid; thisNet++) {
    Serial.print(thisNet);
    Serial.print(") Network: ");
    Serial.println(WiFi.SSID(thisNet));
  }
  Serial.println("**************************");
  // Show SSID
  Serial.print("Connecting to ");
  Serial.println(ssid);
  // Show WiFi status
  Serial.println("Status: " + String(WiFi.status()));
#endif

  if (WiFi.status() != WL_CONNECTED) {
    // connect to WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
  }

  // connect to WiFi
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
#if defined(DEBUG_MODE)
    Serial.println("Status: " + String(WiFi.status()));
#endif
    blinkLEDCode(LED_STATUS, 1);
  }

  // get current time from time.nist.gov
  String strCurrentTime = "";             // current time
  timeClient.begin();
  timeClient.update();
  strCurrentTime = getFormattedDateTime(timeClient.getEpochTime());

#if defined(DEBUG_MODE)
  // show the current time and day
  Serial.println("Requested Time from time.nist.gov");
#endif

#if defined(ENABLE_AWS)
  // connect to AWS
  iotClient.setAWSRegion(awsregion);
  iotClient.setAWSEndpoint(awsendpoint);
  iotClient.setAWSDomain(awsdomain);
  iotClient.setAWSPath(awspath);
  iotClient.setAWSKeyID(awskeyid);
  iotClient.setAWSSecretKey(awssecretkey);
  iotClient.setHttpClient(&httpClient);
  iotClient.setDateTimeProvider(&dateTimeProvider);
#endif

#if defined(DEBUG_MODE)
  // connected to WiFi
  Serial.println("Connected to WiFi");
  Serial.println();
#endif

  // Get Water Temperature
  sensors.requestTemperatures();
  fltTemp = sensors.getTempFByIndex(0);
  // Get pH
  float fltPH = 7.0;                            // pH
  // Get ORP
  float fltORP = 450.0;                           // ORP

#if defined(DEBUG_MODE)
  Serial.println();
  Serial.println("PRINT DETAILS");

  // Show IP
  Serial.println("IP: " + getIP());
  // Show connection strength to router
  Serial.println("RSSI: " + String(WiFi.RSSI()));
  // Show WiFi status
  Serial.println("WiFi Status: " + String(WiFi.status()));
  // Show battery voltage
  Serial.println("Battery Voltage: " + strBattVoltage);
  // Show current time
  Serial.println("Current Time: " + strCurrentTime);
  // Show WiFi diagnostics
  WiFi.printDiag(Serial);
  // Show Water Temperature
  Serial.println("Water Temp (F): " + String(fltTemp));   // 0 refers to the first IC on the wire
#endif

#if defined(ENABLE_AWS)

#if defined(DEBUG_MODE)
  Serial.println("BEFORE AWS CALL");
#endif

  // transmit sensor values to AWS
  ActionError actionError;
  MinimalString shadow = ("{\"serialNumber\": \"" + strSerialNumber + "\", \"temp\": " + String(fltTemp) + ", \"ph\": " + String(fltPH) + ", \"orp\": " + String(fltORP) + ", \"batteryVoltage\": " + strBattVoltage + ", \"eventDateTime\": \"" + strCurrentTime + "\"}").c_str();
  //MinimalString shadow = ("{\"state\":{\"reported\": {\"temp\": \"" + String(fltTemp) + "\"}}}").c_str();

#if defined(DEBUG_MODE)
  Serial.println("CALL AWS");
#endif

  char* result = iotClient.update_shadow(shadow, actionError);

#if defined(DEBUG_MODE)
  Serial.println("AFTER AWS CALL");
#endif
#endif

#if defined(DEBUG_MODE)
  Serial.println(result);

  Serial.println("END SETUP - GO TO SLEEP");
#endif

  // go to sleep to save power - make sure to connect RST to 16
  //  ESP.deepSleep(MEASUREMENT_DELAY);
#if defined(DEBUG_MODE)
  delay(MEASUREMENT_DELAY);
#endif
}

void loop() {
}
