
void heartBeatPrint(){
  static int num = 1;

  if (WiFi.status() == WL_CONNECTED)
    Serial.print(F("H"));        // H means connected to WiFi
  else
    Serial.print(F("F"));        // F means not connected to WiFi

  if (num == 80){
    Serial.println();
    num = 1;
  }
  else if (num++ % 10 == 0){
    Serial.print(F(" "));
  }
}

void displayCredentials(){
  Serial.println(F("\nYour stored Credentials :"));

  for (uint16_t i = 0; i < NUM_MENU_ITEMS; i++)
  {
    Serial.print(myMenuItems[i].displayName);
    Serial.print(F(" = "));
    Serial.println(myMenuItems[i].pdata);
  }
}

void displayCredentialsInLoop(){
  static bool displayedCredentials = false;

  if (!displayedCredentials)
  {
    for (int i = 0; i < NUM_MENU_ITEMS; i++)
    {
      if (!strlen(myMenuItems[i].pdata))
      {
        break;
      }

      if ( i == (NUM_MENU_ITEMS - 1) )
      {
        displayedCredentials = true;
        displayCredentials();
      }
    }
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

 
void connect_to_wifi_manual(){
      //Set MANUAL_SSID, MANUAL_PASS values in defines.h
      WiFi.mode(WIFI_STA);      
      WiFi.begin(MANUAL_SSID, MANUAL_PASS); //WIFI SSID and Password
      while (WiFi.status() != WL_CONNECTED){
        delay(500);
        Serial.print(".");
      }
}

void connect_to_wifi_auto(ESP_WiFiManager_Lite *ESP_WiFiManager){

    Serial.print(F("\nStarting ESP_WiFi using ")); Serial.print(FS_Name);
    Serial.print(F(" on ")); Serial.println(ARDUINO_BOARD);
    Serial.println(ESP_WIFI_MANAGER_LITE_VERSION);   

    #if USING_MRD  
      Serial.println(ESP_MULTI_RESET_DETECTOR_VERSION);
    #else
      Serial.println(ESP_DOUBLE_RESET_DETECTOR_VERSION);
    #endif

    String AP_SSID = "ESP_" + String(ESP_getChipId(), HEX);
    String AP_PWD="qwertyuiop";
  
    // Set customized AP SSID and PWD
    ESP_WiFiManager->setConfigPortal(AP_SSID.c_str(), AP_PWD);

    // Cchange default AP IP and use random channe;
    ESP_WiFiManager->setConfigPortalIP(IPAddress(10, 0, 0, 1));
    ESP_WiFiManager->setConfigPortalChannel(0);

    const char NewCustomsStyle[] /*PROGMEM*/ = "<style>div,input{padding:5px;font-size:1em;}input{width:95%;}body{text-align: center;}\
    button{background-color:blue;color:white;line-height:2.4rem;font-size:1.2rem;width:100%;}fieldset{border-radius:0.3rem;margin:0px;}</style>";
    ESP_WiFiManager->setCustomsStyle(NewCustomsStyle);
    ESP_WiFiManager->setCustomsHeadElement("<style>html{filter: invert(10%);}</style>");
    ESP_WiFiManager->setCORSHeader("Your Access-Control-Allow-Origin");

    //Connect to AP
    //Will show wifi manager if can not connect to network
    ESP_WiFiManager->begin(HOST_NAME);  
    Serial.println("Connecting to AP");
    while(WiFi.status() != WL_CONNECTED){
      ESP_WiFiManager->run();
      Serial.print(".");
      Serial.flush();
      delay(500);
    }
    displayCredentialsInLoop();
}

void get_clio_server(char *host, uint16_t *port){

  unsigned int udpPort = 2222; // local port to listen for UDP packets. Must match clio server udp advertise port.
  char udp_in_packet[255]; //array to hold incoming udp data
  char cmd[255];
  bool got_clio_server=false;

  WiFiUDP udp; //udp object

  while(!got_clio_server){

      IPAddress broadcastIp = WiFi.localIP();
      broadcastIp[3] = 255;
  
      udp.begin(udpPort);
      while(udp.parsePacket()==0){
        Serial.print("Sending broadcast for CLIO servers on: ");
        Serial.println(broadcastIp);
        
        udp.beginPacket(broadcastIp, udpPort);
        udp.print("get-clio-server");
        udp.endPacket();
        delay(500);
      }
      int len = udp.read(udp_in_packet, 255);
      udp_in_packet[len]='\0';
      Serial.print("\nPacket received: ");
      Serial.println(udp_in_packet);

      int this_port;
      int n = sscanf(udp_in_packet, "%s %s %d", cmd, host, &this_port);
      *port = (uint16_t) this_port;
  
       if(strcmp(cmd,"set-clio-server")==0){
            Serial.print("Setting CLIO Server Host: "); Serial.println(host);
            Serial.print("Setting CLIO Server Port: "); Serial.println(*port);
            got_clio_server=true;
       }
  }
}
