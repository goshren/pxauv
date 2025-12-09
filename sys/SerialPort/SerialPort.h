/************************************************************************************
					文件名：SerialPort.h
					最后一次修改时间：2025/6/25
					修改内容：
 ************************************************************************************/


#ifndef __SERIALPORT_H__
#define __SERIALPORT_H__


/************************************************************************************
					包含头文件
*************************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>


/************************************************************************************
					函数原型
*************************************************************************************/
int SerialPort_open(const char *serialportName, struct termios *opt);
int SerialPort_close(int fd, struct termios *opt);
int SerialPort_setBaudrate(int fd, int baudrate);
int SerialPort_setStopbit(int fd, int stopbit);
int SerialPort_setDatabits(int fd, int databits);
int SerialPort_setParity(int fd, char parity);
int SerialPort_setFlowControl(int fd, int enable);
int SerialPort_setDTR(int fd, int enable);
int SerialPort_configBaseParams(int fd, int baudrate, int stopbit, int databits, char parity);
void SerialPort_printConfig(int fd, const char *serialportName);


#endif

