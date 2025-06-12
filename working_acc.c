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

// ---------------- ADXL345 Defines ----------------
#define WRITE_ADDRESS 0x3A // 8-bit write address for ADXL345
#define REG_POWER_CTL 0x2D
#define REG_DATAX0 0x32
#define REG_DATAY0 0x34
#define REG_DATAZ0 0x36
#define MEASURE_MODE 0x08

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t z;
} ACCEL_DATA_t;

// ---------------- Step Detection Globals ----------------
static bool wasAboveThreshold = false;
static bool movementDetected = false;
static uint16_t stepCount = 0;
static uint8_t inactivityCounter = 0;
const float baselineGravity = 1024.0f;

// ---------------- Error Handling ----------------
void errorStop(char *msg)
{
    oledC_DrawString(0, 20, 1, 1, (uint8_t *)msg, OLEDC_COLOR_DARKRED);
    printf("Error: %s\n", msg);
    for (;;)
        ;
}

// ---------------- Read Axis Data ----------------
int16_t readAxis(uint8_t regAddress)
{
    uint8_t lowByte, highByte;
    int retries = 3;

    for (int i = 0; i < retries; i++)
    {
        if (i2cReadSlaveRegister(WRITE_ADDRESS, regAddress, &lowByte) == OK)
            break;
        if (i == retries - 1)
            errorStop("I2C Read Error (LSB)");
        DELAY_milliseconds(10);
    }

    for (int i = 0; i < retries; i++)
    {
        if (i2cReadSlaveRegister(WRITE_ADDRESS, regAddress + 1, &highByte) == OK)
            break;
        if (i == retries - 1)
            errorStop("I2C Read Error (MSB)");
        DELAY_milliseconds(10);
    }

    return ((int16_t)highByte << 8) | lowByte;
}

// ---------------- Initialize ADXL345 ----------------
void initAccelerometer(void)
{
    I2Cerror err;
    uint8_t deviceId = 0;

    for (int i = 0; i < 3; i++)
    {
        err = i2cReadSlaveRegister(WRITE_ADDRESS, 0x00, &deviceId);
        if (err == OK && deviceId == 0xE5)
            break;
        if (i == 2)
            errorStop("I2C Error or Wrong Device ID");
        DELAY_milliseconds(10);
    }

    for (int i = 0; i < 3; i++)
    {
        err = i2cWriteSlave(WRITE_ADDRESS, REG_POWER_CTL, MEASURE_MODE);
        if (err == OK)
            break;
        if (i == 2)
            errorStop("Accel Power Error");
        DELAY_milliseconds(10);
    }

    for (int i = 0; i < 3; i++)
    {
        err = i2cWriteSlave(WRITE_ADDRESS, 0x31, 0x0B);
        if (err == OK)
            break;
        if (i == 2)
            errorStop("Accel Data Format Error");
        DELAY_milliseconds(10);
    }
}

// ---------------- Step Detection ----------------
void detectStep(void)
{
    ACCEL_DATA_t accel; // Declare accelerometer data structure

    accel.x = readAxis(0x32);
    accel.y = readAxis(0x34);
    accel.z = readAxis(0x36);

    float ax = accel.x * 4.0f;
    float ay = accel.y * 4.0f;
    float az = accel.z * 4.0f;
    float mag = sqrtf(ax * ax + ay * ay + az * az);

    float dynamic = fabsf(mag - baselineGravity);
    printf("mag=%.1f dynamic=%.1f\n", mag, dynamic);

    float threshold = 200.0f;
    bool above = (dynamic > threshold);
    movementDetected = above;

    if (above && !wasAboveThreshold)
    {
        stepCount++;
        printf("Step detected! Count=%u\n", stepCount);
    }
    wasAboveThreshold = above;
}

// ---------------- OLED Step Counter Display ----------------
void drawSteps(void)
{
    static char oldStr[6] = "";
    char newStr[6];
    sprintf(newStr, "%u", stepCount);

    if (strcmp(oldStr, newStr) != 0)
    {
        oledC_DrawString(80, 2, 1, 1, (uint8_t *)oldStr, OLEDC_COLOR_BLACK);
        oledC_DrawString(80, 2, 1, 1, (uint8_t *)newStr, OLEDC_COLOR_WHITE);
        strcpy(oldStr, newStr);
    }
}

// ---------------- Main Application ----------------
int main(void)
{
    int rc;
    uint8_t deviceId = 0;

    SYSTEM_Initialize();
    oledC_setBackground(OLEDC_COLOR_SKYBLUE);
    oledC_clearScreen();
    i2c1_open();

    // Verify accelerometer is detected
    for (int i = 0; i < 3; i++)
    {
        rc = i2cReadSlaveRegister(WRITE_ADDRESS, 0x00, &deviceId);
        if (rc == OK && deviceId == 0xE5)
            break;
        if (i == 2)
            errorStop("I2C Error or Wrong Device ID");
        DELAY_milliseconds(10);
    }

    oledC_DrawString(0, 0, 1, 1, (uint8_t *)"ADXL345", OLEDC_COLOR_BLACK);

    // Initialize Accelerometer
    initAccelerometer();

    for (;;)
    {
        detectStep();
        drawSteps();

        int16_t x = readAxis(REG_DATAX0);
        int16_t y = readAxis(REG_DATAY0);
        int16_t z = readAxis(REG_DATAZ0);

        // Display X, Y, Z values
        char buffer[32];

        sprintf(buffer, "X: %03d", abs(x) % 1000);
        oledC_DrawString(20, 20, 1, 1, (uint8_t *)buffer, OLEDC_COLOR_BLACK);

        sprintf(buffer, "Y: %03d", abs(y) % 1000);
        oledC_DrawString(20, 40, 1, 1, (uint8_t *)buffer, OLEDC_COLOR_BLACK);

        sprintf(buffer, "Z: %03d", abs(z) % 1000);
        oledC_DrawString(20, 60, 1, 1, (uint8_t *)buffer, OLEDC_COLOR_BLACK);

        // Check if the device is upside down
        if (z < 0)
        {
            oledC_DrawString(20, 80, 1, 1, (uint8_t *)"Upside Down!", OLEDC_COLOR_RED);
        }
        else
        {
            oledC_DrawString(20, 80, 1, 1, (uint8_t *)"Normal", OLEDC_COLOR_GREEN);
        }

        DELAY_milliseconds(500);
        oledC_DrawRectangle(20, 20, 96, 80, OLEDC_COLOR_SKYBLUE);
    }
}