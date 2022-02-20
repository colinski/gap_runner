  /*
    SPI Slave Demo Sketch
    Connect the SPI Master device to the following pins on the esp8266:

    GPIO    NodeMCU   Name  |   Uno
  ===================================
     15       D8       SS   |   D10
     13       D7      MOSI  |   D11
     12       D6      MISO  |   D12
     14       D5      SCK   |   D13

    Note: If the ESP is booting at a moment when the SPI Master has the Select line HIGH (deselected)
    the ESP8266 WILL FAIL to boot!
    See SPISlave_SafeMaster example for possible workaround

*/

/****************************************************************************************************************************
  ESP_WiFiManager_Lite (https://github.com/khoih-prog/ESP_WiFiManager_Lite) is a library 
  for the ESP32/ESP8266 boards to enable store Credentials in EEPROM/SPIFFS/LittleFS for easy 
  configuration/reconfiguration and autoconnect/autoreconnect of WiFi and other services without Hardcoding.
  *****************************************************************************************************************************/
  
#include "defines.h"
#include "Credentials.h"
#include "dynamicParams.h"
#include <ESP8266WebServer.h>

#include "SPISlave.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>

//CRC8 Global Variables
//#define DI 0x07
//static unsigned char crc8Table[256];

#define ESP_getChipId()   ((uint32_t)ESP.getChipId())

void heartBeatPrint(){
  static int num = 1;

  if (WiFi.status() == WL_CONNECTED)
    Serial.print(F("H"));        // H means connected to WiFi
  else
    Serial.print(F("F"));        // F means not connected to WiFi

  if (num == 80)
  {
    Serial.println();
    num = 1;
  }
  else if (num++ % 10 == 0)
  {
    Serial.print(F(" "));
  }
}

void check_status(){
  static unsigned long checkstatus_timeout = 0;

  //KH
#define HEARTBEAT_INTERVAL    20000L
  // Print hearbeat every HEARTBEAT_INTERVAL (20) seconds.
  if ((millis() > checkstatus_timeout) || (checkstatus_timeout == 0))
  {
    heartBeatPrint();
    checkstatus_timeout = millis() + HEARTBEAT_INTERVAL;
  }
}

ESP_WiFiManager_Lite* ESP_WiFiManager;

#if USING_CUSTOMS_STYLE
const char NewCustomsStyle[] /*PROGMEM*/ = "<style>div,input{padding:5px;font-size:1em;}input{width:95%;}body{text-align: center;}\
button{background-color:blue;color:white;line-height:2.4rem;font-size:1.2rem;width:100%;}fieldset{border-radius:0.3rem;margin:0px;}</style>";
#endif


const uint16_t port = 8584;
const char *host = "192.168.0.155";
//const char *host = "192.168.0.145";

volatile bool interrupted = false;

static uint32_t lowbit = 5;
static uint32_t highbit = 4;

static uint8_t indexes = 0;

#define BUFFERSIZE (4000)
//#define BUFFERSIZE (1500)
static uint8_t tcpBuffer[BUFFERSIZE];
static uint8_t packetBuffer[32];
volatile int bufferLength = 0;
volatile int packetLength = 0;

#define PACKET_TO_TRANSMIT 125
//#define PACKET_TO_TRANSMIT 50
volatile uint8_t totalPacketToTransmit = 0;

//Command received from servers/clouds
static uint8_t commands[4];

WiFiClient tcpClient;

void setup() {
    Serial.begin(115200);
    Serial.println("Start");

    pinMode(lowbit, OUTPUT);
    pinMode(highbit, OUTPUT);

    digitalWrite(highbit, LOW);
    digitalWrite(lowbit, LOW);

  Serial.print(F("\nStarting ESP_WiFi using ")); Serial.print(FS_Name);
  Serial.print(F(" on ")); Serial.println(ARDUINO_BOARD);
  Serial.println(ESP_WIFI_MANAGER_LITE_VERSION);

#if USING_MRD  
  Serial.println(ESP_MULTI_RESET_DETECTOR_VERSION);
#else
  Serial.println(ESP_DOUBLE_RESET_DETECTOR_VERSION);
#endif

  ESP_WiFiManager = new ESP_WiFiManager_Lite();

  String AP_SSID = "ESP_" + String(ESP_getChipId(), HEX);
  String AP_PWD="qwertyuiop";
  
  // Set customized AP SSID and PWD
  ESP_WiFiManager->setConfigPortal(AP_SSID.c_str(), AP_PWD);

  // Optional to change default AP IP(192.168.4.1) and channel(10)
  ESP_WiFiManager->setConfigPortalIP(IPAddress(10, 0, 0, 1));
  ESP_WiFiManager->setConfigPortalChannel(0);

#if USING_CUSTOMS_STYLE
  ESP_WiFiManager->setCustomsStyle(NewCustomsStyle);
#endif

#if USING_CUSTOMS_HEAD_ELEMENT
  ESP_WiFiManager->setCustomsHeadElement("<style>html{filter: invert(10%);}</style>");
#endif

#if USING_CORS_FEATURE  
  ESP_WiFiManager->setCORSHeader("Your Access-Control-Allow-Origin");
#endif

  // Set customized DHCP HostName
  ESP_WiFiManager->begin(HOST_NAME);
  //Or use default Hostname "ESP32-WIFI-XXXXXX"
  //ESP_WiFiManager->begin();


    Serial.println("Connected to WiFi AP successful!");
    
    //Connect to Server Socket
    Serial.println("Connect to TCP Socket");
    while(!tcpClient.connect(host, port)) {
        Serial.println("TCP Connection to host failed");
        delay(500);
    }
    Serial.println("Connected to server successful!");

    SPISlave.onData([](uint8_t * data, size_t len) {
        //for (int i = 0; i < len; i++) {
        //  Serial.print(data[i]);
        //  Serial.print(" ");
        //}
        //Serial.println("");
        digitalWrite(highbit, LOW);
        digitalWrite(lowbit, LOW);
        memcpy((uint8_t *)packetBuffer, data, 32);
        interrupted = true;
        
    });

    SPI1C2 |= 1 << SPIC2MISODM_S;
    SPISlave.setStatus(0xFAFBFCFD);
    SPISlave.begin(4);
}

static unsigned long prev_time = 0;
static unsigned long cur_time = 0;

void loop() {
    if(interrupted) {
        if(indexes == 25) {
            indexes = 0;
        }

        if(packetBuffer[1] < indexes) {
            digitalWrite(highbit, LOW);
            digitalWrite(lowbit, HIGH);
            interrupted = false;
            return;
        } else if(packetBuffer[1] > indexes) {
            digitalWrite(highbit, HIGH);
            digitalWrite(lowbit, LOW);
            interrupted = false;
            return;
        }

        packetLength = (uint32_t)packetBuffer[0];

        memcpy(tcpBuffer + bufferLength, packetBuffer + 2, packetLength);
        bufferLength += packetLength;
        totalPacketToTransmit += 1;

        if(totalPacketToTransmit == PACKET_TO_TRANSMIT) {
            if(tcpClient.connected()) {
                while(tcpClient.availableForWrite() < bufferLength) {
                    delay(0);
                }
                tcpClient.write(tcpBuffer, bufferLength);
            }
            if(tcpClient.connected() && tcpClient.available()) {
                for(int i = 0; i < 4; i++) {
                    commands[i] = tcpClient.read();
                }
                uint32_t commandStatus = commands[0] | (commands[1] << 8) | (commands[2] << 16) | (commands[3] << 24);
                SPISlave.setStatus(commandStatus);
            }
            
            bufferLength = 0;
            totalPacketToTransmit = 0;
        }

        indexes += 1;
        interrupted = false;

        digitalWrite(highbit, LOW);
        digitalWrite(lowbit, HIGH);
    }
}
