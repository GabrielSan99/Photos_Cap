#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "Base64.h"
#include <SPIFFS.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "driver/rtc_io.h"
#include <ESPAsyncWebServer.h>
#include <StringArray.h>
#include <SPIFFS.h>
#include <FS.h>
#include "esp_camera.h"


#define FILE_PHOTO "/photo.jpg"

#define TRIGGER_PIN 13
#define DEBUG_LED 14
#define FLASH_LED 4

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

bool wm_nonblocking = false; // change to true to use non blocking
hw_timer_t *timer = NULL;

struct tm timeinfo;
camera_config_t config;
WiFiManager wm; // global wm instance
WiFiManagerParameter custom_photo_field, custom_ssid_field, custom_pass_field, custom_key_field, custom_flash_field; // global param ( for non blocking w params )

int waitingTime = 30000; //Wait 30 seconds to google response.
const char* myDomain = "script.google.com";
String myFilename = "filename=ESP32-CAM.jpg";
String mimeType = "&mimetype=image/jpeg";
String myImage = "&data=";


String configFlag = "1";    // 1 == turn on wifimanager; 2-5 == trying to connect wifi
String flashFlag = "0";     // 1 == True and 0 == False
 
String photosCap = "1";
String apSSID = "Quickium";
String apPassword = "quickium123";
String setupKey;


char c_flag[4];
char f_flag[4];

char photo[4];
char ssid[30];
char pass[30];
char key [250];

//<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap@4.3.1/dist/css/bootstrap.min.css" integrity="sha384-ggOyR0iXCbMQv3Xipma34MD+dH/1fQ784/j6cY/iJTQUOhcWr7x9JvoRxT2MZw1T" crossorigin="anonymous">
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -3600*3;
const int daylightOffset_sec = 0;

unsigned long interval, adjustInterval, lastPhoto, firstPhoto, blinkTime, _time;

bool photoFlag = true;
bool takeNewPhoto = false;
bool _blink = true;
bool startTest = false;

AsyncWebServer server(80);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta http-equiv="Content-Type" content="text/html; charset=utf-8" /> 
  <style>

    *{margin-left:0px; margin-right: 0px;}

     body{text-align: center; background-color:#e6e6e6}

    .header{height: 15%; background-color: #3e5a98; display: flex; justify-content: center; align-items: center; margin-top:-2%}

    .button{border-radius: 4px; background-color: #3e5a98; color:white; font-size: 15px; font-weight: bolder}
    
    .photo{margin-left: 10%; width:80%; position: relative; top:25%}
    
  </style>
</head>
<body>
  <div class="header">
    <h1 style="font-size:40px; color: white; font: bolder" >QUICKIUM CAM</h1>
  </div><br>
  <div>
    <p style="font-weight:bolder" >Wait 5 seconds to click refresh button!</p>
    <p>
      <button class="button" onclick="capturePhoto()">CAPTURE PHOTO</button>
      <button class="button" onclick="location.reload();">REFRESH PAGE</button>
    </p>
  </div>
  <br>
  <div class="photo">
    <img src="saved-photo" id="photo" width="100%">
  </div>
</body>

<script>
  var deg = 0;
  function capturePhoto() {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', "/capture", true);
    xhr.send();
  }
</script>
</html>)rawliteral";

void IRAM_ATTR resetModule(){
    ets_printf("(watchdog) reiniciar\n"); //imprime no log
    ESP.restart(); //reinicia o chip
}

void setup() {
  
  timer = timerBegin(0, 160, true);                   //160MHz de clock
  timerAttachInterrupt(timer, &resetModule, true);    //função de callback do alarme
  timerAlarmWrite(timer, 60000000, true);             //60 segundos de timer
  timerAlarmEnable(timer);                            //habilita a interrupção
   
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  pinMode(TRIGGER_PIN, INPUT);
  pinMode(DEBUG_LED, OUTPUT);
  pinMode(FLASH_LED, OUTPUT);
  
  digitalWrite(DEBUG_LED, LOW);
  digitalWrite(FLASH_LED, LOW);
  

  
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP  
  Serial.begin(115200);
  Serial.setDebugOutput(true);  
  delay(3000);
  Serial.println("\n Starting");
  
  if(!SPIFFS.begin(true)){
     Serial.println("SPIFFS Mount Failed");
     return;
  }

  checkButton();

  //SPIFFS.format();    //just used to development (clean spiffs memory)
  
  Serial.println("Mounted SPIFFS file system");

  if (SPIFFS.exists("/config_flag.txt")) {
    configFlag = readFile(SPIFFS, "/config_flag.txt");
    Serial.println("Config Flag is set as: " + configFlag); 
  }
  else{
    configFlag.toCharArray(c_flag,4);
    writeFile(SPIFFS, "/config_flag.txt", c_flag);
    Serial.println("Config Flag is set as: " + String(c_flag)); 
  }

  if (SPIFFS.exists("/flash_flag.txt")) {
    flashFlag = readFile(SPIFFS, "/flash_flag.txt");
    Serial.println("Flash Flag is set as: " + flashFlag); 
  }
  else{
    flashFlag.toCharArray(f_flag,4);
    writeFile(SPIFFS, "/flash_flag.txt", f_flag);
    Serial.println("Flash Flag is set as: " + String(f_flag)); 
  }
  
  if (SPIFFS.exists("/photos.txt")) {
    photosCap = readFile(SPIFFS, "/photos.txt");
    photosCap.toCharArray(photo, 4);
    Serial.println("Custom number photos: " + String(photosCap));
  }
  else{
    Serial.println("Default number photos: " + photosCap);
  }
  
  if (SPIFFS.exists("/AP_SSID.txt")) {
    apSSID = readFile(SPIFFS, "/AP_SSID.txt");
    apSSID.toCharArray(ssid, 30);
    Serial.println("Custom AP SSID: " + apSSID);
  }
  else{
    apSSID.toCharArray(ssid, 30);
    Serial.println("Default AP SSID: " + apSSID);
  }

  if (SPIFFS.exists("/AP_password.txt")) {
    apPassword = readFile(SPIFFS, "/AP_password.txt");
    apPassword.toCharArray(pass, 30);
    Serial.println("Custom AP password: " + apPassword);
  }
  else{
    apPassword.toCharArray(pass, 30);
    Serial.println("Default AP password: " + apPassword);
  }

  if (SPIFFS.exists("/setup_key.txt")) {
    setupKey = readFile(SPIFFS, "/setup_key.txt");
    Serial.println("ok");
    setupKey.toCharArray(key, 250);
    Serial.println("Setup Key: " + setupKey);
  }
  else{
    Serial.println("Not yet configured!");
  }
  
  delay(1000);
  
  //wm.resetSettings(); // wipe settings

  if(wm_nonblocking) wm.setConfigPortalBlocking(false);

  String html_photo = "<h1 style='text-align:center'>SETUP PAGE</h1><br><label for='photo'>Number photos to capture </label><input type='number' min='1' max='24' name='photo' value='" + photosCap + "'>";
  const char* custom_photo_cap = html_photo.c_str();
  new (&custom_photo_field) WiFiManagerParameter(custom_photo_cap); // custom html input

  String html_ssid = "<label for='ssid'>AP SSID</label><input type='text' name='ssid' value='" + apSSID + "'>";
  const char* custom_ap_ssid = html_ssid.c_str();
  new (&custom_ssid_field) WiFiManagerParameter(custom_ap_ssid); // custom html input

  String html_pass = "<label for='pass'>AP password</label><input type='password' name='pass' value='" + apPassword + "'><hr>";
  const char* custom_ap_pass = html_pass.c_str();
  new (&custom_pass_field) WiFiManagerParameter(custom_ap_pass); // custom html input

  String html_key ="<label for='key'>Setup Key</label><input type='text' name='key' value='" + setupKey + "'>";
  const char* custom_key = html_key.c_str();
  new (&custom_key_field) WiFiManagerParameter(custom_key); // custom html input

  String html_flash ="<p>Choose flash state to your photos:</p><div style='display: flex; flex-direction:row; flex-wrap: wrap;width: 100%'><div style='display: flex;flex-direction: column;flex-basis: 100%; flex: 1; text-align: center'><input type='radio' id='on' name='flash' value='1'>ON</div><div style='display: flex;flex-direction: column;flex-basis: 100%; flex: 1; text-align: center'><input type='radio' id='off' name='flash' value='0'>OFF</div></div>";
  const char* custom_flash = html_flash.c_str();
  new (&custom_flash_field) WiFiManagerParameter(custom_flash); // custom html input

  
  wm.addParameter(&custom_photo_field);
  wm.addParameter(&custom_ssid_field);
  wm.addParameter(&custom_pass_field);
  wm.addParameter(&custom_key_field);
  //wm.addParameter(&custom_flash_field);
  
  wm.setSaveParamsCallback(saveParamCallback);

  std::vector<const char *> menu = {"wifi","info","param","sep","restart","exit"};
  wm.setMenu(menu);
  wm.setClass("invert");               // set dark theme

  timerAlarmDisable(timer);
  
  if (configFlag == "1"){
    Serial.println("Trying to up config page!!");
    digitalWrite(DEBUG_LED, HIGH);
    
    wm.setConfigPortalTimeout(120);     // auto close configportal after n seconds
    wm.startConfigPortal(ssid,pass);    // password protected ap
    digitalWrite(DEBUG_LED, LOW);
    delay(100);
    configFlag = "2";
    configFlag.toCharArray(c_flag,4);
    writeFile(SPIFFS, "/config_flag.txt", c_flag);
    delay(100); 
    ESP.restart();
  }
  
  else {

    Serial.println("Trying to connect wifi!!");
    wm.setConfigPortalTimeout(5);     // auto close configportal after n seconds
    bool res;
    res = wm.autoConnect(ssid,pass);   // password protected ap
  
    if(!res) {
      Serial.println("Failed to connect or hit timeout");
      int aux = configFlag.toInt();
      aux = aux + 1;
      if (aux > 5){
          configFlag = "1";
          configFlag.toCharArray(c_flag,4);
          writeFile(SPIFFS, "/config_flag.txt", c_flag); 
          delay(100); 
          ESP.restart();
      }
      else{
        configFlag = String(aux);
        configFlag.toCharArray(c_flag,4);
        writeFile(SPIFFS, "/config_flag.txt", c_flag);
        delay(100); 
        ESP.restart();
      }
    } 
    else {
      //if you get here you have connected to the WiFi    
      Serial.println("connected...yeey :)");
    }

  }

  configFlag = "2";
  configFlag.toCharArray(c_flag,4);
  writeFile(SPIFFS, "/config_flag.txt", c_flag); 
  
  delay(2000);
  digitalWrite(DEBUG_LED, LOW);
  timerAlarmEnable(timer);
  Serial.println("Start WatchDog");

  //////////////////////////////////////////////////Setup time to photos capture////////////////////////////////////////////////////////////
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  time_t now;
  struct tm currentTime;
  if(!getLocalTime(&currentTime)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&currentTime, "%A, %B %d %Y %H:%M:%S");
  time(&now);
  unsigned long currentEpoch = (unsigned long) now;
  
  int hourInterval = (24/photosCap.toInt());
  interval = hourInterval * 3600*1000;

  int aux = 0;
  while (currentTime.tm_hour >= hourInterval*aux){
    aux = aux + 1;
    Serial.println(aux);
    delay(20);
  }
  Serial.print("Hour to first photo capture: ");
  Serial.println(hourInterval*aux);

  unsigned long nextEpoch = get_epoch((currentTime.tm_year+1900),(currentTime.tm_mon+1), currentTime.tm_mday, hourInterval*aux, 0, 0);   //manual 6 offset hours from current time 

  adjustInterval = (nextEpoch - currentEpoch) * 1000;
  Serial.println("Adjust interval to first photo: " + String(adjustInterval));

  firstPhoto = millis();

  delay(1000);
  
  /////////////////////////////////////////////////////////Setup Camera//////////////////////////////////////////////////////////////
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;  // UXGA|SXGA|XGA|SVGA|VGA|CIF|QVGA|HQVGA|QQVGA
  config.jpeg_quality = 10;
  config.fb_count = 1;
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }

  ////////////////////////////////////////////////////////Setup WebServer///////////////////////////////////////////////////////
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", index_html);
  });

  server.on("/capture", HTTP_GET, [](AsyncWebServerRequest * request) {
    takeNewPhoto = true;
    request->send_P(200, "text/plain", "Taking Photo");
  });

  server.on("/saved-photo", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, FILE_PHOTO, "image/jpg", false);
  });

  // Start server
  server.begin();

  blinkTime = millis();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////FUNCTIONS///////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void checkButton(){
  // check for button press
  if ( digitalRead(TRIGGER_PIN) == LOW ) {
    delay(500);
    if( digitalRead(TRIGGER_PIN) == LOW ){
      startTest = true;
      delay(4500);
      if( digitalRead(TRIGGER_PIN) == LOW ){
        Serial.println("Start Config Page");
        configFlag = "1";
        configFlag.toCharArray(c_flag,4);
        writeFile(SPIFFS, "/config_flag.txt", c_flag);
        delay(100);       
        ESP.restart();        
      }
    }
  }
}


String getParam(String name){
  //read parameter from server, for customhmtl input
  String value;
  if(wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

void saveParamCallback(){
  Serial.println("[CALLBACK] saveParamCallback fired");

  photosCap = getParam("photo");
  apSSID = getParam("ssid");
  apPassword = getParam("pass");
  setupKey = getParam("key");
  flashFlag = getParam("flash");

  Serial.println("Parameters fetched: ");
  Serial.println(photosCap);
  Serial.println(apSSID);
  Serial.println(apPassword);
  Serial.println(setupKey);

  
  Serial.println("Updating setup configs...");


  if (photosCap != ""){
    photosCap.toCharArray(photo,4);
    writeFile(SPIFFS, "/photos.txt", photo);
    Serial.println("Saved number photos to capture: " + photosCap); 
  }
  
   if (apSSID != ""){
    apSSID.toCharArray(ssid,30);
    writeFile(SPIFFS, "/AP_SSID.txt", ssid);
    Serial.println("Saved new AP SSID: " + apSSID); 
  }
  
  if (apPassword != ""){
    apPassword.toCharArray(pass,30);
    writeFile(SPIFFS, "/AP_password.txt", pass);
    Serial.println("Saved new AP password: " + apPassword); 
  }

  if (setupKey != ""){
    setupKey.toCharArray(key,250);
    writeFile(SPIFFS, "/setup_key.txt", key);
    Serial.println("Saved new setup key: " + setupKey); 
  }

  if (flashFlag != ""){
    flashFlag.toCharArray(f_flag,4);
    writeFile(SPIFFS, "/flash_flag.txt", f_flag);
    Serial.println("Saved flash state: " + flashFlag);  
  }
  
}

String readFile(fs::FS &fs, const char * path){
    Serial.printf("Reading file: %s\n", path);

    File file = fs.open(path);
    if(!file || file.isDirectory()){
        Serial.println("Failed to open file for reading");
        return "";
    }

    Serial.print("Read from file: ");
    char msg[100];
    memset(msg,0,sizeof(msg));
    uint8_t i = 0;
    while(file.available()){
        msg[i] = file.read(); 
        i++;
    }
    
    String result = String(msg);
    return result;
}

void writeFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Writing file: %s\n", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("Failed to open file for writing");
        return;
    }
    if(file.print(message)){
        Serial.println("File written");
    } else {
        Serial.println("Write failed");
    }
}

//https://sourceware.org/newlib/libc.html#gmtime
unsigned long get_epoch(int _year, int _month, int _day, int _hour, int _min, int _sec){

  struct tm t;
  
  t.tm_year = _year-1900;
  t.tm_mon = _month-1;           // Month, 0 - jan
  t.tm_mday = _day;              // Day of the month
  t.tm_hour = _hour;      
  t.tm_min = _min;
  t.tm_sec = _sec;
  t.tm_isdst = -1;

  return  mktime(&t);
}

void saveCapturedImage() {
  Serial.println("Connect to " + String(myDomain));
  WiFiClientSecure client;
  
  if (flashFlag == "1"){
    digitalWrite(FLASH_LED, HIGH);
  }
  if (client.connect(myDomain, 443)) {
    Serial.println("Connection successful");
    
    camera_fb_t * fb = NULL;
    fb = esp_camera_fb_get();  
    if(!fb) {
      Serial.println("Camera capture failed");
      delay(1000);
      ESP.restart();
      return;
    }
  
    char *input = (char *)fb->buf;
    char output[base64_enc_len(3)];
    String imageFile = "";
    for (int i=0;i<fb->len;i++) {
      base64_encode(output, (input++), 3);
      if (i%3==0) imageFile += urlencode(String(output));
    }
    String Data = myFilename+mimeType+myImage;
    
    esp_camera_fb_return(fb);
    
    Serial.println("Send a captured image to Google Drive.");
    //client.println("POST " + myScript + " HTTP/1.1");
    client.println("POST /macros/s/" + setupKey + "/exec HTTP/1.1");
    client.println("Host: " + String(myDomain));
    client.println("Content-Length: " + String(Data.length()+imageFile.length()));
    client.println("Content-Type: application/x-www-form-urlencoded");
    client.println();
    
    client.print(Data);
    int Index;
    for (Index = 0; Index < imageFile.length(); Index = Index+1000) {
      client.print(imageFile.substring(Index, Index+1000));
    }
    
    Serial.println("Waiting for response.");
    long int StartTime=millis();
    while (!client.available()) {
      Serial.print(".");
      delay(100);
      if ((StartTime+waitingTime) < millis()) {
        Serial.println();
        Serial.println("No response.");
        //If you have no response, maybe need a greater value of waitingTime
        break;
      }
    }
    Serial.println();   
    while (client.available()) {
      Serial.print(char(client.read()));
    }  
  } else {         
    Serial.println("Connected to " + String(myDomain) + " failed.");
  }
  client.stop();
  digitalWrite(FLASH_LED, LOW);
}


//https://github.com/zenmanenergy/ESP8266-Arduino-Examples/
String urlencode(String str)
{
    String encodedString="";
    char c;
    char code0;
    char code1;
    char code2;
    for (int i =0; i < str.length(); i++){
      c=str.charAt(i);
      if (c == ' '){
        encodedString+= '+';
      } else if (isalnum(c)){
        encodedString+=c;
      } else{
        code1=(c & 0xf)+'0';
        if ((c & 0xf) >9){
            code1=(c & 0xf) - 10 + 'A';
        }
        c=(c>>4)&0xf;
        code0=c+'0';
        if (c > 9){
            code0=c - 10 + 'A';
        }
        code2='\0';
        encodedString+='%';
        encodedString+=code0;
        encodedString+=code1;
        //encodedString+=code2;
      }
      yield();
    }
    return encodedString;
}


// Check if photo capture was successful
bool checkPhoto( fs::FS &fs ) {
  File f_pic = fs.open( FILE_PHOTO );
  unsigned int pic_sz = f_pic.size();
  return ( pic_sz > 100 );
}

// Capture Photo and Save it to SPIFFS
void capturePhotoSaveSpiffs( void ) {
  camera_fb_t * fb = NULL; // pointer
  bool ok = 0; // Boolean indicating if the picture has been taken correctly

  do {
    // Take a photo with the camera
    Serial.println("Taking a photo...");

    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      return;
    }

    // Photo file name
    Serial.printf("Picture file name: %s\n", FILE_PHOTO);
    File file = SPIFFS.open(FILE_PHOTO, FILE_WRITE);

    // Insert the data in the photo file
    if (!file) {
      Serial.println("Failed to open file in writing mode");
    }
    else {
      file.write(fb->buf, fb->len); // payload (image), payload length
      Serial.print("The picture has been saved in ");
      Serial.print(FILE_PHOTO);
      Serial.print(" - Size: ");
      Serial.print(file.size());
      Serial.println(" bytes");
    }
    // Close the file
    file.close();
    esp_camera_fb_return(fb);

    // check if file has been correctly saved in SPIFFS
    ok = checkPhoto(SPIFFS);
  } while ( !ok );
}


//////////////////////////////////////////////////////////////LOOP///////////////////////////////////////////////////////////////////////
// talvez as conexões devam ser fechadas liberando CPU https://github.com/espressif/esp-idf/issues/2101
void loop() {
  if(wm_nonblocking) wm.process(); // avoid delays() in loop when non-blocking and other long running code  
  
  timerWrite(timer, 0);
  checkButton();
  
  if (takeNewPhoto) {
    if (flashFlag == "1"){
      digitalWrite(FLASH_LED, HIGH);
      delay(10);
      capturePhotoSaveSpiffs();
      takeNewPhoto = false;
      digitalWrite(FLASH_LED, LOW); 
   }
   else {
      capturePhotoSaveSpiffs();
      takeNewPhoto = false;
   }
  }

  if (millis() - blinkTime > 7000){
    digitalWrite(DEBUG_LED, LOW);
    blinkTime = millis();
    _blink = true;
  }
  
  else if (millis() - blinkTime > 5000 && _blink == true) {
    digitalWrite(DEBUG_LED, HIGH);
    _blink = false;
  }

  
// Uncomment and comment bellow to run photosCap
//  if (photoFlag){ 
//    if (millis() - firstPhoto > adjustInterval){ 
//        Serial.println("First photo!");
//        digitalWrite(DEBUG_LED, LOW);
//        saveCapturedImage();
//        photoFlag = false;
//        lastPhoto = millis();  
//    }
//  }
//
//  else{
//    if((millis() - lastPhoto) > interval){
//      Serial.println("New photo captured!");
//      digitalWrite(DEBUG_LED, LOW);
//        lastPhoto = millis();      
//        saveCapturedImage();
//    }
//  }
//  
//  delay(100);

// Uncomment and comment above to test camera
  if (photoFlag){
    if (startTest) {
      _time = millis();
      saveCapturedImage();
      photoFlag = false;         
    }  
  }
  else if (!photoFlag){
    if (millis() - _time > 60000){
      _time = millis();
      saveCapturedImage();
    }
  }
  delay(100);
}
