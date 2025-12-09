/************************************************************************************
					文件名：Sonar.c
					最后一次修改时间：2024/11/28
					修改内容：
*************************************************************************************/

#include "Sonar.h"

#include "../../tool/tool.h"
#include "../../sys/SerialPort/SerialPort.h"
#include "../../sys/epoll/epoll_manager.h"

/************************************************************************************
 									外部变量
*************************************************************************************/
extern int g_epoll_manager_fd;		//Epoll管理器
extern pthread_cond_t g_sonar_cond;
extern pthread_mutex_t g_sonar_mutex;
extern pthread_t g_tid_sonar;

/************************************************************************************
 									全局变量(可以extern的变量)
*************************************************************************************/
/*  工作状态    */
volatile int g_sonar_status = -1;			//-1为不可工作，1为可以工作

/*	保存解析后的数据	*/
sonarDataPack_t g_sonar_dataPack = {
    .obstaclesBearing = 0,
    .obstaclesDistance = 0
};

/************************************************************************************
 									全局变量(仅可本文件使用)
*************************************************************************************/
/*	设备路径	*/
static char *g_sonar_deviceName = "/dev/ttyS0";

/*	设备的文件描述符	*/
static int g_sonar_fd = -1;

/*	旧的串口配置	*/
static struct termios g_sonar_oldSerialPortConfig = {0};

/*	读取原始数据之后 存放的数组	*/
static unsigned char g_sonar_readbuf[144] = {0};

/*	读写锁	*/
static pthread_rwlock_t g_sonar_rwlock = PTHREAD_RWLOCK_INITIALIZER;

pthread_t g_sonarTasktid = 0;

/************************************************************************************
 									指令
*************************************************************************************/
/*  停止心跳    */
const unsigned char Cmd_mtStopAlive[14] = {0x40, 0x30, 0x30, 0x30, 0x38, 0x08, 0x00, 0xFF, 0x02, 0x03, 0x42, 0x80, 0x02, 0x0A};

/*  发送版本    */
const unsigned char Cmd_mtSendVersion[14] = {0x40, 0x30, 0x30, 0x30, 0x38, 0x08, 0x00, 0xFF, 0x02, 0x03, 0x17, 0x80, 0x02, 0x0A};

/*  发送指令    */
const unsigned char Cmd_mtHeadCommand[82] = {0x40, 0x30, 0x30, 0x34, 0x43, 0x4C, 0x00, 0xFF, 0x02, 0x47, 0x13, 0x80, 0x02, 0x1D, 0x07, 0x23, 0x02, 0x99, 0x99, 0x99,
                                             0x02, 0x66, 0x66, 0x66, 0x05, 0xA3, 0x70, 0x3D, 0x06, 0x70, 0x3D, 0x0A, 0x09, 0x13, 0x01, 0x64, 0x00, 0x00, 0x00, 0xE0, 
                                             0x18, 0x53, 0x30, 0x6B, 0x6B, 0x5A, 0x00, 0x7D, 0x00, 0x19, 0x10, 0x54, 0x03, 0x14, 0x00, 0xE8, 0x03, 0x64, 0x00, 0x40, 
                                             0x06, 0x01, 0x00, 0x00, 0x00, 0x53, 0x53, 0x30, 0x30, 0x6B, 0x6B, 0x00, 0x00, 0x5A, 0x00, 0x7D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A};

/*  发送数据    */
const unsigned char Cmd_mtSendData[18] = {0x40, 0x30, 0x30, 0x30, 0x43, 0x0C, 0x00, 0xFF, 0x02, 0x07, 0x19, 0x80, 0x02, 0xCA, 0x64, 0xB0, 0x03, 0x0A};

 /*******************************************************************
 * 函数原型:int Sonar_getFD(void)
 * 函数简介:返回文件描述符
 * 函数参数:无
 * 函数返回值: 文件描述符数值
 *****************************************************************/
int Sonar_getFD(void)
{
	return g_sonar_fd;
}

/*******************************************************************
* 函数原型:int Sonar_Init(void)
* 函数简介:打开声呐串口设备，并且保存旧的配置
* 函数参数:无
* 函数返回值: 成功返回0，失败返回-1
*****************************************************************/
int Sonar_Init(void)
{
	/*	0.入口检查	*/
	if(g_sonar_deviceName == NULL || g_sonar_fd > 0)
	{
		return -1;
	}

	g_sonar_fd = -1;

	/*	1.打开串口	*/
    g_sonar_fd = SerialPort_open(g_sonar_deviceName, &g_sonar_oldSerialPortConfig);
	if(g_sonar_fd < 0)
	{
		return -1;
	}

	/*	2.配置串口	*/
	if(SerialPort_configBaseParams(g_sonar_fd, 115200,  1, 8, 'N') < 0)
	{
		close(g_sonar_fd);  // 关闭已打开的文件描述符
        g_sonar_fd = -1;
		return -1;
	}

	sleep(5);	//数据延时等待

	return 0;
}


 /*******************************************************************
 * 函数原型:int Sonar_Close(void)
 * 函数简介:关闭Sonar的文件描述符，调用SerialPort函数
 * 函数参数:无
 * 函数返回值:成功返回0，失败返回-1
 *****************************************************************/
int Sonar_Close(void)
{
	/*	0.入口检查	*/
 	if(g_sonar_fd < 0)
 	{
 		return -1;
 	}
 	
	/*	1.关闭Sonar	*/
	pthread_rwlock_wrlock(&g_sonar_rwlock);
	if(SerialPort_close(g_sonar_fd, &g_sonar_oldSerialPortConfig) < 0)
	{
		return -1;
	}

    /*	2.删除对应的监听	*/
	epoll_manager_del_fd(g_epoll_manager_fd, g_sonar_fd);

	/*3.更新文件描述符和工作状态(线程退出)*/
	g_sonar_fd = -1;
    g_sonar_status = -1;

    pthread_kill(g_tid_sonar, SIGKILL);
    g_tid_sonar = 0;

    //pthread_cancel(g_tid_sonar);
	//pthread_join(g_tid_sonar, NULL);

    printf("Sonar工作线程已退出\n");

    pthread_mutex_unlock(&g_sonar_mutex);
	pthread_cond_signal(&g_sonar_cond);

	pthread_rwlock_unlock(&g_sonar_rwlock);

 	return 0;
}

/*******************************************************************
 * 函数原型:int Sonar_writeCmd_mtStopAlive(void)
 * 函数简介:发送Cmd_mtStopAlive命令
 * 函数参数:无
 * 函数返回值: 成功返回0，失败就返回-1
 *****************************************************************/
int Sonar_writeCmd_mtStopAlive(void)
{
    /*  1.入口检查    */
    if(g_sonar_fd < 0)
	{
		perror("Sonar_writeCmd_mtStopAlive:g_sonar_fd error");
		return -1;
	}

	/*	发送Cmd_mtStopAlive命令	*/
    if(14 != write(g_sonar_fd, Cmd_mtStopAlive, 14))
	{
		printf("Sonar_writeCmd_mtStopAlive:发送 Cmd_mtStopAlive 出错\n");
		return -1;
	}

	sleep(2);
    tcflush(g_sonar_fd, TCIOFLUSH);

    return 0;
}

/*******************************************************************
 * 函数原型:int Sonar_writeCmd_mtSendVersion(void)
 * 函数简介:发送Cmd_mtSendVersion命令
 * 函数参数:无
 * 函数返回值: 成功返回0，失败就返回-1
 *****************************************************************/
int Sonar_writeCmd_mtSendVersion(void)
{
    /*  1.入口检查    */
    if(g_sonar_fd < 0)
	{
		perror("Sonar_writeCmd_mtSendVersion:g_sonar_fd error");
		return -1;
	}

	/*	2.发送Cmd_mtSendVersion命令	*/
	if(14 != write(g_sonar_fd, Cmd_mtSendVersion, 14))
	{
		printf("Sonar_writeCmd_mtSendVersion:发送 Cmd_mtSendVersion 出错\n");
		return -1;
	}

	sleep(2);
    tcflush(g_sonar_fd, TCIOFLUSH);

    return 0;
}

/*******************************************************************
 * 函数原型:int Sonar_writeCmd_mtHeadCommand(int sonarfd)
 * 函数简介:发送Cmd_mtHeadCommand命令
 * 函数参数:无
 * 函数返回值: 成功返回0，失败就返回-1
 *****************************************************************/
int Sonar_writeCmd_mtHeadCommand(void)
{
    /*  1.入口检查    */
    if(g_sonar_fd < 0)
	{
		perror("Sonar_writeCmd_mtHeadCommand:g_sonar_fd error");
		return -1;
	}

    /*	2.发送Cmd_mtHeadCommand命令	*/
	if(82 != write(g_sonar_fd, Cmd_mtHeadCommand, 82))
	{
		printf("Sonar_writeCmd_mtHeadCommand:发送 Cmd_mtSendVersion 出错\n");
		return -1;
	}

	sleep(2);
    tcflush(g_sonar_fd, TCIOFLUSH);

    return 0;
}

#if 0
/*******************************************************************
 * 函数原型:void *sonarThread_handle(void)
 * 函数简介:任务线程函数，传感器数据打包发送
 * 函数参数:无
 * 函数返回值: 无
 *****************************************************************/
void *thread_sonarTaskExec(void)
{
    /*  设置线程分离    */
    pthread_detach(pthread_self());
    printf("声呐任务线程成功\n");
    sleep(5);

    tcflush(g_sonarfd,TCIOFLUSH);

    /*  任务执行        */
    while(1)
    {
        if(0 == Sonar_sendDataRequest(g_sonarfd, Cmd_mtSendData))
        {
            sleep(1);
            if(-1 != Sonar_readData(g_sonarfd, g_sonarReadBuf, sizeof(g_sonarReadBuf)))
            {
                Sonar_parseData(g_sonarReadBuf, g_psonarDataPack);
                //Database_insertSonarData(g_database, g_psonarDataPack);
                Sonar_printSensorData(g_psonarDataPack);
            }


            if(g_psonarDataPack->obstaclesBearing > 135.0 && g_psonarDataPack->obstaclesBearing < 225.0)
            {
                //sonarTCP发送
            }
        }
        else
        {
            printf("wenjianmiaoshufuzhunbei\n");
            sleep(5);
        }
        usleep(30000);
        tcflush(g_sonarfd, TCIOFLUSH);
    }
}

/*******************************************************************
* 函数原型:pthread_t Sonar_createSonarTaskThread(void)
* 函数简介:创建声呐任务线程。
* 函数参数:无
* 函数返回值:成功返回线程的TID，失败返回0。
*****************************************************************/
pthread_t Sonar_createSonarTaskThread(void)
{
	pthread_t sonarTasktid;
	if(pthread_create(&sonarTasktid, NULL, (void*)thread_sonarTaskExec, NULL) < 0)
	{
		perror("thruster create sonar task thread error:");
		return 0;
	}
	
	return sonarTasktid;
}
#endif

/*******************************************************************
 * 函数原型:int Sonar_SendDataRequest(void)
 * 函数简介:发送数据请求指令
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int Sonar_SendDataRequest(void)
{
    /*  入口检测    */
    if(g_sonar_fd < 0 || Cmd_mtSendData == NULL)
    {
        return -1;
    }

    /*  发送数据    */
    int nwrite = 0;
    nwrite = write(g_sonar_fd, Cmd_mtSendData, 18);
    if(nwrite != 18)
    {
        tcflush(g_sonar_fd, TCIFLUSH);
        return -1;
    }

    return 0;
}


/*******************************************************************
 * 函数原型:ssize_t Sonar_ReadRawData(void)
 * 函数简介:声呐从串口读取数据
 * 函数参数:无
 * 函数返回值: 成功返回0，失败就返回-1
 *****************************************************************/ 
ssize_t Sonar_ReadRawData(void)
{
    /*  入口检测    */
    if(g_sonar_fd < 0 || g_sonar_readbuf == NULL || sizeof(g_sonar_readbuf) < 128)
    {
        return -1;
    }

    /*  读取数据    */
    pthread_rwlock_wrlock(&g_sonar_rwlock);
	memset(g_sonar_readbuf, 0, sizeof(g_sonar_readbuf));

    ssize_t nread = 0;
    unsigned char uc = 0;
    int start = 0, i = 0;

    while(1)
    {
        nread = read(g_sonar_fd, &uc, 1);
        
        if(nread == 1)
        {
            if(uc == 0x40)
                start = 1;
            if(start)
            {
                g_sonar_readbuf[i++] = uc;
            }
            if(i == 64)
                break;
        }
        else if(nread == 0)
        {
            continue;
        }
        else
        {
            break;
        }
    }

    /*  数据检查    */
    if(*(g_sonar_readbuf+0) != 0x40 || *(g_sonar_readbuf+1) != 0x30 || *(g_sonar_readbuf+63) != 0x0A)
    {
        printf("Sonar_ReadRawData:sonar data invalid\n");
        memset(g_sonar_readbuf, 0, sizeof(g_sonar_readbuf));
        strcpy((char *)g_sonar_readbuf, "invalid");
        tcflush(g_sonar_fd, TCIFLUSH);
        pthread_rwlock_unlock(&g_sonar_rwlock);
        return -1;
    }
		
    pthread_rwlock_unlock(&g_sonar_rwlock);
    tcflush(g_sonar_fd, TCIFLUSH);
    return 0;
}


 /*******************************************************************
 * 函数原型:int Sonar_ParseData(void)
 * 函数简介:解析Sonar数据，将原始采集到的数据提取解析保存数据结构体中
 * 函数参数:无
 * 函数返回值:成功返回0，失败返回-1 
 *****************************************************************/
int Sonar_ParseData(void)
{
    /*  入口检测*/
    if(g_sonar_readbuf == NULL)
	{
		return -1;
	}
	else if(strncmp((char *)g_sonar_readbuf, "invalid", 7) == 0)
	{
		printf("Sonar_ParseData:声呐数据无效\n");
		return -1;
	}

    /*  方位计算    */
    unsigned char bearingH = 0;
    unsigned char bearingL = 0;
    unsigned int bearingHex = 0;
    float bearing = 0.0f;

    pthread_rwlock_wrlock(&g_sonar_rwlock);
    bearingH = *(g_sonar_readbuf + 41);
    bearingL = *(g_sonar_readbuf + 40);
    bearingHex = (unsigned int)(bearingH << 8 | bearingL);
    bearing = bearingHex * 0.05625;                 // (/ 6400.0 * 360.0);
    g_sonar_dataPack.obstaclesBearing = bearing;

    /*  距离计算    */
    unsigned char distanceArr[19] = {0};
    unsigned int scanlow = 0x28;
    unsigned int sacnhigh = 0xff;
    float distance = 0.0f;

    memcpy(distanceArr, g_sonar_readbuf+44, 19);
    for(int i = 0; i < 18; i++)
    {
        if(distanceArr[i] > scanlow && distanceArr[i] < sacnhigh)
        {
            distance = (i+1) * 0.5;
            break;
        }
    }
    g_sonar_dataPack.obstaclesDistance = distance;

    pthread_rwlock_unlock(&g_sonar_rwlock);
    return 0;
}

/*******************************************************************
* 函数原型:float Sonar_getObstaclesBearingValue(void)
* 函数简介:获得障碍物方位数值。单位是角度
* 函数参数:无
* 函数返回值: 返回obstaclesBearing的数值。
*****************************************************************/ 
float Sonar_getObstaclesBearingValue(void)
{
    return g_sonar_dataPack.obstaclesBearing;
}

/*******************************************************************
* 函数原型:float Sonar_getObstaclesDistanceValue(void)
* 函数简介:获得障碍物距离数值。单位是米
* 函数参数:无
* 函数返回值: 返回obstaclesDistance的数值。
*****************************************************************/ 
float Sonar_getObstaclesDistanceValue(void)
{
    return g_sonar_dataPack.obstaclesDistance;
}

/*******************************************************************
* 函数原型:char *Sonar_DataPackageProcessing(void)
* 函数简介:将声呐的数据进行按格式打包
* 函数参数:无
* 函数返回值: 成功返回打包好的数据地址
*****************************************************************/
char *Sonar_DataPackageProcessing(void)
{
	static char sonarSendDataBuf[32] = {0};
    sprintf(sonarSendDataBuf, "[%05.1f[%04.1f[", g_sonar_dataPack.obstaclesBearing, g_sonar_dataPack.obstaclesDistance);

    return sonarSendDataBuf;
}


/*******************************************************************
* 函数原型:void Sonar_PrintSensorData(void)
* 函数简介:打印传感器采集的保存在数据结构体中的数据
* 函数参数:无
* 函数返回值: 无
*****************************************************************/ 
void Sonar_PrintSensorData(void)
{
	time_t now = time(NULL);	
	struct tm *nowtime = localtime(&now);
    printf("TIME: %d-%02d-%02d %02d:%02d:%02d\n",nowtime->tm_year + 1900,nowtime->tm_mon + 1,nowtime->tm_mday,\
    											nowtime->tm_hour,nowtime->tm_min,nowtime->tm_sec);
	
	printf("************声呐数据信息**************\n");
	printf("方位:%05.1f °\n", g_sonar_dataPack.obstaclesBearing);
    printf("障碍物距离:%04.1f M\n", g_sonar_dataPack.obstaclesDistance);
	printf("************************************\n");
}


