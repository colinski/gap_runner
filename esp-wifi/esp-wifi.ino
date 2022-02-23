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
  Before compiling: Use the library manager to install ESP_WiFiManager_Lite. 
  The steps to use are as follows:

  1. Use the arduino library manager to install the library "ESP_WiFiManager_Lite." 
  2. Update esp-wifi.ino line with the IP address of the nano server
  3. Flash the new firmware and open the serial monitor to view status.
  4. On first boot, wifi credentials should not be found. The 8266 will start up 
     in AP mode with an sid of the form ESP_XXXXX and password "qwertyuiop". 
  5. Use a device to connect to this SID and then go to 10.0.0.1 in a browser. 
     The 8266 will display a wifi configuration page. Complete the form with the 
     SID and password for your network. Press save and the credentials will be saved 
     across re-boots and flashes.
  6. The 8266 will restart and connect to the AP you indicated. It will do this on 
     every subsequent boot. When the configured AP is not in range, it should boot 
     into AP mode again to let you reselect a new network.
  7. To get to sync with the gap8 after entering wifi settings, you might need to 
     reboot both devices.
  
  ESP_WiFiManager_Lite (https://github.com/khoih-prog/ESP_WiFiManager_Lite) is a library 
  for the ESP32/ESP8266 boards to enable store Credentials in EEPROM/SPIFFS/LittleFS for easy 
  configuration/reconfiguration and autoconnect/autoreconnect of WiFi and other services without Hardcoding.
  *****************************************************************************************************************************/
  

#include <ESP8266WebServer.h>
#include "SPISlave.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>

#include "defines.h"
#include "Credentials.h"
#include "dynamicParams.h"
#include "utilities.h"

ESP_WiFiManager_Lite* ESP_WiFiManager;
WiFiClient tcpClient;

volatile bool interrupted = false;
static uint32_t lowbit = 5;
static uint32_t highbit = 4;
static uint8_t indexes = 0;

#define BUFFERSIZE (4000)
static uint8_t tcpBuffer[BUFFERSIZE];
static uint8_t packetBuffer[32];
volatile int bufferLength = 0;
volatile int packetLength = 0;

#define PACKET_TO_TRANSMIT 125
volatile uint8_t totalPacketToTransmit = 0;

//Command received from servers/clouds
static uint8_t commands[4];

char clio_server_ip[256];
uint16_t clio_server_port;

void setup() {
    Serial.begin(115200);
    Serial.println("Start");

    pinMode(lowbit, OUTPUT);
    pinMode(highbit, OUTPUT);

    digitalWrite(highbit, LOW);
    digitalWrite(lowbit, LOW);

    //Connect to wireless AP
    //Set values in defines.h
    if(USE_WIFI_MANAGER){
      //Connect to wifi using ESP_WiFiManager_Lite
      //Code for connecting is in utilities.h
      Serial.print("Attempting to connect to AP with wifi manager");
      ESP_WiFiManager = new ESP_WiFiManager_Lite();
      connect_to_wifi_auto(ESP_WiFiManager);
    }
    else{
      Serial.printf("Attempting manual AP connection with SID %s and password %s\n",MANUAL_SSID,MANUAL_PASS);
      connect_to_wifi_manual();
    }
    Serial.printf("\nConnection to WiFi AP successful!");
    
    //Use UDP broadcast advertising protocol to get CLIO server IP and port
    get_clio_server(clio_server_ip, &clio_server_port);

    SPISlave.onData([](uint8_t * data, size_t len) {
        //for (int i = 0; i < len; i++) {
        //  Serial.print(data[i]);
        //  Serial.print(" ");
        //}
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

    if(!tcpClient.connected()){

       //Pause transmitting over spi? 
       digitalWrite(highbit, LOW);
       digitalWrite(lowbit, LOW);
      
      //Find clio server
      get_clio_server(clio_server_ip, &clio_server_port);
      
      //Connect to CLIO server using TCP
      Serial.printf("Connecting to CLIO server %s:%d using TCP\n",clio_server_ip, clio_server_port);
      while(!tcpClient.connect(clio_server_ip, clio_server_port)) {
          Serial.println("TCP Connection failed. Retrying.");
          delay(500);
      }
      Serial.println("Connected to CLIO server!");      
    }
  
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
                //Serial.println("Sent data to server");
            }
            else{
              Serial.println("TCP client not connected");
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
