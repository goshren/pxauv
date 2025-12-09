/************************************************************************************
					文件名：DTU.h
					最后一次修改时间：2025/2/28
					修改内容：wending
*************************************************************************************/


#ifndef __DTU_H__
#define __DTU_H__


/************************************************************************************
 									包含头文件
*************************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <termios.h> 
#include <pthread.h>

 
/************************************************************************************
 									宏定义
*************************************************************************************/
#define MAX_DTU_SEND_DATA_SIZE 256
#define MAX_DTU_RECV_DATA_SIZE 256


/************************************************************************************
 									函数原型
*************************************************************************************/
/*	外界获取文件描述符	*/
int DTU_getFD(void);

/*	初始化	*/
int DTU_Init(void);

/*	发送/接收数据	*/
ssize_t DTU_SendData(unsigned char *dtuSendBuf, size_t bufsize);
ssize_t DTU_RecvData(void);

/*	接收的数据进行解析	*/
void DTU_ParseData(void);

/*	关闭数传电台	*/
int DTU_Close(void);

#endif

