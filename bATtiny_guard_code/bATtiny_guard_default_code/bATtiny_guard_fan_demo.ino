#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <Wire.h>
#include <megaTinyCore.h>

#define FAN_PIN     PIN_PA6

#define SW1_PIN     PIN_PA3
#define SW2_PIN     PIN_PC2
#define RESET_PIN   PIN_PC3

#define LED_FAULT_PIN  PIN_PA1
#define LED_WARN_PIN   PIN_PA4
#define LED_OK_PIN     PIN_PA5

#define INA219_ADDRESS 0x40
#define TMP102_ADDRESS 0x49
#define ADS1015_ADDRESS 0x48

bool fan_enabled = false;
bool fault_active = false;

float current_threshold = 160.0f;

float currentBuffer[3] = {0};
uint8_t currentIndex = 0;
bool bufferFilled = false;

unsigned long fanStartTime = 0;

bool sw1Prev = true;
bool sw2Prev = true;
bool resetPrev = true;

unsigned long lastReport = 0;
const unsigned long reportInterval = 1000;

void INA219_setCal() 
{
  uint16_t calByte = 4096;
  uint16_t configBytes = 0b0011100110011111;

  Wire.beginTransmission(INA219_ADDRESS);
  Wire.write(0x05);
  Wire.write((uint8_t)(calByte >> 8));
  Wire.write((uint8_t)(calByte & 0xFF));
  Wire.endTransmission();

  Wire.beginTransmission(INA219_ADDRESS);
  Wire.write(0x00);
  Wire.write((uint8_t)(configBytes >> 8));
  Wire.write((uint8_t)(configBytes & 0xFF));
  Wire.endTransmission();
}

float INA219_readBV() 
{
  uint16_t busVoltReg = 0x00;
  INA219_setCal();

  Wire.beginTransmission(INA219_ADDRESS);
  Wire.write(0x02);
  Wire.endTransmission();

  Wire.requestFrom(INA219_ADDRESS, 2);
  if (Wire.available() == 2) 
  {
    busVoltReg = (Wire.read() << 8) | Wire.read();
  }

  return ((busVoltReg >> 3) * 0.001) * 4;
}

float INA219_readSV() 
{
  uint16_t shuntVoltReg = 0x00;
  INA219_setCal();

  Wire.beginTransmission(INA219_ADDRESS);
  Wire.write(0x01);
  Wire.endTransmission();

  Wire.requestFrom(INA219_ADDRESS, 2);
  if (Wire.available() == 2) {
    shuntVoltReg = (Wire.read() << 8) | Wire.read();
  }

  return shuntVoltReg * 0.1;
}

float INA219_readC() 
{
  uint16_t currentReg = 0x00;
  INA219_setCal();

  Wire.beginTransmission(INA219_ADDRESS);
  Wire.write(0x04);
  Wire.endTransmission();

  Wire.requestFrom(INA219_ADDRESS, 2);
  if (Wire.available() == 2) 
  {
    currentReg = (Wire.read() << 8) | Wire.read();
  }

  return currentReg;
}

float readTMP102() 
{
  int16_t dTemp;
  Wire.beginTransmission(TMP102_ADDRESS);
  Wire.write(0x00);
  Wire.endTransmission();

  uint8_t regByte[2];
  Wire.requestFrom(TMP102_ADDRESS, 2);
  regByte[0] = Wire.read();
  regByte[1] = Wire.read();

  dTemp = ((regByte[0]) << 4) | (regByte[1] >> 4);
  if (dTemp > 0x7FF) 
  {
    dTemp |= 0xF000;
  }
  return dTemp * 0.0625;
}

float ADS1015_readChannel(uint8_t channel) 
{
  uint16_t mux;
  switch (channel) 
  {
    case 0: mux = 0x4000; break;
    case 1: mux = 0x5000; break;
    case 2: mux = 0x6000; break;
    case 3: mux = 0x7000; break;
    default: return -1;
  }

  uint16_t configBytes = 0x0000 | 0x0200 | mux | 0x0080 | 0x8000;
  uint16_t voltReg = 0x00;

  Wire.beginTransmission(ADS1015_ADDRESS);
  Wire.write(0x01);
  Wire.write((uint8_t)(configBytes >> 8));
  Wire.write((uint8_t)(configBytes & 0xFF));
  Wire.endTransmission();

  delay(10);

  Wire.beginTransmission(ADS1015_ADDRESS);
  Wire.write(0x00);
  Wire.endTransmission();

  Wire.requestFrom(ADS1015_ADDRESS, 2);
  if (Wire.available() == 2) 
  {
    voltReg = (Wire.read() << 8) | Wire.read();
  }

  return (voltReg >> 4) * (4.096f / (32768 >> 4));
}

void setup() 
{
  Serial.begin(115200);
  Wire.begin();

  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);

  pinMode(SW1_PIN, INPUT_PULLUP);
  pinMode(SW2_PIN, INPUT_PULLUP);
  pinMode(RESET_PIN, INPUT_PULLUP);

  pinMode(LED_OK_PIN, OUTPUT);
  pinMode(LED_WARN_PIN, OUTPUT);
  pinMode(LED_FAULT_PIN, OUTPUT);

  digitalWrite(LED_OK_PIN, LOW);
  digitalWrite(LED_WARN_PIN, LOW);
  digitalWrite(LED_FAULT_PIN, LOW);
}

void loop() 
{
  float current_mA = INA219_readC();
  float bvbuf = INA219_readBV();
  float svbuf = INA219_readSV();
  float tbuf = readTMP102();
  float pot_voltage = ADS1015_readChannel(2);

  if (pot_voltage >= 0.0f && pot_voltage <= 3.3f) 
  {
    current_threshold = 160.0f + ((pot_voltage / 3.3f) * (250.0f - 160.0f));
  }

  bool sw1Now = digitalRead(SW1_PIN);
  bool sw2Now = digitalRead(SW2_PIN);
  bool resetNow = digitalRead(RESET_PIN);

  if (sw1Prev && !sw1Now && !fault_active) 
  {
    fan_enabled = !fan_enabled;
    if (fan_enabled) 
    {
      fanStartTime = millis();
    }
  }
  if (sw2Prev && !sw2Now && !fault_active) 
  {
    fan_enabled = !fan_enabled;
    if (fan_enabled) 
    {
      fanStartTime = millis();
    }
  }

  if (resetPrev && !resetNow) 
  {
    fault_active = false;
    digitalWrite(LED_FAULT_PIN, LOW);
  }

  sw1Prev = sw1Now;
  sw2Prev = sw2Now;
  resetPrev = resetNow;

  digitalWrite(FAN_PIN, fan_enabled ? HIGH : LOW);

  currentBuffer[currentIndex] = current_mA;
  currentIndex = (currentIndex + 1) % 3;
  if (currentIndex == 0) bufferFilled = true;

  float avg_current = 0.0;
  int count = bufferFilled ? 3 : currentIndex;
  for (int i = 0; i < count; ++i) 
  {
    avg_current += currentBuffer[i];
  }
  avg_current /= count;

  bool ignore_current = (millis() - fanStartTime < 1000);

  if (fan_enabled && !fault_active) 
  {
    if (!ignore_current) 
    {
      if (avg_current < 120.0f) 
      {
        digitalWrite(LED_OK_PIN, HIGH);
        digitalWrite(LED_WARN_PIN, LOW);
        digitalWrite(LED_FAULT_PIN, LOW);
      }
      else if (avg_current < current_threshold) 
      {
        digitalWrite(LED_OK_PIN, HIGH);
        digitalWrite(LED_WARN_PIN, HIGH);
        digitalWrite(LED_FAULT_PIN, LOW);
      } else 
      {
        fault_active = true;
        fan_enabled = false;
        digitalWrite(FAN_PIN, LOW);
        digitalWrite(LED_FAULT_PIN, HIGH);
        digitalWrite(LED_OK_PIN, LOW);
        digitalWrite(LED_WARN_PIN, LOW);
      }
    } 
    else 
    {
      digitalWrite(LED_OK_PIN, HIGH);
      digitalWrite(LED_WARN_PIN, LOW);
      digitalWrite(LED_FAULT_PIN, LOW);
    }
  }

  if (!fan_enabled) 
  {
    digitalWrite(LED_OK_PIN, LOW); // Turn off green LED when fan is off
    digitalWrite(LED_WARN_PIN, LOW);
  }

  if (millis() - lastReport > reportInterval) 
  {
    lastReport = millis();
    Serial.write(12);  // Clear terminal

    Serial.println(F("==================================="));
    Serial.print(F("TMP102 Temperature:      ")); Serial.print(tbuf, 2); Serial.println(F("°C"));
    Serial.println(F("-----------------------------------"));
    Serial.print(F("ADS1015 AIN2 (POT):      ")); Serial.println(pot_voltage, 4);
    Serial.print(F("Current Threshold:       ")); Serial.print(current_threshold, 1); Serial.println(F(" mA"));
    Serial.println(F("-----------------------------------"));
    Serial.print(F("INA219 Battery voltage:  ")); Serial.print(bvbuf, 4); Serial.println(F(" V"));
    Serial.print(F("INA219 Battery current:  ")); Serial.print(current_mA, 4); Serial.println(F(" mA"));
    Serial.print(F("Avg Current (3 samples): ")); Serial.print(avg_current, 4); Serial.println(F(" mA"));
    Serial.println(F("-----------------------------------"));
    Serial.print(F("PA3_SW:   ")); Serial.println(!sw1Now);
    Serial.print(F("PC2_SW:   ")); Serial.println(!sw2Now);
    Serial.print(F("PC3_RESET:")); Serial.println(!resetNow);
    Serial.print(F("FAN_OUT:  ")); Serial.println(digitalRead(FAN_PIN));
    Serial.print(F("LED_OK:   ")); Serial.println(digitalRead(LED_OK_PIN));
    Serial.print(F("LED_WARN: ")); Serial.println(digitalRead(LED_WARN_PIN));
    Serial.print(F("LED_FAULT:")); Serial.println(digitalRead(LED_FAULT_PIN));
    Serial.println(F("==================================="));
  }
}
