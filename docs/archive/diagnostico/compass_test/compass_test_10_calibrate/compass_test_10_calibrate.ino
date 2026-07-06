#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Wire.h>
#include <math.h>
#include "variant.h"

// ── Pin / address config ────────────────────────────────────────────────────
#define PIN_SDA D4
#define PIN_SCL D5
#define COMPASS_ADDR 0x1E
#define HALF_US      60
#define SCL_TIMEOUT  2000

NRF_GPIO_Type* sclPort; uint32_t sclBit;
NRF_GPIO_Type* sdaPort; uint32_t sdaBit;

static inline void scl_low()  { sclPort->OUTCLR=(1UL<<sclBit); delayMicroseconds(HALF_US); }
static inline void sda_low()  { sdaPort->OUTCLR=(1UL<<sdaBit); }
static inline void sda_high() { sdaPort->OUTSET=(1UL<<sdaBit); }
static inline bool sda_read() { return (bool)((sdaPort->IN>>sdaBit)&1); }
static inline void scl_high() {
  sclPort->OUTSET=(1UL<<sclBit);
  uint32_t t=micros(); while(!((sclPort->IN>>sclBit)&1)&&(micros()-t<SCL_TIMEOUT));
  delayMicroseconds(HALF_US);
}

void i2c_init() {
  Wire.end(); delay(5);
  uint32_t sp=g_ADigitalPinMap[PIN_SCL], dp=g_ADigitalPinMap[PIN_SDA];
  sclPort=(sp<32)?NRF_P0:NRF_P1; sclBit=sp&0x1F;
  sdaPort=(dp<32)?NRF_P0:NRF_P1; sdaBit=dp&0x1F;
  uint32_t od=(GPIO_PIN_CNF_DIR_Output<<GPIO_PIN_CNF_DIR_Pos)|
              (GPIO_PIN_CNF_INPUT_Connect<<GPIO_PIN_CNF_INPUT_Pos)|
              (GPIO_PIN_CNF_PULL_Pullup<<GPIO_PIN_CNF_PULL_Pos)|
              (GPIO_PIN_CNF_DRIVE_S0D1<<GPIO_PIN_CNF_DRIVE_Pos);
  
  sclPort->OUTSET=(1UL<<sclBit); sdaPort->OUTSET=(1UL<<sdaBit);
  sclPort->PIN_CNF[sclBit]=od;   sdaPort->PIN_CNF[sdaBit]=od;
  delayMicroseconds(500);
}

void i2c_start() { sda_high();scl_high();delayMicroseconds(HALF_US);sda_low();delayMicroseconds(HALF_US);scl_low(); }
void i2c_stop()  { sda_low();delayMicroseconds(HALF_US/2);scl_high();delayMicroseconds(HALF_US);sda_high();delayMicroseconds(HALF_US); }
bool i2c_wb(uint8_t b) {
  for(int i=7;i>=0;i--){ if((b>>i)&1)sda_high();else sda_low(); delayMicroseconds(HALF_US/4); scl_high();scl_low(); }
  sda_high();delayMicroseconds(HALF_US/4);scl_high();bool a=!sda_read();scl_low();return a;
}
uint8_t i2c_rb(bool ack) {
  uint8_t v=0; sda_high();
  for(int i=7;i>=0;i--){ scl_high();if(sda_read())v|=(1<<i);scl_low();delayMicroseconds(HALF_US/4); }
  if(ack)sda_low();else sda_high(); delayMicroseconds(HALF_US/4);scl_high();scl_low();sda_high();return v;
}

bool wreg(uint8_t reg, uint8_t val) {
  for(int t=0;t<3;t++){
    if(t) delayMicroseconds(500);
    i2c_start();
    bool ok=i2c_wb(COMPASS_ADDR<<1)&&i2c_wb(reg)&&i2c_wb(val);
    i2c_stop(); if(ok)return true;
  }
  return false;
}
bool rreg(uint8_t reg, uint8_t &val) {
  i2c_start(); bool ok=i2c_wb(COMPASS_ADDR<<1)&&i2c_wb(reg); i2c_stop();
  if(!ok)return false;
  delayMicroseconds(50);
  i2c_start(); ok=i2c_wb((COMPASS_ADDR<<1)|1); if(ok)val=i2c_rb(false); i2c_stop();
  return ok;
}

bool readAxes(float &cx, float &cy, float &cz) {
  if(!wreg(0x22, 0x01)) return false;
  uint32_t t0=millis(); bool drdy=false;
  while(millis()-t0<500){
    uint8_t s=0; if(rreg(0x27,s)&&(s&0x08)){drdy=true;break;} delayMicroseconds(500);
  }
  if(!drdy) return false;
  uint8_t xl=0,xh=0,yl=0,yh=0,zl=0,zh=0;
  if(!(rreg(0x28,xl)&&rreg(0x29,xh)&&rreg(0x2A,yl)&&rreg(0x2B,yh)&&rreg(0x2C,zl)&&rreg(0x2D,zh))) return false;
  cx=(int16_t)(xl|((uint16_t)xh<<8))*(100.0f/6842.0f);
  cy=(int16_t)(yl|((uint16_t)yh<<8))*(100.0f/6842.0f);
  cz=(int16_t)(zl|((uint16_t)zh<<8))*(100.0f/6842.0f);
  return true;
}

float cal_x_min = 9999, cal_x_max = -9999;
float cal_y_min = 9999, cal_y_max = -9999;
float cx_offset = 0, cy_offset = 0;
float cx_scale = 1, cy_scale = 1;

String getDirection(float heading) {
  if (heading < 22.5 || heading >= 337.5) return "N";
  if (heading < 67.5) return "NE";
  if (heading < 112.5) return "E";
  if (heading < 157.5) return "SE";
  if (heading < 202.5) return "S";
  if (heading < 247.5) return "SW";
  if (heading < 292.5) return "W";
  if (heading < 337.5) return "NW";
  return "?";
}

void setup() {
  Serial.begin(115200);
  while(!Serial&&millis()<5000); delay(1000);

  Serial.println("\n\n=== COMPASS CALIBRATION DEMO ===");

  i2c_init();

  // SILENT BUS RECOVERY
  for (uint8_t addr = 1; addr < 127; addr++) {
    i2c_start(); i2c_wb(addr << 1); i2c_stop(); delayMicroseconds(100);
  }

  uint8_t who=0;
  bool ok = false;
  for(int i=0; i<5; i++) {
    if(rreg(0x0F, who) && who == 0x3D) { ok = true; break; }
    delay(10);
  }
  
  if(!ok){
    Serial.println("Compass not found! Halting.");
    while(1) delay(1000);
  }

  Serial.println("\n[!] IMPORTANT: Pick up the device and lay it FLAT on a table.");
  Serial.println("[!] In 3 seconds, slowly spin it in a FULL 360-degree circle...");
  delay(3000);
  
  Serial.println("\n>>> SPIN THE DEVICE NOW <<<");
  Serial.print("Calibrating for 10 seconds...");

  uint32_t startTime = millis();
  uint32_t lastDot = 0;
  
  while (millis() - startTime < 10000) {
    float cx, cy, cz;
    if(readAxes(cx, cy, cz)) {
      if(cx < cal_x_min) cal_x_min = cx;
      if(cx > cal_x_max) cal_x_max = cx;
      if(cy < cal_y_min) cal_y_min = cy;
      if(cy > cal_y_max) cal_y_max = cy;
    }
    
    if (millis() - lastDot > 500) {
      Serial.print(".");
      lastDot = millis();
    }
    delay(10);
  }

  cx_offset = (cal_x_min + cal_x_max) / 2.0f;
  cy_offset = (cal_y_min + cal_y_max) / 2.0f;
  cx_scale  = (cal_x_max - cal_x_min) / 2.0f;
  cy_scale  = (cal_y_max - cal_y_min) / 2.0f;

  Serial.println("\n\n=== CALIBRATION COMPLETE ===");
  Serial.print("Hard Iron Offset X: "); Serial.print(cx_offset); Serial.println(" µT");
  Serial.print("Hard Iron Offset Y: "); Serial.print(cy_offset); Serial.println(" µT");
  Serial.println("--------------------------------\n");
}

void loop() {
  float cx, cy, cz;
  if(!readAxes(cx, cy, cz)) return;

  float cal_x = (cx_scale > 1.0f) ? (cx - cx_offset) / cx_scale : 0;
  float cal_y = (cy_scale > 1.0f) ? (cy - cy_offset) / cy_scale : 0;

  float cal_heading = atan2(cal_y, cal_x) * 180.0f / PI;
  if(cal_heading < 0) cal_heading += 360.0f;

  Serial.print("Heading: "); 
  Serial.print(cal_heading, 1); 
  Serial.print("° \t");
  Serial.println(getDirection(cal_heading));

  delay(200);
}
