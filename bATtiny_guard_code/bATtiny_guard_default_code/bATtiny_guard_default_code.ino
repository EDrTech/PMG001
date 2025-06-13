#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <Wire.h>

#define INA219_ADDRESS 0x40
#define TMP102_ADDRESS 0x49
#define ADS1015_ADDRESS 0x48

volatile uint16_t pa7_timer_count = 0;
volatile uint16_t pa3_timer_count = 0;
volatile bool pa7_pressed = false;
volatile bool pa3_pressed = false;
volatile bool pa7_handled = false;
volatile bool pa3_handled = false;

void setup() {
  Serial.begin(115200);
  while (RTC.STATUS > 0)
    {
        ;  // Wait for all registers to be synchronized
    }
  RTC.CLKSEL = RTC_CLKSEL_INT32K_gc;  // 32.768kHz Internal Ultra-Low-Power Oscillator (OSCULP32K)
  RTC.PITINTCTRL = RTC_PI_bm;  // PIT Interrupt: enabled
  RTC.PITCTRLA = RTC_PERIOD_CYC32768_gc | RTC_PITEN_bm;  // Enable PIT counter: enabled 

  PORTA.DIRCLR = PIN7_bm | PIN3_bm;  
  PORTA.PIN7CTRL = PORT_PULLUPEN_bm | PORT_ISC_BOTHEDGES_gc;
  PORTA.PIN3CTRL = PORT_PULLUPEN_bm | PORT_ISC_BOTHEDGES_gc;

  PORTA.DIRSET = PIN2_bm | PIN6_bm;
  PORTA.OUTCLR = PIN2_bm;  // PA2 initially off
  PORTA.OUTCLR = PIN6_bm;  // PA6 initially off

  cli();

  takeOverTCA0();  // Ensure control of TCA0 timer

  // Configure TCA0 for 100ms interval
  TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV64_gc;  
  TCA0.SINGLE.PER = 1562;  // 100ms with 10MHz clock and 64 prescaler
  TCA0.SINGLE.CTRLA |= TCA_SINGLE_ENABLE_bm; // Enable the timer
  TCA0.SINGLE.INTCTRL = TCA_SINGLE_OVF_bm;   // Enable overflow interrupt

  sei();
  Wire.begin();
  set_sleep_mode(SLEEP_MODE_STANDBY);  // Set sleep mode to standby
}

void loop() {
  // Enter standby mode when idle
  if (!pa7_pressed && !pa3_pressed) {
    sleep_mode();  // Enter standby mode
  }
}

ISR(PORTA_PORT_vect) {

  // Debounce delay
  delay(10);

  if (PORTA.INTFLAGS & PIN7_bm) {
    pa7_pressed = !(PORTA.IN & PIN7_bm); // LOW when pressed

    if (pa7_pressed) {
      pa7_timer_count = 0; // Start counting when button is pressed
      pa7_handled = false; // Reset action flag
    }
    PORTA.INTFLAGS = PIN7_bm;
  }

  if (PORTA.INTFLAGS & PIN3_bm) {
    pa3_pressed = !(PORTA.IN & PIN3_bm); // LOW when pressed

    if (pa3_pressed) {
      pa3_timer_count = 0; // Start counting when button is pressed
      pa3_handled = false; // Reset action flag
    }
    PORTA.INTFLAGS = PIN3_bm;
  }
  Serial.flush();
}

ISR(TCA0_OVF_vect) {
  TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;

  // PA7 tracking
  if (pa7_pressed && !pa7_handled) {
    pa7_timer_count++;

    if (!(PORTA.OUT & PIN6_bm) && pa7_timer_count >= 20) { // PA6 is off, 200ms
      PORTA.OUTSET = PIN6_bm; // Turn PA6 on (LOW state)

      pa7_handled = true; // Mark action as handled
    } else if ((PORTA.OUT & PIN6_bm) && pa7_timer_count >= 300) { // PA6 is on, 3s
      PORTA.OUTCLR = PIN6_bm; // Turn PA6 off (HIGH state)

      pa7_handled = true; // Mark action as handled
    }
  }

  // PA3 tracking
  if (pa3_pressed && !pa3_handled) {
    pa3_timer_count++;

    if (pa3_timer_count >= 2) {  // 200ms
      PORTA.OUTTGL = PIN2_bm; // Toggle PA2
      pa3_handled = true; // Mark action as handled
    }
  }

  // Reset timer counts when buttons are released
  if (!pa7_pressed) {
    pa7_timer_count = 0;
  }
  if (!pa3_pressed) {
    pa3_timer_count = 0;
  }
}

ISR(RTC_PIT_vect) 
{
    RTC.PITINTFLAGS = RTC_PI_bm;  // Clear interrupt flag by writing '1' (required)
    float tbuf = readTMP102();
    float ain0buf = ADS1015_readChannel(0);
    float ain1buf = ADS1015_readChannel(1);
    float ain2buf = ADS1015_readChannel(2);
    float ain3buf = ADS1015_readChannel(3);
    float bvbuf = INA219_readBV();
    float svbuf = INA219_readSV();
    float bcbuf = INA219_readC();

    Serial.write(12);  // Clear screen and move cursor to home
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
    output += "PWR_SW:  ";
    output += String(!digitalRead(PIN_PA7));
    output += "\r\n";
    output += "PA3_SW:  ";
    output += String(!digitalRead(PIN_PA3));
    output += "\r\n";
    output += "BAT_OUT:  ";
    output += String(digitalRead(PIN_PA6));
    output += "\r\n";
    output += "LED_BUILTIN:  ";
    output += String(digitalRead(PIN_PA2));
    output += "\r\n";
    output += "-----------------------------------\r\n";
    Serial.print(output);

    Serial.flush();
}

void INA219_setCal() 
{
    uint16_t calByte = 4096;  // Calibration register value
    uint16_t configBytes = 0b0011100110011111;  // Configuration register value

    Wire.beginTransmission(INA219_ADDRESS);
    Wire.write(0x05);  // Calibration register address
    Wire.write((uint8_t)(calByte >> 8));  // High byte
    Wire.write((uint8_t)(calByte & 0xFF));  // Low byte
    Wire.endTransmission();

    Wire.beginTransmission(INA219_ADDRESS);
    Wire.write(0x00);  // Configuration register address
    Wire.write((uint8_t)(configBytes >> 8));  // High byte
    Wire.write((uint8_t)(configBytes & 0xFF));  // Low byte
    Wire.endTransmission();
}

float INA219_readBV() 
{
    uint16_t busVoltReg = 0x00;
    INA219_setCal();

    Wire.beginTransmission(INA219_ADDRESS);
    Wire.write(0x02);  // Bus voltage register address
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
    Wire.write(0x01);  // Shunt voltage register address
    Wire.endTransmission();

    Wire.requestFrom(INA219_ADDRESS, 2);
    if (Wire.available() == 2) 
    {
        shuntVoltReg = (Wire.read() << 8) | Wire.read();
    }

    return shuntVoltReg * 0.1;
}

float INA219_readC() 
{
    uint16_t currentReg = 0x00;
    INA219_setCal();

    Wire.beginTransmission(INA219_ADDRESS);
    Wire.write(0x04);  // Current register address
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
    Wire.write(0X00);  // Temperature register address
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
    Wire.write(0x01);  // Configuration register address
    Wire.write((uint8_t)(configBytes >> 8));  // High byte
    Wire.write((uint8_t)(configBytes & 0xFF));  // Low byte
    Wire.endTransmission();

    delay(10);  // Wait for conversion to complete

    Wire.beginTransmission(ADS1015_ADDRESS);
    Wire.write(0x00);  // Conversion register address
    Wire.endTransmission();

    Wire.requestFrom(ADS1015_ADDRESS, 2);
    if (Wire.available() == 2) 
    {
        voltReg = (Wire.read() << 8) | Wire.read();
    }

    return (voltReg >> 4) * (4.096f / (32768 >> 4));
}
