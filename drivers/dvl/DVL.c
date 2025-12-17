/************************************************************************************
					文件名：DVL.c
					最后一次修改时间：2025/6/27
					修改内容：代码升级
 ************************************************************************************/
 
#include "DVL.h"
#include "../../sys/SerialPort/SerialPort.h"
#include "../../sys/epoll/epoll_manager.h"

 /************************************************************************************
 									外部变量
*************************************************************************************/
extern int g_epoll_manager_fd;		//Epoll管理器
extern pthread_cond_t g_dvl_cond;
extern pthread_mutex_t g_dvl_mutex;
extern pthread_t g_tid_dvl;

/************************************************************************************
 									全局变量(可以extern的变量)
*************************************************************************************/
/*  工作状态    */
volatile int g_dvl_status = -1;			//-1为关闭串口，1为开启串口

/*	保存解析后的数据	*/
dvlDataPack_t g_dvlDataPack = 
{
	.pitch = 0.0,
	.roll = 0.0,
	.heading = 0.0,
	.transducerEntryDepth = 0.0,
	.speedX = 0.0,
	.speedY = 0.0,
	.speedZ = 0.0,
    .buttomDistance = 0.0
};

/************************************************************************************
 									全局变量(仅可本文件使用)
*************************************************************************************/
/*	设备路径	*/
static char *g_dvlDeviceName = "/dev/dvl";

/*	设备的文件描述符	*/
static int g_dvl_fd = -1;

/*	读取原始数据之后 存放的数组	*/
static char g_dvl_readbuf[512] = {0};

/*	读写锁	*/
static pthread_rwlock_t g_dvl_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/*	旧的串口配置	*/
static struct termios g_dvl_oldSerialPortConfig = {0};

/*	数据帧协议	*/
static dvlDataProtocol_t g_dvlDataProtocol = 
{
	.length = 224,
	.timeout_ms = 200
};

/************************************************************************************
 									指令
*************************************************************************************/
const char Cmd_OpenDVLDevice[32] = {"CS \r000000000000"};
const char Cmd_SetDVLSendFreq[32] = {"PR 01\r0000000000"};


/************************************************************************************
 									辅助函数(仅本文件可使用)
*************************************************************************************/
 /*******************************************************************
 * 函数原型:int DVL_Init_de(const char *dvlDeviceName)
 * 函数简介:打开DVL串口设备，并且保存旧的配置
 * 函数参数:device:串口设备字符串
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
static int DVL_ConfigureSerial(int fd)
{
    struct termios options;
    
    if (tcgetattr(fd, &options) != 0) {
        perror("tcgetattr failed: DVL serialport");
        return -1;
    }
    
    // 设置波特率
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);
    
    // 8N1配置
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag |= (CLOCAL | CREAD);
    
    // 原始输入模式
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    
    // 原始输出模式
    options.c_oflag &= ~OPOST;
    
    // 设置超时和最小字符数
    options.c_cc[VMIN] = 1;
    options.c_cc[VTIME] = 0;
    
    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        perror("tcsetattr failed: DVL serialport");
        return -1;
    }
    
    return 0;
}


/************************************************************************************
 									公共接口实现(外部可调用)
*************************************************************************************/
 /*******************************************************************
 * 函数原型:int DVL_getFD(void)
 * 函数简介:返回文件描述符
 * 函数参数:无
 * 函数返回值: 文件描述符数值
 *****************************************************************/
int DVL_getFD(void)
{
	return g_dvl_fd;
}


/*******************************************************************
* 函数原型:int DVL_Init_default(void) 
* 函数简介:初始化DVL的串口，可直接调用进行初始化
* 函数参数:无
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int DVL_Init_default(void) 
{
	/*	0.入口检查	*/
    if(g_dvlDeviceName == NULL || g_dvl_fd > 0)
	{
        return -1; 
    }
    
	/*	1.打开串口	*/
    g_dvl_fd = open(g_dvlDeviceName, O_RDWR | O_NOCTTY | O_NDELAY);
    if(g_dvl_fd < 0)
	{
        return -1;
    }
    
    /*	2.恢复阻塞模式	*/
    fcntl(g_dvl_fd, F_SETFL, 0);
    
    if(DVL_ConfigureSerial(g_dvl_fd) < 0)
	{
        close(g_dvl_fd);
        g_dvl_fd = -1;
        return -1;
    }
    
	g_dvl_status = 1;
    return 0;
}


 /*******************************************************************
 * 函数原型:int DVL_init(void)
 * 函数简介:打开DVL串口设备，并且保存旧的配置
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int DVL_Init(void)
{
	/*	0.入口检查	*/
	if(g_dvlDeviceName == NULL || g_dvl_fd > 0)
	{
		return -1;
	}

	g_dvl_fd = -1;

	/*	1.打开串口	*/
    g_dvl_fd = SerialPort_open(g_dvlDeviceName, &g_dvl_oldSerialPortConfig);
	if(g_dvl_fd < 0)
	{
		return -1;
	}

	/*	2.配置串口	*/
	if(SerialPort_configBaseParams(g_dvl_fd, 115200,  1, 8, 'N') < 0)
	{
        close(g_dvl_fd);  // 关闭已打开的文件描述符
        g_dvl_fd = -1;
		return -1;
	}

	return 0;
}


 /*******************************************************************
 * 函数原型:int DVL_Close_default(void)
 * 函数简介:关闭DVL的文件描述符，默认
 * 函数参数:无
 * 函数返回值:成功返回0，失败返回-1
 *****************************************************************/
int DVL_Close_default(void)
{
	/*	0.入口检查	*/
 	if(g_dvl_fd < 0)
 	{
 		perror("DVL_close:");
 		return -1;
 	}
 	
	/*	1.关闭DVL	*/
	pthread_rwlock_wrlock(&g_dvl_rwlock);
 	close(g_dvl_fd);
	g_dvl_fd = -1;
	g_dvl_status = -1;
	pthread_rwlock_unlock(&g_dvl_rwlock);

 	return 0;
}

 /*******************************************************************
 * 函数原型:int DVL_Close(void)
 * 函数简介:关闭DVL的文件描述符，调用SerialPort函数
 * 函数参数:无
 * 函数返回值:成功返回0，失败返回-1
 *****************************************************************/
int DVL_Close(void)
{
	/*	0.入口检查	*/
 	if(g_dvl_fd < 0)
 	{
 		return -1;
 	}
 	
	/*	1.关闭DVL	*/
	pthread_rwlock_wrlock(&g_dvl_rwlock);
	if(SerialPort_close(g_dvl_fd, &g_dvl_oldSerialPortConfig) < 0)
	{
		return -1;
	}

    /*	2.删除对应的监听	*/
	epoll_manager_del_fd(g_epoll_manager_fd, g_dvl_fd);

	/*3.更新文件描述符和工作状态(线程退出)*/
	g_dvl_fd = -1;
    g_dvl_status = -1;

	pthread_kill(g_tid_dvl, SIGKILL);

    printf("DVL工作线程已退出\n");
	g_tid_dvl = 0;

	pthread_mutex_unlock(&g_dvl_mutex);
	pthread_cond_signal(&g_dvl_cond);

	pthread_rwlock_unlock(&g_dvl_rwlock);

 	return 0;
}


 /*******************************************************************
 * 函数原型:int DVL_SendCmd_OpenDVLDevice(void)
 * 函数简介:DVL上电之后发送开始传输命令
 * 函数参数:无
 * 函数返回值: 成功返回0，失败就返回-1
 *****************************************************************/
int DVL_SendCmd_OpenDVLDevice(void)
{
	if(g_dvl_fd < 0)
	{
		return -1;
	}

	/*	1.发送开始数据传输命令	*/
	if(16 != write(g_dvl_fd, Cmd_OpenDVLDevice, strlen(Cmd_OpenDVLDevice)))
	{
		printf("DVL_SendCmd_OpenDVLDevice:Send Cmd_OpenDVLDevice error\n");
		return -1;
	}

	sleep(1);
	tcflush(g_dvl_fd, TCIOFLUSH);

	return 0;
}


 /*******************************************************************
 * 函数原型:int DVL_SendCmd_SetDVLSendFreq(void)
 * 函数简介:DVL上电之后需要发送设置频率命令
 * 函数参数:无
 * 函数返回值: 成功返回0，失败就返回-1
 *****************************************************************/
int DVL_SendCmd_SetDVLSendFreq(void)
{
	if(g_dvl_fd < 0)
	{
		return -1;
	}

	/*	1.发送设置频率命令	*/
	if(16 != write(g_dvl_fd, Cmd_SetDVLSendFreq, strlen(Cmd_SetDVLSendFreq)))
	{
		printf("DVL_SendCmd_SetDVLSendFreq:Send Cmd_SetDVLSendFreq error\n");
		return -1;
	}
	sleep(1);
	tcflush(g_dvl_fd, TCIOFLUSH);

	return 0;
}

 /*******************************************************************
 * 函数原型:ssize_t DVL_readData(int dvlfd, void *dvlReadBuf, size_t bufsize)
 * 函数简介:DVL从串口读取数据
 * 函数参数:dvlfd:DVL设备的文件描述符
 * 		   dvlReadBuf:读取的数据存放的数据
 * 			bufsize:数组大小
 * 函数返回值: 成功返回读取到的字节个数，失败就返回-1
 *****************************************************************/ 
ssize_t DVL_ReadRawData(void)
{
    /* 0.*入口检查	*/
    if(g_dvl_fd < 0 || g_dvl_readbuf == NULL || sizeof(g_dvl_readbuf) < g_dvlDataProtocol.length + 1)
	{
        return -1;
    }

    pthread_rwlock_wrlock(&g_dvl_rwlock);
    memset(g_dvl_readbuf, 0, sizeof(g_dvl_readbuf));

    ssize_t nread = 0;
    char c = 0;
    int i = 0;
    int datahead = 0, dataend = 0;
    int start = 0;
    int byte = 0;
    int isValid = -1;

    while(byte < 512)
    {
        nread = read(g_dvl_fd, &c, 1);
        byte++;
        if(nread == 1)
        {
            if(c == ':')
            {
                start = 1;
                datahead++;
            }

            if(c == '\r' && start == 1)
            {
                dataend++;
            }
                
            if(start)
            {
                g_dvl_readbuf[i++] = c;
            }

            if(datahead == 6 && dataend == 6)
            {
                g_dvl_readbuf[i++] = '\0';
                isValid = 0;
                pthread_rwlock_unlock(&g_dvl_rwlock);
                break;
            }
            else if(datahead - dataend >= 2)
            {
                isValid = -1;
                pthread_rwlock_unlock(&g_dvl_rwlock);
                break;
            }
        }
        else 
        {
            perror("DVL_ReadRawData:dvl read data error");
            isValid = -1;
            pthread_rwlock_unlock(&g_dvl_rwlock);
            break;
        }            
    }

    /*	字符串检测	*/
    static char *invalidStrArr[] = {"::", "CS", "PR", NULL};
    static char *mustExistStrArr[] = {":SA", ":TS", ":BI", ":BS", ":BE", ":BD", NULL};
    int invalidStrNum = sizeof(invalidStrArr) / sizeof(char *) - 1;
    int mustExistStrNum = sizeof(mustExistStrArr) / sizeof(char *) - 1;

    /*	检测不想存在的字符串是否存在	*/
    for(int i = 0; i < invalidStrNum; i++)
    {
        if(strstr(g_dvl_readbuf, invalidStrArr[i]) != NULL)
        {
            isValid = -1;
        }
    }

    /*	检测必须存在的字符串是否存在	*/
    for(int i = 0; i < mustExistStrNum; i++)
    {
        if(strstr(g_dvl_readbuf, mustExistStrArr[i]) == NULL)
        {
            isValid = -1;
        }
    }

    if(isValid == -1)
    {
        printf("DVL_ReadRawData:dvl data invaild\n");
        memset(g_dvl_readbuf, 0, sizeof(g_dvl_readbuf));
        strcpy(g_dvl_readbuf, "invalid");
        tcflush(g_dvl_fd, TCIFLUSH);
        return -1;
    }

    tcflush(g_dvl_fd, TCIFLUSH);
    return 0;
}


 /*******************************************************************
 * 函数原型:int DVL_ParseData(void)
 * 函数简介:解析DVL数据。
 * 函数参数:dvlReadBuf:读取的数据存放的数据
 * 函数返回值:成功返回0，失败返回-1 
 *****************************************************************/
int DVL_ParseData(void)
{
	if(g_dvl_readbuf == NULL)
	{
		printf("DVL_ParseData:dvlRawData NULL\n");
		return -1;
	}
	else if(strncmp(g_dvl_readbuf, "invalid", 7) == 0)
	{
		printf("DVL_ParseData:dvlRawData invalid\n");
		return -1;
	}

    static char pitch[8];						//俯仰，单位°
	static char roll[8];						//横滚，单位°
	static char heading[8];					//艏向，单位°
	static char transducerEntryDepth[8];		//换能器入水深度，单位m
	static char speedX[8];						//底跟踪，X轴速度，船头为正，单位mm/s
	static char speedY[8];						//底跟踪，Y轴速度，右舷为正，单位mm/s
	static char speedZ[8];						//底跟踪，Z轴速度，向下为正，单位mm/s
	static char buttomDistance[8];				//设备离底距离 高度，单位m

	pthread_rwlock_wrlock(&g_dvl_rwlock);

	char *datahead = NULL;

	datahead = strstr(g_dvl_readbuf, ":SA");
	if(datahead != NULL)
	{
		strncpy(pitch, datahead+4, 6);
		strncpy(roll, datahead+11, 6);
		strncpy(heading, datahead+18, 6);
        sscanf(pitch,"%f", &g_dvlDataPack.pitch);
        sscanf(roll,"%f", &g_dvlDataPack.roll);
        sscanf(heading,"%f", &g_dvlDataPack.heading);
	}
    
	datahead = strstr(g_dvl_readbuf, ":BE");
	if(datahead != NULL)
	{
		strncpy(speedX, datahead+4, 6);
		strncpy(speedY, datahead+11, 6);
		strncpy(speedZ, datahead+18, 6);
        sscanf(speedX,"%f", &g_dvlDataPack.speedX);
        sscanf(speedY,"%f", &g_dvlDataPack.speedY);
        sscanf(speedZ,"%f", &g_dvlDataPack.speedZ);
	}

	datahead = strstr(g_dvl_readbuf, ":TS");
	if(datahead != NULL)
	{
		strncpy(transducerEntryDepth, datahead+30, 6);
        sscanf(transducerEntryDepth,"%f", &g_dvlDataPack.transducerEntryDepth);
	}

	datahead = strstr(g_dvl_readbuf, ":BD");
	if(datahead != NULL)
	{
		strncpy(buttomDistance, datahead+43, 7);
        sscanf(buttomDistance,"%f", &g_dvlDataPack.buttomDistance);
	}

    memset(pitch, 0, sizeof(pitch));
    memset(roll, 0, sizeof(roll));
    memset(heading, 0, sizeof(heading));
    memset(speedX, 0, sizeof(speedX));
    memset(speedY, 0, sizeof(speedY));
    memset(speedZ, 0, sizeof(speedZ));
    memset(transducerEntryDepth, 0, sizeof(transducerEntryDepth));
    memset(buttomDistance, 0, sizeof(buttomDistance));

	pthread_rwlock_unlock(&g_dvl_rwlock);
	
	return 0;	
}

/*******************************************************************
 * 函数原型:float DVL_getPitchValue(void)
 * 函数简介:获得pitch数值。
 * 函数参数:无。
 * 函数返回值: 返回pitch的数值
 *****************************************************************/ 
float DVL_getPitchValue(void)
{
	return g_dvlDataPack.pitch;
}

/*******************************************************************
 * 函数原型:float DVL_getRollValue(void)
 * 函数简介:获得roll数值。
 * 函数参数:无。
 * 函数返回值: 返回roll的数值
 *****************************************************************/ 
float DVL_getRollValue(void)
{
	return g_dvlDataPack.roll;
}

/*******************************************************************
 * 函数原型:float DVL_getHeadingValue(void)
 * 函数简介:获得heading数值。
 * 函数参数:无。
 * 函数返回值: 返回heading的数值
 *****************************************************************/ 
float DVL_getHeadingValue(void)
{
	return g_dvlDataPack.heading;
}

/*******************************************************************
 * 函数原型:float DVL_getSpeedXValue(void)
 * 函数简介:获得speedX数值。
 * 函数参数:无。
 * 函数返回值: 返回speedX的数值
 *****************************************************************/ 
float DVL_getSpeedXValue(void)
{
	return g_dvlDataPack.speedX;
}

/*******************************************************************
 * 函数原型:float DVL_getSpeedYValue(void)
 * 函数简介:获得speedY数值。
 * 函数参数:无。
 * 函数返回值: 返回speedY的数值
 *****************************************************************/ 
float DVL_getSpeedYValue(void)
{
	return g_dvlDataPack.speedY;
}

/*******************************************************************
 * 函数原型:float DVL_getSpeedZValue(void)
 * 函数简介:获得speedZ数值。
 * 函数参数:无。
 * 函数返回值: 返回speedZ的数值
 *****************************************************************/ 
float DVL_getSpeedZValue(void)
{
	return g_dvlDataPack.speedZ;
}

/*******************************************************************
 * 函数原型:float DVL_getTransducerEntryDepthValue(void)
 * 函数简介:获得transducerEntryDepth数值。
 * 函数参数:无。
 * 函数返回值: 返回transducerEntryDepth的数值
 *****************************************************************/ 
float DVL_getTransducerEntryDepthValue(void)
{
	return g_dvlDataPack.transducerEntryDepth;
}

/*******************************************************************
 * 函数原型:float DVL_getButtomDistanceValue(void)
 * 函数简介:获得buttomDistance数值。
 * 函数参数:无。
 * 函数返回值: 返回buttomDistance的数值
 *****************************************************************/ 
float DVL_getButtomDistanceValue(void)
{
	return g_dvlDataPack.buttomDistance;
}

/*******************************************************************
* 函数原型:void DVL_printSensorData(void)
* 函数简介:打印全部数据
* 函数参数:无
* 函数返回值: 无
*****************************************************************/ 
void DVL_PrintAllData(void)
{
	time_t now = time(NULL);	
	struct tm *nowtime = localtime(&now);
    printf("TIME: %d-%02d-%02d %02d:%02d:%02d\n",nowtime->tm_year + 1900,nowtime->tm_mon + 1,nowtime->tm_mday,\
    											nowtime->tm_hour,nowtime->tm_min,nowtime->tm_sec);
	
/*	printf("************DVL数据信息**************\n");
	printf("俯仰:%f °\n", g_dvlDataPack.pitch);
	printf("横滚:%f °\n", g_dvlDataPack.roll);
	printf("艏向:%f °\n", g_dvlDataPack.heading);
	printf("换能器入水深度:%f m\n", g_dvlDataPack.transducerEntryDepth);
	printf("X轴速度:%f mm/s\n", g_dvlDataPack.speedX);
	printf("Y轴速度:%f mm/s\n", g_dvlDataPack.speedY);
	printf("Z轴速度:%f mm/s\n", g_dvlDataPack.speedZ);
	printf("高度:%f m\n", g_dvlDataPack.buttomDistance);
	printf("************************************\n");
*/
}


/*******************************************************************
* 函数原型:char *DVL_DataPackageProcessing(void)
* 函数简介:将DVL的数据进行按格式打包
* 函数参数:无
* 函数返回值: 成功返回打包好的数据地址
*****************************************************************/
char *DVL_DataPackageProcessing(void)
{
	static char dvlSendDataBuf[100] = {0};
	snprintf(dvlSendDataBuf, sizeof(dvlSendDataBuf),"*%6.2f*%6.2f*%6.2f*%6.2f*%6.2f*%6.2f*%6.2f*%6.2f*", \
    g_dvlDataPack.pitch, g_dvlDataPack.roll, g_dvlDataPack.heading, g_dvlDataPack.transducerEntryDepth, \
		g_dvlDataPack.speedX, g_dvlDataPack.speedY, g_dvlDataPack.speedZ, g_dvlDataPack.buttomDistance);
	
	return dvlSendDataBuf;
}

