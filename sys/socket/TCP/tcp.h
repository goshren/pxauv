/************************************************************************************
                    文件名：tcp.h
                    文件说明：tcp.c的头文件
                    最后一次修改时间：2025/4/15
*************************************************************************************/

#ifndef __TCP_H__
#define __TCP_H__

/************************************************************************************
                                        包含头文件
*************************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/types.h>          
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


/************************************************************************************
                                        宏定义
*************************************************************************************/
#define           TCP_MAX_LISTEN                         3       
#define           TCP_MAX_SEND_SIZE                1024
#define           TCP_MAX_RECV_SIZE                 1024


/************************************************************************************
                                        函数声明
*************************************************************************************/
int TCP_InitClient(char *ipaddr , unsigned short int port);
int TCP_InitServer(char *ipaddr , unsigned short int port);
int TCP_SendData(int tcpfd, const unsigned char *databuf, int datasize);
int TCP_RecvData(int tcpfd, unsigned char *tcpReadBuf, size_t bufsize, int nbyte,int timeout_ms);
int TCP_RecvData_Block(int tcpfd, unsigned char *tcpReadBuf, size_t bufsize, int nbyte);

#endif

