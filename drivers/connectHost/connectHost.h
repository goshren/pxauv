/************************************************************************************
					文件名：TCP.h
					最后一次修改时间：2025/2/28
					修改内容：wending
 ************************************************************************************/


#ifndef __SERVER_TCP_H__
#define __SERVER_TCP_H__

/************************************************************************************
 									包含头文件
 ************************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>


/************************************************************************************
 									宏定义
*************************************************************************************/
#define TCP_SERVER_IP   				"192.168.2.1"    
#define TCP_SERVER_PORT 				(unsigned short)6666
#define MAX_TCP_SEND_DATA_SIZE 			255
#define MAX_TCP_RECV_DATA_SIZE 			255


/************************************************************************************
 									函数原型
*************************************************************************************/

int ConnectHost_Init(void);

void *ConnectHost_Thread_TcpServer(void);

void *ConnectHost_Thread_TcpServer1(void);

#endif
