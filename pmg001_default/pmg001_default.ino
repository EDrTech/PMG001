#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <Wire.h>

#define INA219_ADDRESS 0x40
#define TMP102_ADDRESS 0x49
#define ADS1015_ADDRESS 0x48

bool pa7_pressed = 0, pa3_pressed = 0, io_wake = 0, out_active = 0;
int pa7_pressedfor = 0, pa3_pressedfor = 0, pa7_pressedon = 0, pa3_pressedon = 0, pa7_presstime = 0, pa3_presstime = 0, rtc_ctdn = 0;

void setup() 
{
    Serial.begin(115200);
    RTC_init();    
    PORTA.PIN3CTRL = PORT_PULLUPEN_bm | PORT_ISC_BOTHEDGES_gc;  // Enable pull-up and interrupt on both edges for PA3
    PORTA.PIN7CTRL = PORT_PULLUPEN_bm | PORT_ISC_BOTHEDGES_gc;  // Enable pull-up and interrupt on both edges for PA7
    pinConfigure(PIN_PA6,(PIN_DIR_OUTPUT | PIN_PULLUP_ON | PIN_OUT_HIGH));
    pinConfigure(PIN_PA2,(PIN_DIR_OUTPUT | PIN_PULLUP_OFF | PIN_OUT_LOW));  
    PORTA.INTFLAGS = PORT_INT3_bm | PORT_INT7_bm;  // Clear any existing interrupt flags for PA3 and PA7

    set_sleep_mode(SLEEP_MODE_PWR_DOWN);  // Set sleep mode to Power Down mode
    Wire.begin();
    sei();  // Enable global interrupts
}

void RTC_init() 
{
    while (RTC.STATUS > 0)
    {
        ;  // Wait for all registers to be synchronized
    }
    RTC.CLKSEL = RTC_CLKSEL_INT32K_gc;  // 32.768kHz Internal Ultra-Low-Power Oscillator (OSCULP32K)
    RTC.PITINTCTRL = RTC_PI_bm;  // PIT Interrupt: enabled
    RTC.PITCTRLA = RTC_PERIOD_CYC32768_gc | RTC_PITEN_bm;  // Enable PIT counter: enabled
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
    Serial.print(output);
    rtc_ctdn = millis();
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

void loop() 
{
    sleep_mode();  // Put the MCU to sleep
    Serial.begin(115200);

    if(io_wake) 
    {
        while(!digitalRead(PIN_PA7)) 
        {
            pa7_presstime = millis() - pa7_pressedon;
            if((pa7_presstime >= 500) && (out_active == 0)) 
            {
                Serial.println("500ms Timer ON hit");
                out_active = 1;
                break;
            }
            if((pa7_presstime >= 3000) && (out_active == 1)) 
            {
                Serial.println("3s Timer OFF hit");
                out_active = 0;
                break;
            }
        }

        while(!digitalRead(PIN_PA3)) 
        {
            pa3_presstime = millis() - pa3_pressedon;
            if(pa3_presstime >= 100) 
            {
                digitalWrite(PIN_PA2, !digitalRead(PIN_PA2));
                Serial.println("100ms LED_BUILTIN toggle hit");
                break;
            }
        }
    }

    io_wake = 0;
    digitalWrite(PIN_PA6, out_active ? 0 : 1);
    Serial.flush();
}

ISR(PORTA_PORT_vect) 
{
    io_wake = 1;
    Serial.begin(115200);

    if (PORTA.INTFLAGS & PORT_INT3_bm) 
    {
        delay(10);
        if(!digitalRead(PIN_PA3)) 
        {
            pa3_pressed = 1;
            pa3_pressedon = millis();
        } 
        else 
        {
            pa3_pressed = 0;
            pa3_presstime = 0;
        }
    }

    if (PORTA.INTFLAGS & PORT_INT7_bm) 
    {
        delay(10);
        if(!digitalRead(PIN_PA7)) 
        {
            pa7_pressed = 1;
            pa7_pressedon = millis();
        } 
        else 
        {
            pa7_pressed = 0;
            pa7_presstime = 0;
        }
    }

    Serial.flush();
    PORTA.INTFLAGS = PORT_INT3_bm | PORT_INT7_bm;
}