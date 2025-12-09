/************************************************************************************
				文件名：DVL.h
				最后一次修改时间：2025/6/27
				修改内容：
************************************************************************************/
 
#ifndef __DVL_H__
#define __DVL_H__
 
/************************************************************************************
								包含头文件
*************************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <termios.h> 
#include <pthread.h>
#include <math.h>
#include <time.h>
 

/************************************************************************************
								数据类型
*************************************************************************************/
/* 保存采集的数据 */
typedef struct {
	float pitch;				                                  //俯仰，单位°
	float roll;				                                        //横滚，单位°
	float heading;						                    //艏向，单位°
	float transducerEntryDepth;				//换能器入水深度，单位m
	float speedX;							                //底跟踪，X轴速度，船头为正，单位mm/s
	float speedY;		                                    //底跟踪，Y轴速度，右舷为正，单位mm/s
	float speedZ;							                //底跟踪，Z轴速度，向下为正，单位mm/s
    float buttomDistance;				          //设备离底距离 高度，单位m
}dvlDataPack_t;

/*	传感器的读取数据协议	*/
 typedef struct {
    int timeout_ms;							//读取超时
	int length;										//数据长度
} dvlDataProtocol_t;
 
 
/************************************************************************************
 									函数原型
*************************************************************************************/ 
int DVL_getFD(void);

/*	初始化	*/
int DVL_Init_default(void);
int DVL_Init(void);

/*	关闭传感器	*/
int DVL_Close_default(void);
int DVL_Close(void);

/*	发送指令	*/
int DVL_SendCmd_OpenDVLDevice(void);
int DVL_SendCmd_SetDVLSendFreq(void);

/*	读取原始数据	*/
ssize_t DVL_ReadRawData(void);

/*	解析数据	*/
int DVL_ParseData(void);

/*	获取数据	*/
float DVL_getPitchValue(void);
float DVL_getRollValue(void);
float DVL_getHeadingValue(void);
float DVL_getSpeedXValue(void);
float DVL_getSpeedYValue(void);
float DVL_getSpeedZValue(void);
float DVL_getTransducerEntryDepthValue(void);
float DVL_getButtomDistanceValue(void);

/*	打印数值	*/
void DVL_PrintAllData(void);

/*	数据打包	*/
char *DVL_DataPackageProcessing(void);

#endif


