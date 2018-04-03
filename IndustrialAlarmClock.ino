/*
 * DFPlayer code adapted from example code created by Angelo Qiao
 */

#include <SPI.h>
#include <Wire.h>
#include <Encoder.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include "sh1106.h"
#include "LowPower.h"

/* 
Display constants (deinfed in sh1106.h):
  VCCSTATE SH1106_SWITCHCAPVCC
  WIDTH                 128
  HEIGHT                 64
  NUM_PAGE                8

Display pins (deinfed in sh1106.h):
  OLED_RST                7 
  OLED_DC                 8
  OLED_CS                 9
  SPI_MOSI               11
  SPI_SCK                13
*/

// Mode switch pins:
#define TIME_SET_MODE_PIN      12
#define ALARM_SET_MODE_PIN     10

// Alarm arming button pin
#define ALARM_ARMED_PIN         6

// Player pins:
#define ALARM_PLAYER_RX_PIN    A2
#define ALARM_PLAYER_TX_PIN    A3

// Battery voltage pin:
#define BATTERY_VOLTAGE_PIN    A0

// Encoder pins:
#define HOURS_ENCODER_A_PIN     3
#define HOURS_ENCODER_B_PIN     5
#define MINUTES_ENCODER_A_PIN   2
#define MINUTES_ENCODER_B_PIN   4

// Icon parameters:
#define BATTERY_ICON_WIDTH     16
#define BATTERY_ICON_HEIGHT     8
#define BATTERY_ICON_X        112
#define BATTERY_ICON_Y         54
#define ALARM_ICON_WIDTH        8
#define ALARM_ICON_HEIGHT       8
#define ALARM_ICON_X            2
#define ALARM_ICON_Y           54

enum Mode { normal, time_set, alarm_set };

const uint8_t BATTERY_ICON_BITMAP_MAX[16] PROGMEM = {
    0x0F,0xFE,
    0x30,0x02,
    0x26,0xDA,
    0x26,0xDA,
    0x26,0xDA,
    0x26,0xDA,
    0x30,0x02,
    0x0F,0xFE
};

const uint8_t BATTERY_ICON_DATA_MID[16] PROGMEM = {
    0x0F,0xFE,
    0x30,0x02,
    0x20,0xDA,
    0x20,0xDA,
    0x20,0xDA,
    0x20,0xDA,
    0x30,0x02,
    0x0F,0xFE
};

const uint8_t BATTERY_ICON_DATA_MIN[16] PROGMEM = {
    0x0F,0xFE,
    0x30,0x02,
    0x20,0x1A,
    0x20,0x1A,
    0x20,0x1A,
    0x20,0x1A,
    0x30,0x02,
    0x0F,0xFE
};

const uint8_t BATTERY_ICON_DATA_EMPTY[16] PROGMEM = {
    0x0F,0xFE,
    0x30,0x02,
    0x20,0x02,
    0x20,0x02,
    0x20,0x02,
    0x20,0x02,
    0x30,0x02,
    0x0F,0xFE
};

const uint8_t ALARM_ICON_DATA[8] PROGMEM = {
    0xC3,
    0xBD,
    0x42,
    0x52,
    0x4E,
    0x42,
    0x3C,
    0xC3
};

const int32_t BUFFER_SIZE = WIDTH * HEIGHT / 8;
const int32_t SECOND_IN_MILLIS = 1000;
const int32_t MINUTE_IN_MILLIS = 60000;
const int32_t HOUR_IN_MILLIS = 3600000;
const int32_t DAY_IN_MILLIS = 86400000;
const int32_t DT_CORRECTION = 61; // [ms]
const float ANALOG_TO_VOLTAGE = 2.0*3.3/1024.0;

uint8_t oledBuffer[BUFFER_SIZE];
bool colonVisible;

Encoder hoursEncoder(HOURS_ENCODER_A_PIN, HOURS_ENCODER_B_PIN);
Encoder minutesEncoder(MINUTES_ENCODER_A_PIN, MINUTES_ENCODER_B_PIN);
int hoursEncoderPosition;
int minutesEncoderPosition;
int newEncoderPosition; 

SoftwareSerial alarmPlayerSerial(ALARM_PLAYER_RX_PIN, ALARM_PLAYER_TX_PIN);
DFRobotDFPlayerMini alarmPlayer;
bool alarmPlaying;

Mode mode = normal;
int32_t currentTime = 0;
int32_t alarmTime = 0;
float batteryVoltage = 0.0;


void setup()  {
  Serial.begin(9600);
  initializeControls();
  initializeDisplay();
  initializePlayer();
}

void initializeControls() {
  pinMode(TIME_SET_MODE_PIN, INPUT_PULLUP);
  pinMode(ALARM_SET_MODE_PIN, INPUT_PULLUP);
  pinMode(ALARM_ARMED_PIN, INPUT_PULLUP);  
  hoursEncoderPosition = hoursEncoder.read();
  minutesEncoderPosition = minutesEncoder.read();
}

void initializeDisplay() {
  SH1106_begin();
  SH1106_setContrast(0);  
  clearDisplay();
}

void initializePlayer() {
  alarmPlayerSerial.begin(9600);
  Serial.println(F("Initializing DFPlayer ... (May take 3~5 seconds)"));  
  if (!alarmPlayer.begin(alarmPlayerSerial)) {  //Use softwareSerial to communicate with mp3.
    Serial.println(F("Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!"));
    return;
  }
  Serial.println(F("DFPlayer Mini online."));

  alarmPlayer.setTimeOut(500); 
  alarmPlayer.volume(30);
  alarmPlayer.EQ(DFPLAYER_EQ_NORMAL);
  alarmPlayer.outputDevice(DFPLAYER_DEVICE_SD);
}

void loop() {  
  long t0 = millis();
  checkMode();
  updateTimeSetting();
  
  checkAlarm();
  measureBatteryVoltage();
  
  clearDisplay();
  renderMode();
  renderTime();
  renderIcons();
  displayBuffer();  

  long dt = millis()-t0;

  if ( mode == normal ) {
    LowPower.powerDown(SLEEP_500MS, ADC_OFF, BOD_OFF); 
    dt += 500;
    dt += DT_CORRECTION;
  } else {
    delay(100);
    dt += 100;
  }
  
  currentTime = trimTime( currentTime+dt );
}

void checkMode() {
  if ( digitalRead(TIME_SET_MODE_PIN) == LOW ) {
    mode = time_set;
  } else if ( digitalRead(ALARM_SET_MODE_PIN) == LOW ) {
    mode = alarm_set;    
  } else {
    mode = normal;
  }
}

void updateTimeSetting() {
  int32_t modeTime = getModeTime();
  
  newEncoderPosition = hoursEncoder.read();
  int delta = round((newEncoderPosition - hoursEncoderPosition)/6.0);
  if ( abs(delta) > 0 ) {
    if ( mode != normal ) {
      modeTime = trimTime( modeTime+delta*HOUR_IN_MILLIS );
    }
    hoursEncoderPosition = newEncoderPosition;
  }

  newEncoderPosition = minutesEncoder.read();
  delta = round((newEncoderPosition - minutesEncoderPosition)/6.0);
  if ( abs(delta) > 0 ) {
    if ( mode != normal ) {
      int32_t hours, minutes;
      getHoursAndMinutes( modeTime, hours, minutes );
      minutes += delta;
      if ( minutes > 59 ) {
        minutes -= 59;
      } else if ( minutes < 0 ) {
        minutes += 60;
      }
      modeTime = calculateTime( hours, minutes );
    }
    minutesEncoderPosition = newEncoderPosition;
  }

  setModeTime( modeTime );
}

bool alarmArmed() {
  return digitalRead(ALARM_ARMED_PIN) == LOW;
}

void checkAlarm() {
  if ( alarmArmed() ) {
    if ( !alarmPlaying && ( currentTime > alarmTime && currentTime < alarmTime+SECOND_IN_MILLIS ) ) {
      alarmPlayer.loop(1);
      alarmPlaying = true;
    }
  } else {
    if ( alarmPlaying ) {
      alarmPlayer.reset();
      alarmPlaying = false;
    }
  }
}

void incrementCurrentTime() {
  currentTime = trimTime( currentTime+SECOND_IN_MILLIS );
}

void measureBatteryVoltage() {
  int sensorValue = analogRead( BATTERY_VOLTAGE_PIN );
  batteryVoltage = ANALOG_TO_VOLTAGE*sensorValue;
}

void clearDisplay() {
  SH1106_clear(oledBuffer);
}

void renderMode() {
  if ( mode == time_set ) {
    SH1106_string(0, 2, "TIME", 12, 0, oledBuffer); 
  } else if ( mode == alarm_set ) {
    SH1106_string(98, 2, "ALARM", 12, 0, oledBuffer); 
  }
}

void renderTime() {
  int32_t modeTime = getModeTime();
  int32_t hours, minutes;
  getHoursAndMinutes( modeTime, hours, minutes );
  
  uint8_t h1 = hours/10 + 0x30;
  uint8_t h2 = hours%10 + 0x30;
  uint8_t m1 = minutes/10 + 0x30;
  uint8_t m2 = minutes%10 + 0x30;
  
  int x0 = 24;  
  int y0 = 24;
  SH1106_char1616(x0, y0, h1, oledBuffer);
  SH1106_char1616(x0+16, y0, h2, oledBuffer);
  if ( mode == normal && colonVisible ) {
    colonVisible = false;
  } else {
    SH1106_char1616(x0+32, y0, ':', oledBuffer);
    colonVisible = true;
  }
  SH1106_char1616(x0+48, y0, m1, oledBuffer);
  SH1106_char1616(x0+64, y0, m2, oledBuffer);
}

void renderIcons() {
  renderAlarmIcon();
  renderBatteryIcon();
}

void renderAlarmIcon() {
  bool armed = alarmArmed();
  if (armed) {
    SH1106_bitmap(ALARM_ICON_X, ALARM_ICON_Y, ALARM_ICON_DATA, ALARM_ICON_WIDTH, ALARM_ICON_HEIGHT, oledBuffer); 
  }
}

void renderBatteryIcon() {  
  const uint8_t *icon; 
  if ( batteryVoltage > 4.10 ) {
    icon = BATTERY_ICON_BITMAP_MAX;
  } else if ( batteryVoltage > 3.80 ) {
    icon = BATTERY_ICON_DATA_MID;
  } else if ( batteryVoltage > 3.60 ) {
    icon = BATTERY_ICON_DATA_MIN;
  } else {
    icon = BATTERY_ICON_DATA_EMPTY;
  }
  SH1106_bitmap(BATTERY_ICON_X, BATTERY_ICON_Y, icon, BATTERY_ICON_WIDTH, BATTERY_ICON_HEIGHT, oledBuffer); 
}

void displayBuffer() {
  SH1106_display(oledBuffer); 
}

int32_t trimTime( int32_t time ) {
  if ( time > DAY_IN_MILLIS ) {
    return time - DAY_IN_MILLIS;
  } else if ( time < 0 ) {
    return DAY_IN_MILLIS + time;
  } else {
    return time;
  }
}

int32_t getModeTime() {
  return ( mode == alarm_set ) ? alarmTime : currentTime;
}

void setModeTime( int32_t time ) {
  if ( mode == alarm_set ) {
    alarmTime = time;
  } else {
    currentTime = time;
  }
}

void getHoursAndMinutes( int32_t time, int32_t &hours, int32_t &minutes ) {
  hours = time / HOUR_IN_MILLIS;
  minutes = (time-hours*HOUR_IN_MILLIS) / MINUTE_IN_MILLIS;
}

int32_t calculateTime( int32_t hours, int32_t minutes ) {
  return hours*HOUR_IN_MILLIS + minutes*MINUTE_IN_MILLIS;
}

