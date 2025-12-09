/************************************************************************************
				文件名：CTD.h
				最后一次修改时间：2025/6/27
				修改内容：
************************************************************************************/
 
#ifndef __CTD_H__
#define __CTD_H__
 
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
	double temperature;				//存放温度字符串型数据，单位℃
	double conductivity;				 //存放电导率数据，单位mS/cm
	double pressure;						 //存放压力数据，单位dbar
	double depth;							//存放深度数据，单位m
	double salinity;							//存放盐度数据，单位PSU
	double soundVelocity;		   //存放声速数据，单位m/s
	double density;							//存放密度数据，单位kg/m³
}ctdDataPack_t;

/*	传感器的读取数据协议	*/
 typedef struct {
    int timeout_ms;							//读取超时
	int length;										//数据长度
    char head_marker;					//数据头
    char tail_marker;						//数据尾
} ctdDataProtocol_t;
 
 
/************************************************************************************
 									函数原型
*************************************************************************************/ 
/*	外界获取文件描述符	*/
int CTD_getFD(void);

/*	初始化	*/
int CTD_Init_default(void);
int CTD_Init(void);

/*	关闭传感器	*/
int CTD_Close_default(void);
int CTD_Close(void);

/*	读取原始数据	*/
int CTD_ReadRawData(void);

/*	解析数据	*/
int CTD_ParseData(void);

/*	获取数据	*/
double CTD_getTemperatureValue(void);
double CTD_getPressureValue(void);
double CTD_getConductivityValue(void);
double CTD_getDepthValue(void);
double CTD_getSalinityValue(void);
double CTD_getSoundVelocityValue(void);
double CTD_getDensityValue(void);

/*	打印数值	*/
void CTD_PrintAllData(void);

/*	数据打包	*/
char *CTD_DataPackageProcessing(void);

#endif


