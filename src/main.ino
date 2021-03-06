/**
 * @file OTA-mDNS-SPIFFS.ino
 *
 * @author Pascal Gollor (http://www.pgollor.de/cms/)
 * @date 2015-09-18
 *
 * changelog:
 * 2015-10-22:
 * - Use new ArduinoOTA library.
 * - loadConfig function can handle different line endings
 * - remove mDNS studd. ArduinoOTA handle it.
 *
 */

// includes
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <FS.h>
#include <ArduinoOTA.h>
#include <Ticker.h>

#define DEBUG

/**
 * @brief mDNS and OTA Constants
 * @{
 */
#define HOSTNAME "ESP8266-serial-" ///< Hostename. The setup function adds the Chip ID at the end.
/// @}

/**
 * @brief Default WiFi connection information.
 * @{
 */
#define ENABLEPIN 4

const char* ap_default_psk = "esp8266esp8266"; ///< Default PSK.
/// @}

////////////////////////////////
// UDP declarations
////////////////////////////////
#define UDPLOCALPORT 4201
unsigned int localUdpPort = UDPLOCALPORT;  // local port to listen on

char incomingPacket[255];  // buffer for incoming packets

WiFiUDP Udp;

uint16_t firstPort;
IPAddress firstIP;
bool logged = false;

////////////////////////////////
// Serial utitlites
////////////////////////////////

Ticker serialInput;
Ticker pinger;
#define ENDLINE '\n'
#define SERIAL_FRAMERATE 0.1

char inputChar;
char serialBuffer[255];
int bufferIndex = 0;

/**
 * @brief Read WiFi connection information from file system.
 * @param ssid String pointer for storing SSID.
 * @param pass String pointer for storing PSK.
 * @return True or False.
 *
 * The config file have to containt the WiFi SSID in the first line
 * and the WiFi PSK in the second line.
 * Line seperator can be \r\n (CR LF) \r or \n.
 */
bool loadConfig(String *ssid, String *pass)
{
  // open file for reading.
  File configFile = SPIFFS.open("/cl_conf.txt", "r");
  if (!configFile)
  {
    return false;
  }

  // Read content from config file.
  String content = configFile.readString();
  configFile.close();

  content.trim();

  // Check if ther is a second line available.
  int8_t pos = content.indexOf("\r\n");
  uint8_t le = 2;
  // check for linux and mac line ending.
  if (pos == -1)
  {
    le = 1;
    pos = content.indexOf("\n");
    if (pos == -1)
    {
      pos = content.indexOf("\r");
    }
  }

  // If there is no second line: Some information is missing.
  if (pos == -1)
  {

    return false;
  }

  // Store SSID and PSK into string vars.
  *ssid = content.substring(0, pos);
  *pass = content.substring(pos + le);

  ssid->trim();
  pass->trim();

  return true;
} // loadConfig


/**
 * @brief Save WiFi SSID and PSK to configuration file.
 * @param ssid SSID as string pointer.
 * @param pass PSK as string pointer,
 * @return True or False.
 */
bool saveConfig(String *ssid, String *pass)
{
  // Open config file for writing.
  File configFile = SPIFFS.open("/cl_conf.txt", "w");
  if (!configFile)
  {
    return false;
  }

  // Save SSID and PSK.
  configFile.println(*ssid);
  configFile.println(*pass);

  configFile.close();

  return true;
} // saveConfig


/**
 * @brief Arduino setup function.
 */
void setup()
{
  String station_ssid = "";
  String station_psk = "";

  //Activate serial bridge.
  pinMode(ENABLEPIN,OUTPUT);
  digitalWrite(ENABLEPIN,LOW);

  delay(100);


  // Set Hostname.
  String hostname(HOSTNAME);
  hostname += String(ESP.getChipId(), HEX);
  WiFi.hostname(hostname);


  // Initialize file system.
  if (!SPIFFS.begin())
  {
    return;
  }

  // Load wifi connection information.
  if (! loadConfig(&station_ssid, &station_psk))
  {
    station_ssid = "";
    station_psk = "";
  }

  // Check WiFi connection
  // ... check mode
  if (WiFi.getMode() != WIFI_STA)
  {
    WiFi.mode(WIFI_STA);
    delay(10);
  }

  // ... Compare file config with sdk config.
  if (WiFi.SSID() != station_ssid || WiFi.psk() != station_psk)
  {
    // ... Try to connect to WiFi station.
    WiFi.begin(station_ssid.c_str(), station_psk.c_str());
  }
  else
  {
    // ... Begin with sdk config.
    WiFi.begin();
  }

  Serial.println("Wait for WiFi connection.");

  // ... Give ESP 10 seconds to connect to station.
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000)
  {
    delay(500);
  }

  // Check connection
  if(WiFi.status() == WL_CONNECTED)
  {
    // ... print IP Address
  }
  else
  {
    // Go into software AP mode.
    WiFi.mode(WIFI_AP);

    delay(10);

    WiFi.softAP((const char *)hostname.c_str(), ap_default_psk);
  }

  Serial.begin(115200);

  //start UDP server.
  Udp.begin(localUdpPort);



  // Start OTA server.
  ArduinoOTA.setHostname((const char *)hostname.c_str());
  ArduinoOTA.begin();
}

void udploop(){
  //receive incoming UDP packets, and pass them to serial.
  int packetSize = Udp.parsePacket();
  if (packetSize)
  {
    int len = Udp.read(incomingPacket, 255);
    if (len > 0)
    {
      incomingPacket[len] = 0;
    }
    String command = (String) incomingPacket;
    if(command.equals("Login") && !logged){
      login();
    }else{
      if(logged) Serial.println(command);
    }
    #ifdef DEBUG
      sendPacket("Received: " + command + String(Serial.available()));
    #endif
  }
  return;
}

void fetchSerial(){
  //transmit messages from serial.
  while(Serial.available() > 0){
    #ifdef DEBUG
      String deb = "Message detected!";
      sendPacket(deb);
    #endif
    inputChar = Serial.read();
    serialBuffer[bufferIndex] = inputChar;
    bufferIndex++;
    if(inputChar == ENDLINE){
      #ifdef DEBUG
        deb = "Message for you!";
        sendPacket(deb);
      #endif
      sendPacket(serialBuffer, bufferIndex);
      bufferIndex = 0;
      break;
    }
  }
  return;
}

void fetchSerial_Until(){
  //transmit messages from serial.
  if(Serial.available() > 0){
    String response = Serial.readStringUntil(ENDLINE);
    sendPacket(response);
  }
  return;
}

void ping(){
  sendPacket("Ping");
}

void login(){
  firstIP = Udp.remoteIP();
  firstPort = Udp.remotePort();
  pinger.attach(1,ping);
  serialInput.attach(SERIAL_FRAMERATE,fetchSerial_Until);
  logged = true;
  sendPacket("LOGGED");
}

void sendPacket(char * response, int until){
  if(logged){
    char buffer[until + 1];
    strncpy(buffer, response, until);
    Udp.beginPacket(firstIP, firstPort);
    Udp.write(buffer);
    Udp.endPacket();
  }
  return;
}

void sendPacket(String response){
  if(logged){
    int len = response.length() + 1;
    char buffer[len];
    response.toCharArray(buffer,len);
    Udp.beginPacket(firstIP, firstPort);
    Udp.write(buffer);
    Udp.endPacket();
  }
}

/**
 * @brief Arduino loop function.
 */
void loop()
{
  udploop();
  // Handle OTA server.
  ArduinoOTA.handle();
  yield();
}
