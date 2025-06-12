/*
 * File:   Accel_i2c.h
 * Author: Amit
 *
 * Created on April 21, 2022
 */

typedef enum {OK, NACK, ACK, BAD_ADDR, BAD_REG} I2Cerror;

void i2c1_open(void);
I2Cerror i2cReadSlaveRegister(unsigned char devAddW, unsigned char regAdd, unsigned char *reg);
I2Cerror i2cWriteSlave(unsigned char devAddW, unsigned char regAdd, unsigned char data);
