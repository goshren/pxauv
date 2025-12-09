/************************************************************************************
					文件名：Sonar.h
					最后一次修改时间：2024/11/28
					修改内容：
*************************************************************************************/


#ifndef __SONAR_H__
#define __SONAR_H__


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
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <pthread.h>
#include <time.h>


/************************************************************************************
 									数据类型
*************************************************************************************/
/* 保存采集的数据 */
typedef struct 
{
	float obstaclesBearing;							//障碍物方位（角度）
	float obstaclesDistance;						//障碍物距离 单位米
}sonarDataPack_t;


/************************************************************************************
 									函数原型
*************************************************************************************/
/*	外界获取文件描述符	*/
int Sonar_getFD(void);

/*	初始化	*/
int Sonar_Init(void);

/*	关闭声呐	*/
int Sonar_Close(void);

/*	发送上电配置	*/
int Sonar_writeCmd_mtStopAlive(void);
int Sonar_writeCmd_mtSendVersion(void);
int Sonar_writeCmd_mtHeadCommand(void);

/*	发送数据请求	*/
int Sonar_SendDataRequest(void);

/*	读取/解析数据	*/
ssize_t Sonar_ReadRawData(void);
int Sonar_ParseData(void);

/*	数据获取	*/
float Sonar_getObstaclesBearingValue(void);
float Sonar_getObstaclesDistanceValue(void);

/*	工作线程	*/
void *thread_sonarTaskExec(void);
pthread_t Sonar_createSonarTaskThread(void);

/*	数据打包	*/
char *Sonar_DataPackageProcessing(void);

/*	打印数值	*/
void Sonar_PrintSensorData(void);

#endif
