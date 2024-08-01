#include <Wire.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>



#define ADS1015_ADDRESS 0x48
#define TMP102_ADDRESS 0x49
#define INA219_ADDRESS 0x40
unsigned long secondCountdown1 = 0, secondCountdown2 = 0;
bool pa7held = false;
bool sw = false;
bool actionTaken = false;
bool released = true;
bool LEDstate = LOW;

#include <Wire.h>

#define INA219_ADDRESS 0x40
#define TMP102_ADDRESS 0x48
#define ADS1015_ADDRESS 0x48

// Function to set calibration for INA219
void INA219_setCal() {
  uint16_t calByte = 4096;                    // Calibration register value
  uint16_t configBytes = 0b0011100110011111;  // Configuration register value

  // Write to calibration register
  Wire.beginTransmission(INA219_ADDRESS);
  Wire.write(0x05);                       // Calibration register address
  Wire.write((uint8_t)(calByte >> 8));    // High byte
  Wire.write((uint8_t)(calByte & 0xFF));  // Low byte
  Wire.endTransmission();

  // Write to configuration register
  Wire.beginTransmission(INA219_ADDRESS);
  Wire.write(0x00);                           // Configuration register address
  Wire.write((uint8_t)(configBytes >> 8));    // High byte
  Wire.write((uint8_t)(configBytes & 0xFF));  // Low byte
  Wire.endTransmission();
}

// Function to read bus voltage from INA219
float INA219_readBV() {
  float resV = 0;
  uint16_t busVoltReg = 0x00;
  INA219_setCal();

  // Request bus voltage register
  Wire.beginTransmission(INA219_ADDRESS);
  Wire.write(0x02);  // Bus voltage register address
  Wire.endTransmission();

  // Read bus voltage
  Wire.requestFrom(INA219_ADDRESS, 2);
  if (Wire.available() == 2) {
    busVoltReg = (Wire.read() << 8) | Wire.read();
  }

  resV = ((busVoltReg >> 3) * 0.001) * 4;
  return resV;
}

// Function to read shunt voltage from INA219
float INA219_readSV() {
  float resV = 0;
  uint16_t shuntVoltReg = 0x00;
  INA219_setCal();

  // Request shunt voltage register
  Wire.beginTransmission(INA219_ADDRESS);
  Wire.write(0x01);  // Shunt voltage register address
  Wire.endTransmission();

  // Read shunt voltage
  Wire.requestFrom(INA219_ADDRESS, 2);
  if (Wire.available() == 2) {
    shuntVoltReg = (Wire.read() << 8) | Wire.read();
  }

  resV = shuntVoltReg * 0.1;
  return resV;
}

// Function to read current from INA219
float INA219_readC() {
  float resC = 0;
  uint16_t currentReg = 0x00;
  INA219_setCal();

  // Request current register
  Wire.beginTransmission(INA219_ADDRESS);
  Wire.write(0x04);  // Current register address
  Wire.endTransmission();

  // Read current
  Wire.requestFrom(INA219_ADDRESS, 2);
  if (Wire.available() == 2) {
    currentReg = (Wire.read() << 8) | Wire.read();
  }

  resC = currentReg;
  return resC;
}

// Function to read temperature from TMP102
float readTMP102() {
  int16_t dTemp;
  float reading = 0;

  // Request temperature register
  Wire.beginTransmission(TMP102_ADDRESS);
  Wire.write(0X00);  // Temperature register address
  Wire.endTransmission();

  uint8_t regByte[2];

  // Read temperature
  Wire.requestFrom(TMP102_ADDRESS, 2);
  regByte[0] = Wire.read();
  regByte[1] = Wire.read();

  dTemp = ((regByte[0]) << 4) | (regByte[1] >> 4);
  if (dTemp > 0x7FF) {
    dTemp |= 0xF000;
  }
  reading = dTemp * 0.0625;
  return reading;
}

// Function to read a specific channel from ADS1015
float ADS1015_readChannel(uint8_t channel) {
  uint16_t mux;
  switch (channel) {
    case 0:
      mux = 0x4000;
      break;
    case 1:
      mux = 0x5000;
      break;
    case 2:
      mux = 0x6000;
      break;
    case 3:
      mux = 0x7000;
      break;
    default:
      return -1;  // Invalid channel
  }

  float resV = 0;
  uint16_t voltReg = 0x00;
  uint16_t configBytes = 0x0000;

  // Set up configuration bytes for ADS1015
  configBytes |= 0x0200;  // Set gain and mode
  configBytes |= mux;     // Set channel
  configBytes |= 0x0080;  // Set data rate
  configBytes |= 0x8000;  // Start single conversion

  // Write to configuration register
  Wire.beginTransmission(ADS1015_ADDRESS);
  Wire.write(0x01);                           // Configuration register address
  Wire.write((uint8_t)(configBytes >> 8));    // High byte
  Wire.write((uint8_t)(configBytes & 0xFF));  // Low byte
  Wire.endTransmission();

  delay(10);  // Wait for conversion to complete

  // Request conversion result
  Wire.beginTransmission(ADS1015_ADDRESS);
  Wire.write(0x00);  // Conversion register address
  Wire.endTransmission();

  // Read conversion result
  Wire.requestFrom(ADS1015_ADDRESS, 2);
  if (Wire.available() == 2) {
    voltReg = (Wire.read() << 8) | Wire.read();
  }

  resV = (voltReg >> 4);
  return resV * (4.096f / (32768 >> 4));
}

// Setup function
void setup() {
  Serial.begin(115200);

  pinConfigure(PIN_PA7, (PIN_DIR_INPUT | PIN_PULLUP_ON));                  // Configure PWR_SW
  pinConfigure(PIN_PA3, (PIN_DIR_INPUT | PIN_PULLUP_ON));                  // Configure SWITCH
  pinConfigure(PIN_PA6, (PIN_DIR_OUTPUT | PIN_PULLUP_ON | PIN_OUT_HIGH));  // Configure P_ON
  pinConfigure(PIN_PA2, (PIN_DIR_OUTPUT | PIN_PULLUP_OFF | PIN_OUT_LOW));  // Configure LED

  Wire.begin();
  delay(100);  // Allow time for setup
}

void loop() {
  if (digitalRead(PIN_PA7)) {
    // Button is released
    secondCountdown2 = millis();
    pa7held = false;
    released = true;
    actionTaken = false;  // Reset the action taken flag
  } else {
    // Button is pressed
    if (!pa7held) {
      // First time button is pressed
      secondCountdown2 = millis();
      pa7held = true;
      released = false;
    } else if (!actionTaken) {
      // Button is held and action has not been taken yet
      if (sw == 0 && (millis() - secondCountdown2) >= 500) {
        // Held for more than 500ms
        secondCountdown2 = millis();
        digitalWrite(PIN_PA6, LOW);
        sw = 1;
        actionTaken = true;
      } else if (sw == 1 && (millis() - secondCountdown2) >= 3000) {
        // Held for more than 3000ms
        secondCountdown2 = millis();
        digitalWrite(PIN_PA6, HIGH);
        sw = 0;
        actionTaken = true;
      }
    }
  }
  static bool lastButtonStatePA3 = HIGH;
  bool readingPA3 = digitalRead(PIN_PA3);
  if (readingPA3 == LOW && lastButtonStatePA3 == HIGH) {
    // PA3 was just pressed, toggle PA2
    LEDstate = !LEDstate;
    digitalWrite(PIN_PA2, LEDstate);
  }
  lastButtonStatePA3 = readingPA3;
  if (millis() - secondCountdown1 >= 1000) {
    float tbuf, ain0buf, ain1buf, ain2buf, ain3buf, bvbuf, svbuf, bcbuf;
    // Read sensor values
    tbuf = readTMP102();
    ain0buf = ADS1015_readChannel(0);
    ain1buf = ADS1015_readChannel(1);
    ain2buf = ADS1015_readChannel(2);
    ain3buf = ADS1015_readChannel(3);
    bvbuf = INA219_readBV();
    svbuf = INA219_readSV();
    bcbuf = INA219_readC();
    Serial.write(12);
    String output = "";  // Clear screen and move cursor to home
    output += "-----------------------------------\r\n";
    output += "TMP102 Temperature:      ";
    output += String(tbuf, 2);
    output += "°C\r\n";
    output += "-----------------------------------\r\n";
    output += "ADS1015 AIN0:            ";
    output += String(ain0buf, 4);
    output += "V\r\n";
    output += "ADS1015 AIN1:            ";
    output += String(ain1buf, 4);
    output += "V\r\n";
    output += "ADS1015 AIN2:            ";
    output += String(ain2buf, 4);
    output += "V\r\n";
    output += "ADS1015 AIN3:            ";
    output += String(ain3buf, 4);
    output += "V\r\n";
    output += "-----------------------------------\r\n";
    output += "INA219 Battery voltage:  ";
    output += String(bvbuf, 4);
    output += "V\r\n";
    output += "INA219 Shunt voltage:    ";
    output += String(svbuf, 4);
    output += "mV\r\n";
    output += "INA219 Battery current:  ";
    output += String(bcbuf, 4);
    output += "mA\r\n";
    output += "-----------------------------------\r\n";
    Serial.print(output);
    secondCountdown1 = millis();
  }
}