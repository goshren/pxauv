/************************************************************************************
				文件名：GPS.h
				最后一次修改时间：2024/11/12
				修改内容：
************************************************************************************/


#ifndef __GPS_H__
#define __GPS_H__


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
typedef struct {
    char systemFlag[8];             //存放系统标识符
    char longitudeDirection;        //存放经度方向，‘E’：东经，‘W’：西经
    char latitudeDirection;         //存放纬度方向，‘N’：北纬，‘S’：南纬
    char satelliteNum;              //存放连接的定位卫星数：0-24
    char isValid;                   //存放定位是否有效
    float longitude;                //存放经度数据，结果为度
    float latitude;                 //存放纬度数据，结果为度
}gpsDataPack_t;


/************************************************************************************
 									函数原型
*************************************************************************************/
/*	文件描述符	*/
int GPS_getFD(void);

/*	初始化	*/
int GPS_Init(void);

/*  读取数据 /解析数据 */
ssize_t GPS_ReadRawData(void);
int GPS_ParseData(void);

/*  打包数据    */
char *GPS_DataPackageProcessing(void);

/*  获取数据    */
float GPS_getLongitudeValue(void);
float GPS_getLatitudeValue(void);

/*  打印数据    */
void GPS_PrintAllData(void);

/*  任务线程    */
void *TaskThread_GPS(void);


#endif

