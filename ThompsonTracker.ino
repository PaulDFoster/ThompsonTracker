#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFiMulti.h>
#include <CRC32.h>

#define COMMON_ANODE
#define SEALEVELPRESSURE_HPA (1013.25)

extern "C" {
  #include "user_interface.h" // this is for the RTC memory read/write functions
}

typedef struct {
  byte markerFlag;
  byte counter;
  unsigned long sleepTime;
} rtcStore __attribute__((aligned(4))); // not sure if the aligned stuff is necessary, some posts suggest it is for RTC memory?

rtcStore rtcMem;

const unsigned long ONE_SECOND = 1000 * 1000;   
const unsigned long ONE_HOUR = 60 * 60 * ONE_SECOND; 

int button = A0; // Pin on which button is attached
int redPin = D6;
int greenPin = D7;
int bluePin = D8;

bool buttonWake = false;
boolean firstTIme = false;

Adafruit_BME280 bme; // I2C
ESP8266WiFiMulti wifiMulti;

void setup() {
    pinMode(button, INPUT);
    if(analogRead(button)>1000)
    {
      buttonWake = true;
    }

    Serial.begin(115200);
    while(analogRead(button)>1000)
    {
      Serial.println("A0 High");
      delay(10);
    }
    
    system_rtc_mem_read(65, &rtcMem, sizeof(rtcMem));
    // 126 in [0] is a marker to detect first use   
    if (rtcMem.markerFlag != 126) {
       rtcMem.markerFlag = 126;
       rtcMem.counter = 0;
       rtcMem.sleepTime = ONE_HOUR;
       firstTIme = true;
    } else {
       rtcMem.counter += 1;
       firstTIme = false;
       Serial.print("Counter:");
       Serial.println(rtcMem.counter);
    }

    system_rtc_mem_write(65, &rtcMem, sizeof(rtcMem));
    
    pinMode(redPin, OUTPUT);
    pinMode(greenPin, OUTPUT);
    pinMode(bluePin, OUTPUT); 
        
    bool status;
    status = bme.begin(0x76);  
    if (!status) {
        // PRODUCTION INDICATOR
        // Set RGBLed to RED
        setColor(255,0,0);
        Serial.println("Could not find a valid BME280 sensor, check wiring!");
        delay(20000); // 20 second indicator of RED LED. Then start deepsleep cycle again
    }
    else
    {
        if (buttonWake)
        {
          Serial.println("ButtonWake True");
          setColor(0,255,0); // Green
        }
        else
        {
          Serial.println("ButtonWake False");
          setColor(0,0,255); // Blue 
        }
        
        recordAndSendValues(buttonWake);
        
        delay(1000);
    }
    setColor(0,0,0); //Turn LED off

    digitalWrite(redPin,LOW);
    digitalWrite(greenPin, LOW);
    digitalWrite(bluePin, LOW);
    
    ESP.deepSleep(5e6); // Deep Sleep for 5 seconds.
}

void loop() {
// Don't put anything here, it won't run
}

void httpSendValues(byte binaryPayload[], int arraySize){
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP("<SSID>", "<WiFiPASSWORD>");


  int attempts = 0;
  while(attempts<10)
  {
    setColor(255, 255, 255);  // Red
    if ((wifiMulti.run() == WL_CONNECTED)) {
  
      HTTPClient http;
  
      Serial.print("[HTTP] begin...\n");
      attempts = 10; // Attempts to connect to wifi before giving up
  
      // configure target server and url
  
      //http.begin("http://user:password@192.168.1.12/test.html");
      http.begin("http://paulfotestendpoint.azurewebsites.net/api/values/");
  
      /*
  
        // or
  
        http.begin("http://192.168.1.12/test.html");
  
        http.setAuthorization("user", "password");
  
        // or
  
        http.begin("http://192.168.1.12/test.html");
  
        http.setAuthorization("dXNlcjpwYXN3b3Jk");
  
      */
      setColor(0, 255, 0);  // green
      //http.begin("http://jsonplaceholder.typicode.com/posts");
      //http.addHeader("Content-Type", "application/x-binary");
      int httpCode = http.GET();//(const char*)binaryPayload); // incorrect payload delivery
      //http.writeToStream(&Serial);
  
      // httpCode will be negative on error
  
      if (httpCode > 0) {
  
        // HTTP header has been send and Server response header has been handled
  
        Serial.printf("[HTTP] GET... code: %d\n", httpCode);
  
        // file found at server
  
        if (httpCode == HTTP_CODE_OK) {
  
          String payload = http.getString();
  
          Serial.println(payload);
  
          // TODO: Clear the EEPROM of data as we have sent it all
          // EEPROM can hold X days of data, so justs need one successful send within X days. To have cover for the whole trip
          
        }
  
      } else {
  
        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  
      }
  
      http.end();
  
    }
    else
    {
      Serial.println("Failed to connect to wifi");
      attempts++;
      delay(1000);
    }
  }
}

void recordAndSendValues(bool send) {
    //float _temperature, _pressure, _humidity, _altitude;
    int _temperature, _pressure, _humidity, _altitude; // int forces rounding/truncation
    
    _temperature = bme.readTemperature();
    _pressure = bme.readPressure() / 100.0F;
    _humidity = bme.readHumidity();
    _altitude = bme.readAltitude(SEALEVELPRESSURE_HPA);
  
    // calculate checksum here
    CRC32 crc;
    crc.update(_temperature);
    crc.update(_pressure);
    crc.update(_humidity);
    //crc.update(byteBuffer[i]); // This will be the timestamp (time period count)
    uint32_t checksum = crc.finalize();
    byte payload[4] = {checksum,_temperature, _humidity, _pressure};

    Serial.print("C:");
    Serial.println(checksum);
    
    // Store latest payload into RTC Memory (or EEPROM??)

    // Proceed to transmission of all data or finish
    if(send)
    {
      httpSendValues(payload,sizeof(payload));
      buttonWake = false;
    }
    
}

void setColor(int red, int green, int blue)
{
  #ifdef COMMON_ANODE
    red = 255 - red;
    green = 255 - green;
    blue = 255 - blue;
  #endif
  analogWrite(redPin, red);
  analogWrite(greenPin, green);
  analogWrite(bluePin, blue);  
}
