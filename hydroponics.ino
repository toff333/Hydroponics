#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_SH1106.h>
#include <DS3231.h>

namespace pins {
  const byte tds_sensor = A1;
  const byte ph_sensor = A0;
  const byte temp_sensor = 12; 

  const byte EC_Isolator = 11; // 3906 PNP TYPE TRANSISTOR THIS is used to connect and disconnect the 3.3V wire
  const byte EC_GND_Wire = 10; // 2N2222 NPN TRANSISTOR THIS IS USED TO CONNECT AND DISCONNECT THE GND WIRE
  
  const byte phUp_relay = 5;
  const byte phDown_relay = 6;
  const byte tds_relay = 4;
  
  const byte irrigation_pump_relay = 2;
}

namespace sensorReading {
  float ec = 0; // electrical conductivity in tds
  unsigned int tds = 0;
  float waterTemp = 0;
  float ph = 0;
}

namespace sensorCalibration {
  float forEc = 1;
  float forPh = 21.34 - 4.95;
  
  float nano_aref = 5.0;
  float nano_aref_adcRange = 1024;
  float kValue = 0.904;
}

namespace oled {
  const byte SCREEN_WIDTH = 128;
  const byte SCREEN_HEIGHT = 64;
  const byte RESET_PIN = -1;
}

namespace timing {
  unsigned long phRead_previousMillis = 0; 
  const long phRead_repeat = 100;
  unsigned long tdsRead_previousMillis = 0; 
  const long tdsRead_repeat = 500;
  unsigned long oledDisplay_previousMillis = 0; 
  const long oledDisplay_repeat = 1000;
  unsigned long phRelay_previousMillis = 0;
  const long phRelay_repeat = 5000; 
  unsigned long tdsRelay_previousMillis = 0;
  const long tdsRelay_repeat = 5000; 
  unsigned long perSec_previousMillis = 0;
  const long perSec = 1000;
}

Adafruit_SH1106 display(oled::RESET_PIN);
OneWire oneWire(pins::temp_sensor);
DallasTemperature dallasTemperature(&oneWire);
DS3231 clock;
RTCDateTime dt;

bool phUp_relay_state = true;
bool phDown_relay_state = true;
bool tdsRelay_state = true;
bool irrigation_pump_state = true;

void setup() {
  Serial.begin(9600);
  Wire.begin();
  dallasTemperature.begin();
  
  pinMode(pins::EC_Isolator, OUTPUT);
  pinMode(pins::EC_GND_Wire, OUTPUT);
  digitalWrite(pins::EC_Isolator, HIGH);
  digitalWrite(pins::EC_GND_Wire, LOW);
  
  pinMode(pins::phUp_relay, OUTPUT);
  pinMode(pins::phDown_relay, OUTPUT);
  pinMode(pins::tds_relay, OUTPUT);
  pinMode(pins::irrigation_pump_relay, OUTPUT);

  display.begin(SH1106_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(WHITE);
  
  clock.begin();
  clock.setDateTime(__DATE__, __TIME__);
}

void loop() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - timing::tdsRead_previousMillis >= timing::tdsRead_repeat) {
    timing::tdsRead_previousMillis = currentMillis;  
    digitalWrite(pins::EC_Isolator, LOW);
    digitalWrite(pins::EC_GND_Wire, HIGH);
    tds_sensor();
  } else if (currentMillis - timing::phRead_previousMillis >= timing::phRead_repeat) {
    timing::phRead_previousMillis = currentMillis;
    temp_sensor();
    digitalWrite(pins::EC_Isolator, HIGH);
    digitalWrite(pins::EC_GND_Wire, LOW);
    ph_sensor();
  }

  if (currentMillis - timing::oledDisplay_previousMillis >= timing::oledDisplay_repeat) { 
    timing::oledDisplay_previousMillis = currentMillis;  
    display_to_oled();   
  }
  
  if (currentMillis - timing::phRelay_previousMillis >= timing::phRelay_repeat) { 
    timing::phRelay_previousMillis = currentMillis;
    ph_pump();   
  }
  
  if (currentMillis - timing::tdsRelay_previousMillis >= timing::tdsRelay_repeat) { 
    timing::tdsRelay_previousMillis = currentMillis;  
    tds_pump();   
  }
  
  if (currentMillis - timing::perSec_previousMillis >= timing::perSec) { 
    timing::perSec_previousMillis = currentMillis;
    dt = clock.getDateTime();
    Serial.print(dt.month); Serial.print("/"); Serial.print(dt.day); Serial.print("/"); Serial.print(dt.year); 
    Serial.print(" "); 
    Serial.print(dt.hour); Serial.print(":"); Serial.print(dt.minute); Serial.print(":"); Serial.print(dt.second); 
    Serial.println("");
    
    irrigation_pump(dt);
    
    Serial.print("Temperature: "); Serial.println(sensorReading::waterTemp, 2);
    Serial.print("pH Val: "); Serial.println(sensorReading::ph);
    Serial.print("TDS: "); Serial.println(sensorReading::tds);
    Serial.print("EC: "); Serial.println(sensorReading::ec, 2);
    Serial.println("");
  }
  
  digitalWrite(pins::phUp_relay, phUp_relay_state);
  digitalWrite(pins::phDown_relay, phDown_relay_state);
  digitalWrite(pins::tds_relay, tdsRelay_state);
  digitalWrite(pins::irrigation_pump_relay, irrigation_pump_state);
}

// Temp Sensor Reading
void temp_sensor() {
  dallasTemperature.requestTemperatures();
  sensorReading::waterTemp = dallasTemperature.getTempCByIndex(0);
  // Serial.print(F("Temperature: ")); Serial.println(sensorReading::waterTemp, 2);
}

// TDS Sensor Reading
void tds_sensor() {
  float rawEc = analogRead(pins::tds_sensor) * sensorCalibration::nano_aref / sensorCalibration::nano_aref_adcRange;
  float temperatureCoefficient = 1.0 + 0.02 * (sensorReading::waterTemp - 25.0); 
  sensorReading::ec = (rawEc / temperatureCoefficient) * sensorCalibration::forEc;
  sensorReading::tds = (133.42 * pow(sensorReading::ec, 3) - 255.86 * pow(sensorReading::ec, 2) + 857.39 * sensorReading::ec) * sensorCalibration::kValue;
  // Serial.print(F("TDS: ")); Serial.println(sensorReading::tds);
  // Serial.print(F("EC: ")); Serial.println(sensorReading::ec, 2);
}

// PH Sensor Reading
void ph_sensor() {
  unsigned long int avgval;
  int buffer_arr[10] ;
  int temp;
  for (int i = 0; i < 10; i++) {
    buffer_arr[i] = analogRead(pins::ph_sensor);
    delay(30);
  }
  for (int i = 0; i < 9; i++) {
    for (int j = i + 1; j < 10; j++) {
      if (buffer_arr[i] > buffer_arr[j]) {
        temp = buffer_arr[i];
        buffer_arr[i] = buffer_arr[j];
        buffer_arr[j] = temp;
      }
    }
  }
  avgval = 0;
  for (int i = 2; i < 8; i++){
    avgval += buffer_arr[i];
  }
  float volt = (float)avgval * 3.3 / 1024 / 6; // the original was float volt=(float)avgval*5.0/1024/6; when its connected with arduino's 5v
  sensorReading::ph = -5.70 * volt + sensorCalibration::forPh;
  // Serial.print("pH Val: "); Serial.println(sensorReading::ph);
}

// PH pump
void ph_pump() {
  if (sensorReading::ph > 6.2) {
    phUp_relay_state = true;
    phDown_relay_state = false;
  } else if (sensorReading::ph < 5.8) {
    phUp_relay_state = false;
    phDown_relay_state = true;
  } else {
    phUp_relay_state = true;
    phDown_relay_state = true;
  }
}

// TDS pump
void tds_pump() {
  if (sensorReading::tds < 750){
    tdsRelay_state = false;
  } else {
    tdsRelay_state = true;
  }
}

// Irrigation Pump
void irrigation_pump(RTCDateTime dateTime) {
  Serial.print("Irrigation Status: ");
  if(dateTime.hour>=5 && dateTime.hour<18) {
    if ((dateTime.minute>=0 && dateTime.minute<15) || (dateTime.minute>=30 && dateTime.minute<45)) {
      irrigation_pump_state = false;
      Serial.println("ON");
    } else {
      irrigation_pump_state = true;
      Serial.println("OFF");
    }
  }
}

// Oled display
void display_to_oled() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print("pH:");

  display.setTextSize(2);
  display.setCursor(60, 0);
  display.print(sensorReading::ph);

  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print("Temp:");

  display.setTextSize(2);
  display.setCursor(60, 16);
  display.print(sensorReading::waterTemp);

  display.setTextSize(2);
  display.setCursor(0, 32);
  display.print("EC:");

  display.setTextSize(2);
  display.setCursor(60, 32);
  display.print(sensorReading::ec);

  display.setTextSize(2);
  display.setCursor(0, 48);
  display.print("TDS:");

  display.setTextSize(2);
  display.setCursor(60, 48);
  display.print(sensorReading::tds);

  display.display();
}
