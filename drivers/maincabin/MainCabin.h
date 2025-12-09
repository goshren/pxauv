/************************************************************************************
					文件名：MainCabin.h
					最后一次修改时间：2025/6/29
					修改内容：
*************************************************************************************/

#ifndef __MAINCABIN_H__
#define __MAINCABIN_H__

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
#define TCP_MAINCABIN_IP   					  "192.168.1.230"    
#define TCP_MAINCABIN_TEST_IP   	  "127.0.0.1" 
#define TCP_MAINCABIN_PORT 				(unsigned short)40002
#define MAX_TCP_CLI_RECV_DATA_SIZE		255
#define MAX_TCP_CLI_SEND_DATA_SIZE		255

/************************************************************************************
 									数据类型
*************************************************************************************/
/*  主控舱控制供电/断电的设备    */
typedef enum {
    CTD = 0,              
    DVL,                      
	USBL,
	Radio,
	Sonar,
	Releaser1,
	Releaser2
}MainCabin_Control_DeviceID;

/*	主控舱的数据结构体	*/
typedef struct{
	float temperature;								//存放主控舱温度信息，单位℃
	float humidity;									//存放主控舱湿度信息，单位%
	float pressure;									//存放主控舱气压信息，单位hPa
	char isLeak01[8];								//存放主控舱泄露信息01
	char isLeak02[8];								//存放主控舱泄露信息02
	char deviceState[8];							//存放DVL、CTD、USBL的上电状态
	char releaser1State[8];							//存放释放器1状态
	char releaser2State[8];							//存放释放器2状态
}maincabinDataPack_t;

/*	传感器的读取数据协议	*/
 typedef struct {
    int timeout_ms;							//读取超时
	int length;										//数据长度
} maincabinDataProtocol_t;

/************************************************************************************
 									函数原型
*************************************************************************************/
/*	文件描述符	*/
int MainCabin_getFD(void);

/*	初始化	*/
int MainCabin_Init(void);

/*  控制设备   */
int MainCabin_SwitchPowerDevice(MainCabin_Control_DeviceID id, int power);
int MainCabin_PowerOnAllDeviceExceptReleaser(void);
int MainCabin_PowerOffAllDeviceExceptReleaser(void);

/*  读取数据 /解析数据 */
int MainCabin_ReadRawData(void);
int MainCabin_ParseData(void);

/*  打包数据    */
char *MainCabin_DataPackageProcessing(void);

/*  打印数据    */
void MainCabin_PrintSensorData(void);

/*	上电初始化的工作线程	*/
void *MainCabin_CTD_PowerOnHandle(void *arg);
void *MainCabin_DVL_PowerOnHandle(void *arg);
void *MainCabin_DTU_PowerOnHandle(void *arg);
void *MainCabin_USBL_PowerOnHandle(void *arg);
void *MainCabin_Sonar_PowerOnHandle(void *arg);

#endif
