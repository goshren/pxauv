/************************************************************************************
					文件名：CTD.c
					最后一次修改时间：2025/6/27
					修改内容：代码升级
 ************************************************************************************/
 
#include "CTD.h"
#include "../../sys/SerialPort/SerialPort.h"
#include "../../sys/epoll/epoll_manager.h"

/************************************************************************************
 									外部变量
*************************************************************************************/
extern int g_epoll_manager_fd;		//Epoll管理器
extern pthread_cond_t g_ctd_cond;
extern pthread_mutex_t g_ctd_mutex;
extern pthread_t g_tid_ctd;

/************************************************************************************
 									全局变量(可以extern的变量)
*************************************************************************************/
/*  工作状态    */
volatile int g_ctd_status = -1;			//-1为不可工作，1为可以工作

/*	保存解析后的数据	*/
ctdDataPack_t g_ctdDataPack = 
{
	.temperature = 0.0,
	.conductivity = 0.0,
	.pressure = 0.0,
	.depth = 0.0,
	.salinity = 0.0,
	.soundVelocity = 0.0,
	.density = 0.0
};

/************************************************************************************
 									全局变量(仅可本文件使用)
*************************************************************************************/
/*	设备路径	*/
static char *g_ctdDeviceName = "/dev/ctd";

/*	设备的文件描述符	*/
static int g_ctd_fd = -1;

/*	读取原始数据之后 存放的数组	*/
static char g_ctd_readbuf[64] = {0};

/*	读写锁	*/
static pthread_rwlock_t g_ctd_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/*	旧的串口配置	*/
static struct termios g_ctd_oldSerialPortConfig = {0};

/*	数据帧协议	*/
static ctdDataProtocol_t g_ctdDataProtocol = 
{
	.length = 43,
	.head_marker = '$',
	.tail_marker = '\n',
	.timeout_ms = 200
};

/************************************************************************************
 							计算盐度、密度、声速的变量
************************************************************************************/
/*							计算盐度									*/ 
const double a0 = 0.0080,      a1 = -0.1692,      a2 = 25.3851,       a3 = 14.0941,      a4 = -7.0261,       a5 = 2.7081;
const double b0 = 0.0005,      b1 = -0.0056,      b2 = -0.0066,       b3 = -0.0375,      b4 = 0.06360,       b5 = -0.0144;
const double c0 = 0.6766097,   c1 = 2.00564e-2,   c2 = 1.104259e-4,   c3 = -6.9698e-7,   c4 = 1.0031e-9;
const double d1 = 3.426e-2,    d2 = 4.464e-4,     d3 = 4.215e-1,      d4 = -3.107e-3;
const double e1 = 2.070e-5,    e2 = -6.370e-10,   e3 = 3.989e-15;
const double k  = 0.0162;
/*							计算声速									*/ 
const double A00 = 1.389,      A01 = -1.262e-2,   A02 = 7.164e-5,     A03 = 2.006e-6,    A04 = -3.21e-8;
const double A10 = 9.4742e-5,  A11 = -1.2580e-5,  A12 = -6.4885e-8,   A13 = 1.0507e-8,   A14 = -2.0122e-10;         
const double A20 = -3.9064e-7, A21 = 9.1041e-9,   A22 = -1.6002e-10,  A23 = 7.988e-12;
const double A30 = 1.100e-10,  A31 = 6.649e-12,   A32 = -3.389e-13;
const double B00 = -1.922e-2,  B01 = -4.42e-5,    B10 = 7.3637e-5,    B11 = 1.7945e-7;
const double C00 = 1402.388,   C01 = 5.03711,     C02 = -5.80852e-2,  C03 = 3.3420e-4,   C04 = -1.47800e-6,  C05 = 3.1464e-9;;
const double C10 = 0.153563,   C11 = 6.8982e-4,   C12 = -8.1788e-6,   C13 = 1.3621e-7,   C14 = -6.1185e-10;      
const double C20 = 3.1260e-5,  C21 = -1.7107e-6,  C22 = 2.5974e-8,    C23 = -2.5335e-10, C24 = 1.0405e-12;
const double C30 = -9.7729e-9, C31 = 3.8504e-10,  C32 = -2.3643e-12;
const double D00 = 1.727e-3,   D10 = -7.9836e-6;


/************************************************************************************
 									辅助函数(仅本文件可使用)
*************************************************************************************/
 /*******************************************************************
 * 函数原型:int CTD_Init_de(const char *ctdDeviceName)
 * 函数简介:打开CTD串口设备，并且保存旧的配置
 * 函数参数:device:串口设备字符串
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
static int CTD_ConfigureSerial(int fd)
{
    struct termios options;
    
    if (tcgetattr(fd, &options) != 0) {
        perror("tcgetattr failed: CTD serialport");
        return -1;
    }
    
    // 设置波特率
    cfsetispeed(&options, B9600);
    cfsetospeed(&options, B9600);
    
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
        perror("tcsetattr failed: CTD serialport");
        return -1;
    }
    
    return 0;
}


/************************************************************************************
 									公共接口实现(外部可调用)
*************************************************************************************/
 /*******************************************************************
 * 函数原型:int CTD_getFD(void)
 * 函数简介:返回文件描述符
 * 函数参数:无
 * 函数返回值: 文件描述符数值
 *****************************************************************/
int CTD_getFD(void)
{
	return g_ctd_fd;
}


/*******************************************************************
* 函数原型:int CTD_Init_default(void) 
* 函数简介:初始化CTD的串口，可直接调用进行初始化
* 函数参数:无
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int CTD_Init_default(void) 
{
	/*	0.入口检查	*/
    if(g_ctdDeviceName == NULL || g_ctd_fd > 0)
	{
        return -1; 
    }
    
	/*	1.打开串口	*/
    g_ctd_fd = open(g_ctdDeviceName, O_RDWR | O_NOCTTY | O_NDELAY);
    if(g_ctd_fd < 0)
	{
        return -1;
    }
    
    /*	2.恢复阻塞模式	*/
    fcntl(g_ctd_fd, F_SETFL, 0);
    
    if(CTD_ConfigureSerial(g_ctd_fd) < 0)
	{
        close(g_ctd_fd);
        g_ctd_fd = -1;
        return -1;
    }
    
	g_ctd_status = 1;
    return 0;
}


 /*******************************************************************
 * 函数原型:int CTD_init(void)
 * 函数简介:打开CTD串口设备，并且保存旧的配置
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int CTD_Init(void)
{
	/*	0.入口检查	*/
	if(g_ctdDeviceName == NULL || g_ctd_fd > 0)
	{
		return -1;
	}

	g_ctd_fd = -1;

	/*	1.打开串口	*/
    g_ctd_fd = SerialPort_open(g_ctdDeviceName, &g_ctd_oldSerialPortConfig);
	if(g_ctd_fd < 0)
	{
		return -1;
	}

	/*	2.配置串口	*/
	if(SerialPort_configBaseParams(g_ctd_fd, 9600,  1, 8, 'N') < 0)
	{
		close(g_ctd_fd);  // 关闭已打开的文件描述符
        g_ctd_fd = -1;
		return -1;
	}

	sleep(3);	//数据延时等待

	return 0;
}


 /*******************************************************************
 * 函数原型:int CTD_Close_default(void)
 * 函数简介:关闭CTD的文件描述符，默认
 * 函数参数:无
 * 函数返回值:成功返回0，失败返回-1
 *****************************************************************/
int CTD_Close_default(void)
{
	/*	0.入口检查	*/
 	if(g_ctd_fd < 0)
 	{
 		perror("CTD_close:");
 		return -1;
 	}
 	
	/*	1.关闭CTD	*/
	pthread_rwlock_wrlock(&g_ctd_rwlock);
 	close(g_ctd_fd);
	g_ctd_fd = -1;
	g_ctd_status = -1;
	pthread_rwlock_unlock(&g_ctd_rwlock);

 	return 0;
}

 /*******************************************************************
 * 函数原型:int CTD_Close(void)
 * 函数简介:关闭CTD的文件描述符，调用SerialPort函数
 * 函数参数:无
 * 函数返回值:成功返回0，失败返回-1
 *****************************************************************/
int CTD_Close(void)
{
	/*	0.入口检查	*/
 	if(g_ctd_fd < 0)
 	{
 		return -1;
 	}
 	
	/*	1.关闭CTD	*/
	pthread_rwlock_wrlock(&g_ctd_rwlock);
	if(SerialPort_close(g_ctd_fd, &g_ctd_oldSerialPortConfig) < 0)
	{
		return -1;
	}

	/*	2.删除对应的监听	*/
	epoll_manager_del_fd(g_epoll_manager_fd, g_ctd_fd);

	/*3.更新文件描述符和工作状态(线程退出)*/
	g_ctd_fd = -1;
	g_ctd_status = -1;

	//pthread_cancel(g_tid_ctd);
	//pthread_join(g_tid_ctd, NULL);

	pthread_kill(g_tid_ctd, SIGKILL);

    printf("CTD工作线程已退出\n");
	g_tid_ctd = 0; 

	pthread_mutex_unlock(&g_ctd_mutex);
	pthread_cond_signal(&g_ctd_cond);

	pthread_rwlock_unlock(&g_ctd_rwlock);

 	return 0;
}


/*******************************************************************
* 函数原型:int CTD_ReadRawData(void)
* 函数功能: 从CTD设备读取完整数据帧
* 参数说明:无
* 返回值:
*   成功返回0，失败返回-1
* 注意事项:
*   1. 会自动在数据末尾添加字符串结束符
*   2. 超时或错误时会清空缓冲区并写入"invalid"
********************************************************************/
int CTD_ReadRawData(void)
{
	/* 0.*入口检查	*/
    if(g_ctd_fd < 0 || g_ctd_readbuf == NULL || sizeof(g_ctd_readbuf) < g_ctdDataProtocol.length + 1)
	{
        return -1;
    }

	int state = 0;			//读取状态
	int nread = 0;		  //读取数据的总长度

	pthread_rwlock_wrlock(&g_ctd_rwlock);
    memset(g_ctd_readbuf, 0, sizeof(g_ctd_readbuf));

	while(nread < g_ctdDataProtocol.length)
	{
        /*	读取数据	*/
        char c;
        if(read(g_ctd_fd, &c, 1) != 1)
		{
			tcflush(g_ctd_fd, TCIFLUSH);
			perror("CTD_readData:read");
			break;
		}

        /*	按照协议读取数据	*/
        if(state == 0 && c == g_ctdDataProtocol.head_marker)
		{
            state = 1;		//开始读取
			g_ctd_readbuf[nread++] = c;	//把数据头写进数组
        }
		else if(state == 1)
		{
            if(c == g_ctdDataProtocol.tail_marker)
			{
				g_ctd_readbuf[nread++] = c;
                g_ctd_readbuf[nread] = '\0';
				if(nread == g_ctdDataProtocol.length)
				{
					pthread_rwlock_unlock(&g_ctd_rwlock);
					return 0;
				}
				else
				{
					g_ctd_readbuf[nread] = '\0';
					printf("CTD_readData:length error\n");
					break;
				}
            }
            g_ctd_readbuf[nread++] = c;
        }
    }
    
	/*	如果发生异常	*/
	memset(g_ctd_readbuf, 0, sizeof(g_ctd_readbuf));
    strcpy(g_ctd_readbuf, "invalid");
	pthread_rwlock_unlock(&g_ctd_rwlock);
    return -1;
}


 /*******************************************************************
 * 函数原型:int CTD_ParseData(char *ctdRawData)
 * 函数简介:解析CTD数据，并计算数据，把数据保存到g_ctdDataPack
 * 函数参数:无
 * 函数返回值:成功返回0，失败返回-1 
 *****************************************************************/
int CTD_ParseData(void)
{
	/*	0.入口检查	*/
	if(g_ctd_readbuf == NULL)
	{
		printf("CTD_parseData:g_ctd_readbuf NULL\n");
		return -1;
	}
	else if(strncmp(g_ctd_readbuf, "invalid", 7) == 0)
	{
		printf("CTD_parseData:原始数据无效\n");
		return -1;
	}

	char temp_tempature[16] = {0};
	char temp_pressure[16] = {0};
	char temp_conductivity[16] = {0};

	/*	1.解析数据	*/
	pthread_rwlock_wrlock(&g_ctd_rwlock);
	sscanf(g_ctd_readbuf, "%*[^T]T=%[0-9.Ee+-]%*[^P]P=%[0-9.Ee+-]%*[^C]C=%[0-9.Ee+-];\r\n",  temp_tempature, temp_pressure, temp_conductivity);
	sscanf(temp_tempature,"%lf", &g_ctdDataPack.temperature);
	sscanf(temp_pressure,"%lf", &g_ctdDataPack.pressure);
	sscanf(temp_conductivity,"%lf", &g_ctdDataPack.conductivity);

	/*	2.其他数值计算	*/
	//	2.1.盐度计算
	double Temp = g_ctdDataPack.temperature;
	double Cond = g_ctdDataPack.conductivity;
	double Pres = g_ctdDataPack.pressure;

	double p = (double)Pres;
	double t = (double)Temp * 1.00024;
	double R = (double)Cond / 42.914;
	
	double rt = c0 + c1 * t + c2 * t * t + c3 * t * t * t + c4 * t * t * t * t;
	double Rp = 1 + p * (e1 + e2 * p + e3 * p * p) / (1 + d1 * t + d2 * t * t + (d3 + d4 * t) * R);
	double Rt = R / (Rp * rt);
	double S = (t - 15) * (b0 + b1 * sqrt(Rt) + b2 * Rt + b3 * Rt * sqrt(Rt) + b4 * Rt * Rt + b5 * Rt * Rt * sqrt(Rt)) / (1 + k * (t - 15));
	double Salinity = a0 + a1 * sqrt(Rt) + a2 * Rt + a3 * Rt * sqrt(Rt) + a4 * Rt * Rt + a5 * Rt * Rt * sqrt(Rt) + S;

	g_ctdDataPack.salinity = Salinity;

	// 2.2.声速计算
	double A = A00 + A01 * t + A02 * t * t + A03 * t * t * t + A04 * t * t * t * t + (A10 + A11 * t + A12 * t * t + A13 * t * t * t + A14 * t * t * t * t) * p  + (A20 + A21 * t + A22 * t * t + A23 * t * t * t) * p*p + (A30 + A31 * t + A32 * t * t) *p*p*p;
	double Cw = C00 + C01 * t + C02 * t * t + C03 * t * t * t + C04 * t * t * t * t + C05 * t * t * t * t * t + (C10 + C11 * t + C12 * t * t + C13 * t * t * t + C14 * t * t * t * t) * p+ (C20 + C21 * t + C22 * t * t + C23 * t * t * t + C24 * t * t * t * t) * p * p + (C30 + C31 * t + C32 * t * t)*p * p * p;
	double B = B00 + B01 * t + (B10 + B11 * t) * p;
	double D = D00 + D10 *p;
	double SoundVelocity = Cw + A * Salinity + B * Salinity * sqrt(Salinity) + D * Salinity * Salinity; 	
	
	g_ctdDataPack.soundVelocity = SoundVelocity;

	// 2.3.密度计算
	double Rho0 = 999.842594 + 6.793952 / 100.0 * t-9.095290 / 1000.0 * t * t + 1.001685 / 10000.0 * t * t * t - 1.120083 / 1000000.0 * t * t * t * t + 6.536332/ 1000000000.0 * t * t * t * t * t;
	double Density = Rho0 + (8.24493 / 10.0 - 4.0899 / 1000 * t + 7.6438 / 100000.0 * t * t - 8.2467 / 10000000.0 * t * t * t + 5.3875 / 1000000000.0 * t * t * t * t) * Salinity + (-5.72466 / 1000.0 + 1.0227 / 10000 * t - 1.6546 / 1000000 * t * t) * Salinity * sqrt(Salinity) + 4.8314 / 10000.0 * Salinity * Salinity;
	
	g_ctdDataPack.density = Density;

	// 2.4.深度计算
	g_ctdDataPack.depth = g_ctdDataPack.pressure + 2.8;

	pthread_rwlock_unlock(&g_ctd_rwlock);
	
	return 0;	
}


/*******************************************************************
* 函数原型:double CTD_getTemperatureValue(void)
* 函数简介:获得温度数值。单位是摄氏度
* 函数参数:无
* 函数返回值: 返回temperature的数值。
*****************************************************************/ 
double CTD_getTemperatureValue(void)
{
	return g_ctdDataPack.temperature;
}


/*******************************************************************
* 函数原型:double CTD_getPressureValue(void)
* 函数简介:获得压力数值。单位是dbar。
* 函数参数:无
* 函数返回值: 返回pressure的数值。
*****************************************************************/ 
double CTD_getPressureValue(void)
{
	return g_ctdDataPack.pressure;
}


/*******************************************************************
* 函数原型:double CTD_getConductivityValue(void)
* 函数简介:获得电导率数值。单位是mS/cm。
* 函数参数:无
* 函数返回值: 返回conductivity的数值。
*****************************************************************/ 
double CTD_getConductivityValue(void)
{
	return g_ctdDataPack.conductivity;
}


/*******************************************************************
* 函数原型:double CTD_getDepthValue(void)
* 函数简介:获得深度数值。单位是m。
* 函数参数:无
* 函数返回值: 返回depth的数值。
*****************************************************************/ 
double CTD_getDepthValue(void)
{
	return g_ctdDataPack.depth;
}


/*******************************************************************
* 函数原型:double CTD_getSalinityValue(void)
* 函数简介:获得盐度数值。单位是PSU。
* 函数参数:无
* 函数返回值: 返回salinity的数值。
*****************************************************************/ 
double CTD_getSalinityValue(void)
{
	return g_ctdDataPack.salinity;
}


/*******************************************************************
* 函数原型:double CTD_getSoundVelocityValue(void)
* 函数简介:获得声速数值。单位是m/s。
* 函数参数:无
* 函数返回值: 返回soundVelocity的数值。
*****************************************************************/ 
double CTD_getSoundVelocityValue(void)
{
	return g_ctdDataPack.soundVelocity;
}


/*******************************************************************
* 函数原型:double CTD_getDensityValue(void)
* 函数简介:获得密度数值。单位是kg/m³。
* 函数参数:无
* 函数返回值: 返回density的数值。
*****************************************************************/ 
double CTD_getDensityValue(void)
{
	return g_ctdDataPack.density;
}


/*******************************************************************
* 函数原型:void CTD_printSensorData(void)
* 函数简介:打印全部数据
* 函数参数:无
* 函数返回值: 无
*****************************************************************/ 
void CTD_PrintAllData(void)
{	
	time_t now = time(NULL);	
	struct tm *nowtime = localtime(&now);
    printf("TIME: %d-%02d-%02d %02d:%02d:%02d\n",nowtime->tm_year + 1900,nowtime->tm_mon + 1,nowtime->tm_mday,\
    											nowtime->tm_hour,nowtime->tm_min,nowtime->tm_sec);
	printf("************CTD采集数据信息**************\n");
	printf("温度:%8.3lf ℃\n", CTD_getTemperatureValue());
	printf("压力:%8.3lf dbar\n", CTD_getPressureValue());
	printf("电导率:%8.3lf mS/cm\n", CTD_getConductivityValue());
	printf("深度:%8.3lf m \n", CTD_getDepthValue());
	printf("盐度:%8.3lf PSU\n", CTD_getSalinityValue());
	printf("声速:%8.3lf m/s\n", CTD_getSoundVelocityValue());
	printf("密度:%8.3lf kg/m³\n", CTD_getDensityValue());
	printf("************************************\n");
}


/*******************************************************************
* 函数原型:char *CTD_DataPackageProcessing(void)
* 函数简介:将CTD的数据进行按格式打包
* 函数参数:无
* 函数返回值: 成功返回打包好的数据地址
*****************************************************************/
char *CTD_DataPackageProcessing(void)
{
	static char ctdSendDataBuf[100] = {0};
	
	snprintf(ctdSendDataBuf, sizeof(ctdSendDataBuf),"#%8.3lf#%8.3lf#%8.3lf#%8.3lf#%8.3lf#%8.3lf#",\
	 g_ctdDataPack.temperature, g_ctdDataPack.conductivity, g_ctdDataPack.depth, \
	 g_ctdDataPack.salinity, g_ctdDataPack.soundVelocity, g_ctdDataPack.density);

	return ctdSendDataBuf;
}

