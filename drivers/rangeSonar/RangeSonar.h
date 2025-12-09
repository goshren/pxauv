/************************************************************************************
					文件名：RangeSonar.h
					最后一次修改时间：2025/6/29
					修改内容：
*************************************************************************************/

#ifndef __RANGE_H__
#define __RANGE_H__

/************************************************************************************
 									包含头文件
*************************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>


/************************************************************************************
 									宏定义
*************************************************************************************/
#define TCP_RANGESONAR_IP   					  "192.168.2.253"    
#define TCP_RANGESONAR_PORT 				(unsigned short)1031


/************************************************************************************
 									数据类型
*************************************************************************************/
/*	测距声呐数据结构体	*/
typedef struct {
	int distanceFirstObstacle;				//第一个障碍物的距离,单位cm
}rangeSonarDataPack_t;

/*	传感器的读取数据协议	*/
 typedef struct {
    int timeout_ms;										//读取超时
	int length;													//数据长度
	unsigned char head_marker[4];	  //固定帧头 
}rangeSonarDataProtocol_t;

/************************************************************************************
 									函数原型
*************************************************************************************/
/*	外界获取文件描述符	*/
int RangeSonar_getFD(void);

/*	初始化	*/
int RangeSonar_Init(void);

/*	发送上电配置信息	*/
int RangeSonar_SendInitConfig(void);

/*	数据采集与解析	*/
int RangeSonar_ReadRawData(void);
int RangeSonar_ParseData(void);

/*	数据获取*/
int RangeSonar_getDistanceFirstObstacleValue(void);

/*	数据打包/打印*/
char *RangeSonar_DataPackageProcessing(void);
void RangeSonar_PrintSonarData(void);

/*	关闭测距	*/
int RangeSonar_Close(void);

#endif

