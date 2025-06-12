/*
 * File: main.c
 * Project: Smart Watch - Final Version
 * Description: A multifunctional smartwatch with pedometer, clock, and menu system.
 * Date: March 25, 2025
 * Author: Shirel Orkabi
 */

 #include <stdlib.h>
 #include <stdio.h>
 #include <math.h>
 #include <stdint.h>
 #include <stdbool.h>
 #include <string.h>
 #include "System/system.h"
 #include "System/delay.h"
 #include "oledDriver/oledC.h"
 #include "oledDriver/oledC_colors.h"
 #include "oledDriver/oledC_shapes.h"
 #include "Accel_i2c.h"
 #include <libpic30.h>
 #include <xc.h>
 
 // Hardware Pin Definitions
 #define BUTTON1_PORT      PORTAbits.RA0
 #define BUTTON2_PORT      PORTAbits.RA1
 #define BUTTON1_TRIS      TRISAbits.TRISA0
 #define BUTTON2_TRIS      TRISAbits.TRISA1
 #define LED1_PORT         LATAbits.LATA8
 #define LED2_PORT         LATAbits.LATA9
 #define LED1_TRIS         TRISAbits.TRISA8
 #define LED2_TRIS         TRISAbits.TRISA9
 
 // Accelerometer Configuration
 #define ACCEL_WRITE_ADDR  0x3A
 #define REG_POWER_CTL     0x2D
 #define REG_DATA_X0       0x32
 #define REG_DATA_Y0       0x34
 #define REG_DATA_Z0       0x36
 #define MEASURE_MODE      0x08
 
 // Constants
 #define STEP_THRESHOLD    900.0f
 #define GRAPH_WIDTH       90
 #define GRAPH_HEIGHT      100
 #define HISTORY_SIZE      60
 #define MENU_ITEM_COUNT   5
 
 // Data Structures
 typedef struct {
     int16_t x, y, z;
 } AccelerometerData;
 
 typedef struct {
     uint8_t hours;
     uint8_t minutes;
 } TimeSetting;
 
 typedef struct {
     uint8_t day;
     uint8_t month;
 } DateSetting;
 
 typedef struct {
     uint8_t hours;
     uint8_t minutes;
     uint8_t seconds;
     uint8_t day;
     uint8_t month;
 } ClockTime;
 
 // Global Variables
 static uint8_t stepRateHistory[GRAPH_WIDTH] = {0};
 static uint32_t lastStepTimestamp = 0;
 static float currentStepPace = 0.0f;
 static bool isGraphDisplayed = false;
 
 static TimeSetting timeToSet = {4, 0}; // Default 4:00
 static uint8_t timeFieldSelected = 0;
 
 static DateSetting dateToSet = {24, 1}; // Default January 24th
 static uint8_t dateFieldSelected = 0;
 
 static bool wasStepThresholdExceeded = false;
 static bool isMovementActive = false;
 static uint16_t totalSteps = 0;
 static const float GRAVITY_BASELINE = 1024.0f;
 static uint8_t stepsPerSecond[HISTORY_SIZE] = {0};
 static uint8_t currentSecondIndex = 0;
 static float displayedStepPace = 0.0f;
 static uint32_t elapsedSeconds = 0;
 static bool use12HourFormat = true;
 static bool inTimeFormatMenu = false;
 static bool inTimeSetMenu = false;
 static uint8_t timeFormatOption = 0;
 static bool showFootIcon = false;
 static bool redrawClock = false;
 static bool justEnteredMenu = false;
 static bool inMainMenu = false;
 
 static const uint8_t DAYS_PER_MONTH[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
 static ClockTime systemClock = {4, 0, 0, 24, 1}; // 4:00:00 AM, Jan 24th
 static char lastDisplayedTime[9] = "";
 
 // Foot Icon Bitmaps (16x16)
 static const uint16_t FOOT_ICON_1[16] = {
     0x7800, 0xF800, 0xFC00, 0xFC00, 0xFC00, 0x7C1E, 0x783E, 0x047F,
     0x3F9F, 0x1F3E, 0x0C3E, 0x003E, 0x0004, 0x00F0, 0x01F0, 0x00E0
 };
 static const uint16_t FOOT_ICON_2[16] = {
     0x001E, 0x003F, 0x003F, 0x007F, 0x003F, 0x383E, 0x7C1E, 0x7E10,
     0x7E7C, 0x7E78, 0x7C30, 0x3C00, 0x2000, 0x1E00, 0x1F00, 0x0E00
 };
 
 // Function Prototypes
 void updateMenuTimeDisplay(void);
 void renderTimeFormatMenu(void);
 void renderTimeSetMenu(void);
 void displayTimeSetValues(void);
 void processTimeSetInput(void);
 bool checkTiltToSave(void);
 void displayDateSetValues(void);
 extern void renderMainMenu(void);
 
 // Error Handling
 void haltWithError(const char *message) {
     oledC_DrawString(0, 20, 1, 1, (uint8_t *)message, OLEDC_COLOR_DARKRED);
     printf("Error: %s\n", message);
     while (1);
 }
 
 // Reads accelerometer data for a specific axis
 int16_t readAccelerometerAxis(uint8_t registerAddress) {
     uint8_t lowByte, highByte;
     int retries = 3;
     for (int i = 0; i < retries; i++) {
         if (i2cReadSlaveRegister(ACCEL_WRITE_ADDR, registerAddress, &lowByte) == OK)
             break;
         if (i == retries - 1)
             haltWithError("I2C Read Error (LSB)");
         DELAY_milliseconds(10);
     }
     for (int i = 0; i < retries; i++) {
         if (i2cReadSlaveRegister(ACCEL_WRITE_ADDR, registerAddress + 1, &highByte) == OK)
             break;
         if (i == retries - 1)
             haltWithError("I2C Read Error (MSB)");
         DELAY_milliseconds(10);
     }
     return ((int16_t)highByte << 8) | lowByte;
 }
 
 // Initializes the accelerometer with retry mechanism
 void initializeAccelerometer(void) {
     I2Cerror status;
     uint8_t deviceId = 0;
     for (int i = 0; i < 3; i++) {
         status = i2cReadSlaveRegister(ACCEL_WRITE_ADDR, 0x00, &deviceId);
         if (status == OK && deviceId == 0xE5)
             break;
         if (i == 2)
             haltWithError("I2C Error or Wrong Device ID");
         DELAY_milliseconds(10);
     }
     for (int i = 0; i < 3; i++) {
         status = i2cWriteSlave(ACCEL_WRITE_ADDR, REG_POWER_CTL, MEASURE_MODE);
         if (status == OK)
             break;
         if (i == 2)
             haltWithError("Accel Power Error");
         DELAY_milliseconds(10);
     }
     for (int i = 0; i < 3; i++) {
         status = i2cWriteSlave(ACCEL_WRITE_ADDR, 0x31, 0x0B);
         if (status == OK)
             break;
         if (i == 2)
             haltWithError("Accel Data Format Error");
         DELAY_milliseconds(10);
     }
 }
 
 // Detects steps based on accelerometer data
 void detectStep(void) {
     AccelerometerData accel;
     accel.x = readAccelerometerAxis(REG_DATA_X0);
     accel.y = readAccelerometerAxis(REG_DATA_Y0);
     accel.z = readAccelerometerAxis(REG_DATA_Z0);
 
     float ax = accel.x * 4.0f;
     float ay = accel.y * 4.0f;
     float az = accel.z * 4.0f;
     float magnitude = sqrtf(ax * ax + ay * ay + az * az);
     float dynamicForce = fabsf(magnitude - GRAVITY_BASELINE);
     bool exceedsThreshold = (dynamicForce > STEP_THRESHOLD);
 
     isMovementActive = exceedsThreshold;
 
     if (exceedsThreshold && !wasStepThresholdExceeded) {
         totalSteps++;
         stepsPerSecond[currentSecondIndex]++;
         printf("Step detected! Total=%u\n", totalSteps);
     }
 
     wasStepThresholdExceeded = exceedsThreshold;
 }
 
 // Displays the current step pace on the OLED
 void displayStepPace(void) {
     static char previousText[6] = "";
     char currentText[6];
 
     if (displayedStepPace <= 0.5f) {
         if (previousText[0] != '\0') {
             oledC_DrawString(25, 2, 1, 1, (uint8_t *)previousText, OLEDC_COLOR_BLACK);
             previousText[0] = '\0';
         }
         return;
     }
 
     sprintf(currentText, "%u", (uint16_t)(displayedStepPace + 0.5f));
 
     if (strcmp(previousText, currentText) != 0) {
         oledC_DrawString(25, 2, 1, 1, (uint8_t *)previousText, OLEDC_COLOR_BLACK);
         oledC_DrawString(25, 2, 1, 1, (uint8_t *)currentText, OLEDC_COLOR_WHITE);
         strcpy(previousText, currentText);
     }
 }
 
 // Converts a number to a two-digit string
 static void formatTwoDigits(uint8_t value, char *buffer) {
     buffer[0] = (value / 10) + '0';
     buffer[1] = (value % 10) + '0';
     buffer[2] = '\0';
 }
 
 // Updates the date when a day overflows
 void advanceDate(ClockTime *time) {
     time->day++;
     uint8_t maxDays = DAYS_PER_MONTH[time->month - 1];
     if (time->day > maxDays) {
         time->day = 1;
         time->month++;
         if (time->month > 12)
             time->month = 1;
     }
 }
 
 // Increments the system clock time
 void incrementSystemTime(ClockTime *time) {
     time->seconds++;
     if (time->seconds >= 60) {
         time->seconds = 0;
         time->minutes++;
     }
     if (time->minutes >= 60) {
         time->minutes = 0;
         time->hours++;
     }
     if (time->hours >= 24) {
         time->hours = 0;
         advanceDate(time);
     }
 }
 
 // Renders the clock display on the OLED
 void renderClockDisplay(ClockTime *time) {
     static uint8_t lastHours = 255, lastMinutes = 255, lastSeconds = 255;
     static bool lastWas12Hour = false, lastIsPM = false;
     static uint8_t lastDay = 255, lastMonth = 255;
     char buffer[3];
     bool isPM = false;
     uint8_t displayHours = time->hours;
 
     if (redrawClock) {
         lastHours = lastMinutes = lastSeconds = 255;
         lastDay = lastMonth = 255;
         lastWas12Hour = !use12HourFormat;
         lastIsPM = true;
         redrawClock = false;
     }
 
     if (use12HourFormat) {
         if (displayHours == 0)
             displayHours = 12;
         else if (displayHours >= 12) {
             isPM = true;
             if (displayHours > 12)
                 displayHours -= 12;
         }
     }
 
     if (displayHours != lastHours || lastWas12Hour != use12HourFormat) {
         oledC_DrawRectangle(8, 45, 32, 61, OLEDC_COLOR_BLACK);
         formatTwoDigits(displayHours, buffer);
         oledC_DrawString(8, 45, 2, 2, (uint8_t *)buffer, OLEDC_COLOR_WHITE);
         oledC_DrawString(32, 45, 2, 2, (uint8_t *)":", OLEDC_COLOR_WHITE);
         lastHours = displayHours;
     }
 
     if (time->minutes != lastMinutes || lastWas12Hour != use12HourFormat) {
         oledC_DrawRectangle(40, 45, 64, 61, OLEDC_COLOR_BLACK);
         formatTwoDigits(time->minutes, buffer);
         oledC_DrawString(40, 45, 2, 2, (uint8_t *)buffer, OLEDC_COLOR_WHITE);
         oledC_DrawString(64, 45, 2, 2, (uint8_t *)":", OLEDC_COLOR_WHITE);
         lastMinutes = time->minutes;
     }
 
     if (time->seconds != lastSeconds) {
         oledC_DrawRectangle(72, 45, 96, 61, OLEDC_COLOR_BLACK);
         formatTwoDigits(time->seconds, buffer);
         oledC_DrawString(72, 45, 2, 2, (uint8_t *)buffer, OLEDC_COLOR_WHITE);
         lastSeconds = time->seconds;
     }
 
     if (use12HourFormat && (isPM != lastIsPM || lastWas12Hour != use12HourFormat || lastHours == 255)) {
         oledC_DrawRectangle(0, 85, 20, 93, OLEDC_COLOR_BLACK);
         oledC_DrawString(0, 85, 1, 1, (uint8_t *)(isPM ? "PM" : "AM"), OLEDC_COLOR_WHITE);
         lastIsPM = isPM;
     } else if (!use12HourFormat && lastWas12Hour) {
         oledC_DrawRectangle(0, 85, 20, 93, OLEDC_COLOR_BLACK);
     }
 
     if (time->day != lastDay || time->month != lastMonth || lastHours == 255) {
         oledC_DrawRectangle(65, 85, 95, 93, OLEDC_COLOR_BLACK);
         formatTwoDigits(time->day, buffer);
         oledC_DrawString(65, 85, 1, 1, (uint8_t *)buffer, OLEDC_COLOR_WHITE);
         oledC_DrawString(77, 85, 1, 1, (uint8_t *)"/", OLEDC_COLOR_WHITE);
         formatTwoDigits(time->month, buffer);
         oledC_DrawString(83, 85, 1, 1, (uint8_t *)buffer, OLEDC_COLOR_WHITE);
         lastDay = time->day;
         lastMonth = time->month;
     }
 
     lastWas12Hour = use12HourFormat;
 }
 
 // Draws the foot icon animation based on step activity
 void renderFootIcon(uint8_t x, uint8_t y, const uint16_t *bitmap, uint8_t width, uint8_t height) {
     for (uint8_t row = 0; row < height; row++)
         for (uint8_t col = 0; col < width; col++)
             if (bitmap[row] & (1 << (width - 1 - col)))
                 oledC_DrawPoint(x + col, y + row, OLEDC_COLOR_WHITE);
 }
 
 // Manages the time format selection menu
 void manageTimeFormatSelection(void) {
     inTimeFormatMenu = true;
     timeFormatOption = (use12HourFormat ? 0 : 1);
 
     renderTimeFormatMenu();
 
     while (inTimeFormatMenu) {
         bool button1Pressed = (PORTAbits.RA11 == 0);
         bool button2Pressed = (PORTAbits.RA12 == 0);
 
         if (button2Pressed) {
             while (PORTAbits.RA12 == 0) DELAY_milliseconds(10);
             timeFormatOption = (timeFormatOption + 1) % 2;
             renderTimeFormatMenu();
             DELAY_milliseconds(50);
         } else if (button1Pressed) {
             while (PORTAbits.RA11 == 0) DELAY_milliseconds(10);
             use12HourFormat = (timeFormatOption == 0);
             inTimeFormatMenu = false;
             DELAY_milliseconds(50);
             break;
         }
         DELAY_milliseconds(20);
     }
 }
 
 // Renders the time format selection menu
 void renderTimeFormatMenu(void) {
     oledC_clearScreen();
     oledC_DrawString(10, 5, 1, 1, (uint8_t *)"Format:", OLEDC_COLOR_WHITE);
     oledC_DrawString(10, 25, 1, 1, (uint8_t *)"12H", OLEDC_COLOR_WHITE);
     oledC_DrawString(10, 40, 1, 1, (uint8_t *)"24H", OLEDC_COLOR_WHITE);
     oledC_DrawString(4, (timeFormatOption == 0 ? 25 : 40), 1, 1, (uint8_t *)">", OLEDC_COLOR_WHITE);
 }
 
 // Renders the base layout for the set time menu
 void renderTimeSetMenu(void) {
     oledC_clearScreen();
     oledC_DrawRectangle(30, 2, 115, 10, OLEDC_COLOR_BLACK);
     oledC_DrawString(6, 10, 2, 2, (uint8_t *)"Set Time", OLEDC_COLOR_WHITE);
 
     if (timeFieldSelected == 0) {
         oledC_DrawRectangle(8, 40, 44, 64, OLEDC_COLOR_WHITE);
         oledC_DrawRectangle(10, 42, 42, 62, OLEDC_COLOR_BLACK);
         oledC_DrawRectangle(50, 40, 86, 64, OLEDC_COLOR_BLACK);
         oledC_DrawRectangle(52, 42, 84, 62, OLEDC_COLOR_BLACK);
     } else {
         oledC_DrawRectangle(8, 40, 44, 64, OLEDC_COLOR_BLACK);
         oledC_DrawRectangle(10, 42, 42, 62, OLEDC_COLOR_BLACK);
         oledC_DrawRectangle(50, 40, 86, 64, OLEDC_COLOR_WHITE);
         oledC_DrawRectangle(52, 42, 84, 62, OLEDC_COLOR_BLACK);
     }
 
     displayTimeSetValues();
 }
 
 // Displays the current values in the set time menu
 void displayTimeSetValues(void) {
     char buffer[3];
     oledC_DrawRectangle(15, 46, 43, 62, OLEDC_COLOR_BLACK);
     sprintf(buffer, "%02d", timeToSet.hours);
     oledC_DrawString(15, 46, 2, 2, (uint8_t *)buffer, OLEDC_COLOR_WHITE);
     oledC_DrawRectangle(55, 46, 83, 62, OLEDC_COLOR_BLACK);
     sprintf(buffer, "%02d", timeToSet.minutes);
     oledC_DrawString(55, 46, 2, 2, (uint8_t *)buffer, OLEDC_COLOR_WHITE);
 }
 
 // Processes user input for setting the time
 void processTimeSetInput(void) {
     bool button1Pressed = (PORTAbits.RA11 == 0);
     bool button2Pressed = (PORTAbits.RA12 == 0);
 
     if (button1Pressed && button2Pressed) {
         while (PORTAbits.RA11 == 0 && PORTAbits.RA12 == 0) DELAY_milliseconds(10);
         timeFieldSelected = !timeFieldSelected;
         renderTimeSetMenu();
         DELAY_milliseconds(50);
     } else if (button1Pressed) {
         while (PORTAbits.RA11 == 0) DELAY_milliseconds(10);
         if (timeFieldSelected == 0)
             timeToSet.hours = (timeToSet.hours + 1) % 24;
         else
             timeToSet.minutes = (timeToSet.minutes + 1) % 60;
         displayTimeSetValues();
         DELAY_milliseconds(50);
     } else if (button2Pressed) {
         while (PORTAbits.RA12 == 0) DELAY_milliseconds(10);
         if (timeFieldSelected == 0)
             timeToSet.hours = (timeToSet.hours == 0) ? 23 : timeToSet.hours - 1;
         else
             timeToSet.minutes = (timeToSet.minutes == 0) ? 59 : timeToSet.minutes - 1;
         displayTimeSetValues();
         DELAY_milliseconds(50);
     }
 }
 
 // Checks if the device is tilted to save settings
 bool checkTiltToSave(void) {
     AccelerometerData accel;
     accel.x = readAccelerometerAxis(REG_DATA_X0);
     accel.y = readAccelerometerAxis(REG_DATA_Y0);
     accel.z = readAccelerometerAxis(REG_DATA_Z0);
 
     const float TILT_THRESHOLD = 600.0f;
     float ax = accel.x * 4.0f;
     float ay = accel.y * 4.0f;
     float az = accel.z * 4.0f;
     float magnitude = sqrtf(ax * ax + ay * ay + az * az);
 
     return (magnitude < TILT_THRESHOLD);
 }
 
 // Manages the time setting page
 void manageTimeSetPage(void) {
     inTimeSetMenu = true;
     timeToSet.hours = systemClock.hours;
     timeToSet.minutes = systemClock.minutes;
     timeFieldSelected = 0;
 
     renderTimeSetMenu();
 
     while (PORTAbits.RA11 == 0 || PORTAbits.RA12 == 0) DELAY_milliseconds(10);
 
     int tiltCount = 0;
     while (inTimeSetMenu) {
         processTimeSetInput();
         if (checkTiltToSave()) {
             tiltCount++;
             if (tiltCount >= 1) {
                 systemClock.hours = timeToSet.hours;
                 systemClock.minutes = timeToSet.minutes;
                 systemClock.seconds = 0;
                 inTimeSetMenu = false;
                 break;
             }
         } else {
             tiltCount = 0;
         }
         DELAY_milliseconds(20);
     }
 }
 
 // Renders the base layout for the set date menu
 void renderDateSetMenu(void) {
     oledC_clearScreen();
     oledC_DrawRectangle(30, 2, 115, 10, OLEDC_COLOR_BLACK);
     oledC_DrawString(6, 10, 2, 2, (uint8_t *)"Set Date", OLEDC_COLOR_WHITE);
 
     if (dateFieldSelected == 0) {
         oledC_DrawRectangle(8, 40, 44, 64, OLEDC_COLOR_WHITE);
         oledC_DrawRectangle(10, 42, 42, 62, OLEDC_COLOR_BLACK);
         oledC_DrawRectangle(50, 40, 86, 64, OLEDC_COLOR_BLACK);
         oledC_DrawRectangle(52, 42, 84, 62, OLEDC_COLOR_BLACK);
     } else {
         oledC_DrawRectangle(8, 40, 44, 64, OLEDC_COLOR_BLACK);
         oledC_DrawRectangle(10, 42, 42, 62, OLEDC_COLOR_BLACK);
         oledC_DrawRectangle(50, 40, 86, 64, OLEDC_COLOR_WHITE);
         oledC_DrawRectangle(52, 42, 84, 62, OLEDC_COLOR_BLACK);
     }
 
     displayDateSetValues();
 }
 
 // Displays the current values in the set date menu
 void displayDateSetValues(void) {
     char buffer[3];
     oledC_DrawRectangle(15, 46, 43, 62, OLEDC_COLOR_BLACK);
     sprintf(buffer, "%02d", dateToSet.day);
     oledC_DrawString(15, 46, 2, 2, (uint8_t *)buffer, OLEDC_COLOR_WHITE);
     oledC_DrawRectangle(55, 46, 83, 62, OLEDC_COLOR_BLACK);
     sprintf(buffer, "%02d", dateToSet.month);
     oledC_DrawString(55, 46, 2, 2, (uint8_t *)buffer, OLEDC_COLOR_WHITE);
 }
 
 // Processes user input for setting the date
 void processDateSetInput(void) {
     bool button1Pressed = (PORTAbits.RA11 == 0);
     bool button2Pressed = (PORTAbits.RA12 == 0);
 
     if (button1Pressed && button2Pressed) {
         while (PORTAbits.RA11 == 0 && PORTAbits.RA12 == 0) DELAY_milliseconds(10);
         dateFieldSelected = !dateFieldSelected;
         renderDateSetMenu();
         DELAY_milliseconds(50);
     } else if (button1Pressed) {
         while (PORTAbits.RA11 == 0) DELAY_milliseconds(10);
         if (dateFieldSelected == 0) {
             uint8_t maxDay = DAYS_PER_MONTH[dateToSet.month - 1];
             dateToSet.day = (dateToSet.day % maxDay) + 1;
         } else {
             dateToSet.month = (dateToSet.month % 12) + 1;
             uint8_t maxDay = DAYS_PER_MONTH[dateToSet.month - 1];
             if (dateToSet.day > maxDay)
                 dateToSet.day = maxDay;
         }
         displayDateSetValues();
         DELAY_milliseconds(50);
     } else if (button2Pressed) {
         while (PORTAbits.RA12 == 0) DELAY_milliseconds(10);
         if (dateFieldSelected == 0) {
             if (dateToSet.day == 1)
                 dateToSet.day = DAYS_PER_MONTH[dateToSet.month - 1];
             else
                 dateToSet.day--;
         } else {
             if (dateToSet.month == 1)
                 dateToSet.month = 12;
             else
                 dateToSet.month--;
             uint8_t maxDay = DAYS_PER_MONTH[dateToSet.month - 1];
             if (dateToSet.day > maxDay)
                 dateToSet.day = maxDay;
         }
         displayDateSetValues();
         DELAY_milliseconds(50);
     }
 }
 
 // Manages the date setting page
 void manageDateSetPage(void) {
     inTimeSetMenu = true;
     dateToSet.day = systemClock.day;
     dateToSet.month = systemClock.month;
     dateFieldSelected = 0;
 
     renderDateSetMenu();
 
     while (PORTAbits.RA11 == 0 || PORTAbits.RA12 == 0) DELAY_milliseconds(10);
 
     int tiltCount = 0;
     while (inTimeSetMenu) {
         processDateSetInput();
         if (checkTiltToSave()) {
             tiltCount++;
             if (tiltCount >= 1) {
                 systemClock.day = dateToSet.day;
                 systemClock.month = dateToSet.month;
                 inTimeSetMenu = false;
                 break;
             }
         } else {
             tiltCount = 0;
         }
         DELAY_milliseconds(20);
     }
 }
 
 // Displays the step rate graph
 void displayStepGraph(void) {
     isGraphDisplayed = true;
     bool graphModeActive = true;
 
     oledC_clearScreen();
 
     int xLeft = 5;
     int xRight = GRAPH_WIDTH;
     int baselineY = GRAPH_HEIGHT - 10;
     int topY = 10;
 
     int stepLevels[] = {30, 60, 100};
     for (int i = 0; i < 3; i++) {
         int value = stepLevels[i];
         int yPosition = baselineY - ((value * (baselineY - topY)) / 100);
         for (int x = xLeft; x <= xRight; x += 3)
             oledC_DrawPoint(x, yPosition, OLEDC_COLOR_GHOSTWHITE);
 
         char label[4];
         sprintf(label, "%d", value);
         oledC_DrawString(0, yPosition - 10, 1, 1, (uint8_t *)label, OLEDC_COLOR_WHITE);
     }
 
     for (int i = 0; i <= 9; i++) {
         int xTick = xLeft + (i * (xRight - xLeft) / 9);
         oledC_DrawRectangle(xTick, baselineY - 2, xTick + 2, baselineY, OLEDC_COLOR_GHOSTWHITE);
     }
 
     int previousX = xLeft;
     int previousY = baselineY - ((stepRateHistory[0] * (baselineY - topY)) / 100);
     for (int i = 1; i < GRAPH_WIDTH; i++) {
         int currentX = xLeft + (i * (xRight - xLeft) / (GRAPH_WIDTH - 1));
         int currentY = baselineY - ((stepRateHistory[i] * (baselineY - topY)) / 100);
         if (stepRateHistory[i] > 0 || stepRateHistory[i - 1] > 0)
             oledC_DrawLine(previousX, previousY, currentX, currentY, 1, OLEDC_COLOR_BLUE);
         previousX = currentX;
         previousY = currentY;
     }
 
     static uint8_t button1HoldCount = 0;
     while (graphModeActive) {
         bool button1Pressed = (PORTAbits.RA11 == 0);
         bool button2Pressed = (PORTAbits.RA12 == 0);
 
         if (button2Pressed) {
             while (PORTAbits.RA12 == 0) DELAY_milliseconds(10);
             graphModeActive = false;
             inMainMenu = true;
             renderMainMenu();
             updateMenuTimeDisplay();
             DELAY_milliseconds(50);
             break;
         } else if (button1Pressed) {
             button1HoldCount++;
             if (button1HoldCount >= 20) {
                 oledC_clearScreen();
                 graphModeActive = false;
                 inMainMenu = false;
                 redrawClock = true;
                 DELAY_milliseconds(50);
                 break;
             }
             DELAY_milliseconds(10);
         } else {
             button1HoldCount = 0;
         }
         DELAY_milliseconds(20);
     }
 
     isGraphDisplayed = false;
 }
 
 // Menu System
 static const char *MENU_OPTIONS[MENU_ITEM_COUNT] = {
     "PedometerGraph", "12H/24H", "Set Time", "Set Date", "Exit"
 };
 static uint8_t currentMenuSelection = 0;
 
 // Renders the main menu
 void renderMainMenu(void) {
     oledC_clearScreen();
     for (uint8_t i = 0; i < MENU_ITEM_COUNT; i++) {
         uint8_t yPosition = 20 + (i * 12);
         oledC_DrawString(10, yPosition, 1, 1, (uint8_t *)MENU_OPTIONS[i], OLEDC_COLOR_WHITE);
         if (i == currentMenuSelection)
             oledC_DrawString(4, yPosition, 1, 1, (uint8_t *)">", OLEDC_COLOR_WHITE);
     }
     updateMenuTimeDisplay();
     if (use12HourFormat) {
         uint8_t displayHours = systemClock.hours;
         bool isPM = (displayHours >= 12);
         oledC_DrawString(0, 80, 1, 1, (uint8_t *)(isPM ? "PM" : "AM"), OLEDC_COLOR_WHITE);
     }
 }
 
 // Updates the time display in the menu
 void updateMenuTimeDisplay(void) {
     static char previousTime[9] = "";
     char currentTimeStr[9], buffer[3];
     uint8_t displayHours = systemClock.hours;
 
     if (use12HourFormat) {
         if (displayHours >= 12) {
             if (displayHours > 12) displayHours -= 12;
         } else if (displayHours == 0) displayHours = 12;
     }
 
     formatTwoDigits(displayHours, buffer);
     sprintf(currentTimeStr, "%s:", buffer);
     formatTwoDigits(systemClock.minutes, buffer);
     strcat(currentTimeStr, buffer);
     strcat(currentTimeStr, ":");
     formatTwoDigits(systemClock.seconds, buffer);
     strcat(currentTimeStr, buffer);
 
     if (strcmp(previousTime, currentTimeStr) != 0) {
         oledC_DrawRectangle(48, 80, 115, 88, OLEDC_COLOR_BLACK);
         oledC_DrawString(48, 80, 1, 1, (uint8_t *)currentTimeStr, OLEDC_COLOR_WHITE);
         strcpy(previousTime, currentTimeStr);
     }
 }
 
 // Executes the selected menu action
 void executeMenuSelection(void) {
     switch (currentMenuSelection) {
         case 0: displayStepGraph(); break;
         case 1: manageTimeFormatSelection(); renderMainMenu(); updateMenuTimeDisplay(); break;
         case 2: manageTimeSetPage(); renderMainMenu(); updateMenuTimeDisplay(); break;
         case 3: manageDateSetPage(); renderMainMenu(); updateMenuTimeDisplay(); break;
         case 4: inMainMenu = false; redrawClock = true; oledC_clearScreen(); break;
         default: break;
     }
 }
 
 // Initializes the timer for periodic updates
 void initializeTimer(void) {
     TMR1 = 0;
     PR1 = 15625;
     T1CONbits.TCKPS = 3;
     T1CONbits.TCS = 0;
     T1CONbits.TGATE = 0;
     T1CONbits.TON = 1;
 }
 
 // Configures the Timer1 interrupt
 void configureTimerInterrupt(void) {
     IPC0bits.T1IP = 5;
     IFS0bits.T1IF = 0;
     IEC0bits.T1IE = 1;
 }
 
 // Initializes user hardware settings
 void initializeHardware(void) {
     TRISA &= ~(1 << 8 | 1 << 9); // LEDs as outputs
     TRISA |= (1 << 11 | 1 << 12); // Buttons as inputs
     TRISB |= (1 << 12);
     ANSB = 0;
     AD1CON1 = 0;
     AD1CON2 = 0;
     AD1CON3 = 0b001000011111111;
     AD1CHS = 0;
     AD1CHS |= (1 << 3);
     AD1CON1 |= (1 << 15);
 
     BUTTON1_TRIS = 1;
     BUTTON2_TRIS = 1;
     LED1_TRIS = 0;
     LED2_TRIS = 0;
     LED1_PORT = 0;
     LED2_PORT = 0;
 }
 
 // Timer1 interrupt handler for timekeeping and step updates
 void __attribute__((__interrupt__, auto_psv)) _T1Interrupt(void) {
     incrementSystemTime(&systemClock);
     elapsedSeconds++;
     showFootIcon = !showFootIcon;
 
     if (!isGraphDisplayed) {
         static uint8_t button1HoldCount = 0;
         bool button1Pressed = (PORTAbits.RA11 == 0);
         if (button1Pressed) {
             button1HoldCount++;
             if (button1HoldCount >= 2 && !inMainMenu) {
                 inMainMenu = true;
                 currentMenuSelection = 0;
                 renderMainMenu();
                 button1HoldCount = 0;
             }
             DELAY_milliseconds(10);
         } else {
             button1HoldCount = 0;
         }
     }
 
     if (!inMainMenu) {
         currentSecondIndex = (currentSecondIndex + 1) % HISTORY_SIZE;
         stepsPerSecond[currentSecondIndex] = 0;
 
         static uint16_t previousStepCount = 0;
         static uint8_t inactivityCount = 0;
 
         uint16_t stepsThisSecond = totalSteps - previousStepCount;
         previousStepCount = totalSteps;
 
         float rawPace = (float)stepsThisSecond * 60.0f;
 
         if (stepsThisSecond == 0) {
             inactivityCount++;
             if (inactivityCount >= 3) rawPace = 0.0f;
         } else {
             inactivityCount = 0;
         }
 
         currentStepPace = rawPace;
     }
 
     IFS0bits.T1IF = 0;
 }
 
 // Main application entry point
 int main(void) {
     int status;
     uint8_t deviceId = 0;
     SYSTEM_Initialize();
     initializeHardware();
     oledC_setBackground(OLEDC_COLOR_BLACK);
     oledC_clearScreen();
     i2c1_open();
 
     for (int i = 0; i < 3; i++) {
         status = i2cReadSlaveRegister(ACCEL_WRITE_ADDR, 0x00, &deviceId);
         if (status == OK && deviceId == 0xE5) break;
         if (i == 2) haltWithError("I2C Error or Wrong Device ID");
         DELAY_milliseconds(10);
     }
     initializeAccelerometer();
     initializeTimer();
     configureTimerInterrupt();
 
     static bool wasInMenu = false;
     LED1_PORT = 0;
     LED2_PORT = 0;
 
     while (1) {
         bool button1Pressed = (PORTAbits.RA11 == 0);
         bool button2Pressed = (PORTAbits.RA12 == 0);
 
         if (inMainMenu) {
             static bool button1WasPressed = false;
             static bool button2WasPressed = false;
             static uint8_t comboPressCount = 0;
 
             if (justEnteredMenu) {
                 justEnteredMenu = false;
                 button1WasPressed = false;
                 button2WasPressed = false;
                 renderMainMenu();
             } else {
                 if (button1Pressed && button2Pressed) {
                     comboPressCount++;
                     if (comboPressCount >= 3) {
                         LED1_PORT = 1;
                         LED2_PORT = 1;
                         while (PORTAbits.RA11 == 0 && PORTAbits.RA12 == 0) DELAY_milliseconds(10);
                         LED1_PORT = 0;
                         LED2_PORT = 0;
                         executeMenuSelection();
                         DELAY_milliseconds(50);
                         comboPressCount = 0;
                         button1WasPressed = true;
                         button2WasPressed = true;
                     }
                 } else {
                     comboPressCount = 0;
                     if (button1Pressed && !button1WasPressed) {
                         LED1_PORT = 1;
                         while (PORTAbits.RA11 == 0) DELAY_milliseconds(10);
                         LED1_PORT = 0;
                         if (currentMenuSelection == 0)
                             currentMenuSelection = MENU_ITEM_COUNT - 1;
                         else
                             currentMenuSelection--;
                         renderMainMenu();
                         DELAY_milliseconds(50);
                         button1WasPressed = true;
                     } else if (!button1Pressed) {
                         button1WasPressed = false;
                     }
                     if (button2Pressed && !button2WasPressed) {
                         LED2_PORT = 1;
                         while (PORTAbits.RA12 == 0) DELAY_milliseconds(10);
                         LED2_PORT = 0;
                         if (currentMenuSelection == MENU_ITEM_COUNT - 1)
                             currentMenuSelection = 0;
                         else
                             currentMenuSelection++;
                         renderMainMenu();
                         DELAY_milliseconds(50);
                         button2WasPressed = true;
                     } else if (!button2Pressed) {
                         button2WasPressed = false;
                     }
                 }
                 if (!button1Pressed && !button2Pressed) {
                     LED1_PORT = 0;
                     LED2_PORT = 0;
                 }
                 updateMenuTimeDisplay();
             }
             wasInMenu = true;
         } else {
             LED1_PORT = button1Pressed ? 1 : 0;
             LED2_PORT = button2Pressed ? 1 : 0;
 
             if (wasInMenu) {
                 oledC_DrawRectangle(0, 80, 115, 88, OLEDC_COLOR_BLACK);
                 wasInMenu = false;
             }
 
             detectStep();
 
             float currentDisplayPace = displayedStepPace;
             float rawPace = currentStepPace;
 
             if (currentDisplayPace < rawPace) {
                 displayedStepPace += 2.0f;
                 if (displayedStepPace > rawPace) displayedStepPace = rawPace;
             } else if (currentDisplayPace > rawPace) {
                 displayedStepPace -= 2.0f;
                 if (displayedStepPace < rawPace) displayedStepPace = rawPace;
             }
 
             if (displayedStepPace < 0.5f) displayedStepPace = 0.0f;
             else if (displayedStepPace > 100.0f) displayedStepPace = 100.0f;
 
             stepRateHistory[elapsedSeconds % GRAPH_WIDTH] = (uint8_t)displayedStepPace;
 
             displayStepPace();
             renderClockDisplay(&systemClock);
             oledC_DrawRectangle(0, 0, 15, 15, OLEDC_COLOR_BLACK);
             if (displayedStepPace > 0)
                 renderFootIcon(0, 0, showFootIcon ? FOOT_ICON_1 : FOOT_ICON_2, 16, 16);
         }
 
         DELAY_milliseconds(20);
     }
 
     return 0;
 }