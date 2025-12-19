/************************************************************************************
					文件名：MainCabin.c
					最后一次修改时间：2025/6/29
					修改内容：
 ************************************************************************************/

#include "MainCabin.h"

#include "../../task/task_thread.h"

#include "../../sys/socket/TCP/tcp.h"
#include "../../tool/tool.h"

#include "../ctd/CTD.h"
#include "../dvl/DVL.h"
#include "../dtu/DTU.h"
#include "../usbl/USBL.h"
#include "../sonar/Sonar.h"
#include "../../sys/epoll/epoll_manager.h"

/************************************************************************************
 									全局变量(其他文件可使用)
*************************************************************************************/
/*	设备的文件描述符	*/
int g_maincabin_tcpclisock_fd = -1;						//作为客户端的文件描述符

extern int g_epoll_manager_fd; // 引用外部变量

/*	与服务器端连接的状态	*/
volatile int g_maincabin_tcpcliConnectFlag = -1;		//-1为未连接，1为已连接

/*	与设备上电状态状态	*/
volatile int g_ctd_power_flag = -1;		//-1为未连接，1为已连接
volatile int g_dvl_power_flag = -1;		//-1为未连接，1为已连接
volatile int g_dtu_power_flag = -1;		//-1为未连接，1为已连接
volatile int g_sonar_power_flag = -1;		//-1为未连接，1为已连接
volatile int g_usbl_power_flag = -1;		//-1为未连接，1为已连接
volatile int g_releaser1_power_flag = -1;		//-1为未连接，1为已连接
volatile int g_releaser2_power_flag = -1;		//-1为未连接，1为已连接

/*	数据保存的结构体	*/
maincabinDataPack_t g_maincabin_data_pack = {
	.temperature = 0.0f,
	.pressure = 0.0f,
	.humidity = 0.0f,
	.isLeak01= {"01GOOD"},
	.isLeak02 = {"02GOOD"},
	.deviceState = {"DEVCLOS"},
	.releaser1State = {"R1OPEN"},
	.releaser2State = {"R2OPEN"}
};

/*	工作线程tid	*/
pthread_t g_tid_ctd = 0;
pthread_t g_tid_dvl = 0;
pthread_t g_tid_usbl = 0;
pthread_t g_tid_dtu = 0;
pthread_t g_tid_sonar = 0;

/************************************************************************************
 									全局变量(仅可本文件使用)
*************************************************************************************/
/*	读取原始数据之后 存放的数组	*/
static unsigned char g_maincabin_readbuf[128] = {0};

/*	读写锁	*/
static pthread_rwlock_t g_maincabin_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/*	主控舱数据协议	*/
static maincabinDataProtocol_t g_maincabin_data_protocol = {
	.length = 117,
	.timeout_ms = 200
};


/************************************************************************************
 									指令
*************************************************************************************/
const unsigned char g_control_power_cmd[7][2][13] = 
{
	/*	1.供电/断电CTD	*/
	{
		{0x08, 0x00, 0x00, 0x00, 0xC0, 0x04, 0x80, 0xFF, 0x00, 0x00, 0x00, 0x00,0x00},		//CTD供电
		{0x08, 0x00, 0x00, 0x00, 0xC0, 0x04, 0x80, 0x00, 0xFF, 0x00, 0x00, 0x00,0x00}		//CTD断电
	},

	/*	2.供电/断电DVL	*/
	{
		{0x08, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x04, 0xFF, 0x00, 0x00, 0x00, 0x00,0x00},		//DVL供电
		{0x08, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x04, 0x00, 0xFF, 0x00, 0x00, 0x00,0x00} 		//DVL断电
	},

	/*	3.供电/断电USBL	*/
	{
		{0x08, 0x00, 0x00, 0x00, 0xC0, 0x04, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,0x00},
		{0x08, 0x00, 0x00, 0x00, 0xC0, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,0x00}	
	},

	/*	4.供电/断电数传电台Radio	*/
	{
		{0x08, 0x00, 0x00, 0x00, 0xC0, 0x01, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,0x00},
		{0x08, 0x00, 0x00, 0x00, 0xC0, 0x01, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,0x00}
	},

	/*	5.供电/断电Sonar	*/
	{
		{0x08, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x40, 0xFF, 0x00, 0x00, 0x00, 0x00,0x00},
		{0x08, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x40, 0x00, 0xFF, 0x00, 0x00, 0x00,0x00}
	},

	/*	6.供电/断电Releaser1	*/
	{
		{0x08, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x01, 0xFF, 0x00, 0x00, 0x00, 0x00,0x00},
		{0x08, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x01, 0x00, 0xFF, 0x00, 0x00, 0x00,0x00}
	},

	/*	7.供电/断电Releaser2	*/
	{
		{0x08, 0x00, 0x00, 0x00, 0xC0, 0x02, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,0x00},
		{0x08, 0x00, 0x00, 0x00, 0xC0, 0x02, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,0x00}
	}
};


 /*******************************************************************
 * 函数原型:int  MainCabin_getFD(void)
 * 函数简介:返回文件描述符
 * 函数参数:无
 * 函数返回值: 文件描述符数值
 *****************************************************************/
int  MainCabin_getFD(void)
{
    return g_maincabin_tcpclisock_fd;
}


/*******************************************************************
* 函数原型:int MainCabin_Init(void)
* 函数简介:主要进行TCP连接到主控舱的服务器端。
* 函数参数:无
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int MainCabin_Init(void)
{
	/*	1.初始化客户端	*/
	g_maincabin_tcpclisock_fd = TCP_InitClient(TCP_MAINCABIN_IP, TCP_MAINCABIN_PORT);
	if(g_maincabin_tcpclisock_fd < 0)
	{
		return -1;
	}

	/*	2.忽略SIGPIPE信号	*/
	signal(SIGPIPE, SIG_IGN);

	/*	3.更新连接状态	*/
	g_maincabin_tcpcliConnectFlag = 1;
	
	return 0;
}


/* 重连处理函数 */
int MainCabin_ReConnect(void)
{
    // 1. 关闭旧的 socket
    if(g_maincabin_tcpclisock_fd > 0)
    {
        // 从 epoll 中移除
        epoll_manager_del_fd(g_epoll_manager_fd, g_maincabin_tcpclisock_fd);
        close(g_maincabin_tcpclisock_fd);
        g_maincabin_tcpclisock_fd = -1;
    }
    
    g_maincabin_tcpcliConnectFlag = -1; // 标记为未连接

    printf("[MainCabin] 尝试重连服务器 %s:%d ...\n", TCP_MAINCABIN_IP, TCP_MAINCABIN_PORT);

    // 2. 重新建立连接
    g_maincabin_tcpclisock_fd = TCP_InitClient(TCP_MAINCABIN_IP, TCP_MAINCABIN_PORT);
    if(g_maincabin_tcpclisock_fd < 0)
    {
        return -1; // 重连失败
    }

    // 3. 重新加入 Epoll 监听
    if(epoll_manager_add_fd(g_epoll_manager_fd, g_maincabin_tcpclisock_fd, EPOLLIN) < 0)
    {
        close(g_maincabin_tcpclisock_fd);
        return -1;
    }

    g_maincabin_tcpcliConnectFlag = 1; // 标记为已连接
    printf("[MainCabin] 重连成功！\n");
    return 0;
}


/*******************************************************************
* 函数原型:int MainCabin_SwitchPowerDevice(MainCabin_Control_DeviceID id, int power)
* 函数简介:供电/断电某个设备。
* 函数参数:id:MainCabin_Control_DeviceID中数值。其他数值无效
* 函数参数:power:1为供电，-1为断电。其他数值无效。
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int MainCabin_SwitchPowerDevice(MainCabin_Control_DeviceID id, int power)
{
	if(g_maincabin_tcpcliConnectFlag != 1) return -1; // 如果没连接，直接返回失败
	/*	0.入口检查	*/
	if(g_maincabin_tcpcliConnectFlag != 1 || g_maincabin_tcpclisock_fd < 0 || (power != -1 && power != 1) || (id < 0 || id > 6))
	{
		return -1;
	}

	int sendtry = 3;
	int ret = -1;

	volatile int *temp = 0;
	switch(id)
	{
		case CTD:
			temp = &g_ctd_power_flag;
			break;

		case DVL:
			temp = &g_dvl_power_flag;
			break;

		case Radio:
			temp = &g_dtu_power_flag;
			break;

		case Sonar:
			temp = &g_sonar_power_flag;
			break;

		case USBL:
			temp = &g_usbl_power_flag;
			break;

		case Releaser1:
			temp = &g_releaser1_power_flag;
			break;

		case Releaser2:
			temp = &g_releaser2_power_flag;
			break;

		default:
			return -1;
	}

	/*	1.发送供电/断电的指令	*/
	if(power == 1)		// 供电
	{
		while(sendtry--)
		{
			ret = TCP_SendData(g_maincabin_tcpclisock_fd, g_control_power_cmd[id][0], sizeof(g_control_power_cmd[0][0]));
			if(ret < 0) 
    		{
        	// [新增] 发送失败也认为是断开
       		 printf("[MainCabin] 发送指令失败，连接可能已断开\n");
        	 g_maincabin_tcpcliConnectFlag = -1; 
        	 return -1;
    		}
			if(ret == sizeof(g_control_power_cmd[0][0]))
			{
				*temp = 1;
				if(id == Releaser1) strcpy(g_maincabin_data_pack.releaser1State,"R1OPEN");
				if(id == Releaser2) strcpy(g_maincabin_data_pack.releaser2State,"R2OPEN");
				return 0; //发送成功
			}
			else
			{
				continue;	//重发
			}
			usleep(100000);
		}
	}
	else if(power == -1)
	{
		while(sendtry--)
		{
			ret = TCP_SendData(g_maincabin_tcpclisock_fd, g_control_power_cmd[id][1], sizeof(g_control_power_cmd[0][0]));
			if(ret < 0) 
    		{
        	// [新增] 发送失败也认为是断开
       		 printf("[MainCabin] 发送指令失败，连接可能已断开\n");
        	 g_maincabin_tcpcliConnectFlag = -1; 
        	 return -1;
    		}
			if(ret == sizeof(g_control_power_cmd[0][0]))
			{
				*temp = -1;
				if(id == Releaser1) strcpy(g_maincabin_data_pack.releaser1State,"R1CLOSE");
				if(id == Releaser2) strcpy(g_maincabin_data_pack.releaser2State,"R2CLOSE");
				return 0; //发送成功
			}
			else
			{
				continue;	//重发
			}
			usleep(100000);
		}
	}

	*temp = -1;

	return -1;
}


/*******************************************************************
* 函数原型:int MainCabin_PowerOnAllDeviceExceptReleaser(void)
* 函数简介:供电CTD、DTU、DVL、USBL、Sonar设备。
* 函数参数:无
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int MainCabin_PowerOnAllDeviceExceptReleaser(void)
{
	for(int id = CTD; id <= Sonar; id++)
	{
		if(MainCabin_SwitchPowerDevice(id, 1) < 0) 
		{
			return -1;
		}
		usleep(200000);
	}

	strcpy(g_maincabin_data_pack.deviceState, "DEVOPEN");

	/*	温盐深初始化	*/
	if(g_ctd_power_flag == 1)
	{
		pthread_t tid;
		pthread_create(&tid, NULL, (void *)MainCabin_CTD_PowerOnHandle, NULL);
		g_tid_ctd = tid;
	}

	/*	DVL初始化	*/
	if(g_dvl_power_flag == 1)
	{
		pthread_t tid;
		pthread_create(&tid, NULL, (void *)MainCabin_DVL_PowerOnHandle, NULL);
		g_tid_dvl = tid;
	}

	/*	数传电台	*/
	if(g_dtu_power_flag == 1)
	{
		pthread_t tid;
		pthread_create(&tid, NULL, (void *)MainCabin_DTU_PowerOnHandle, NULL);
		g_tid_dtu = tid;
	}

	/*	USBL	*/
	if(g_usbl_power_flag == 1)
	{
		pthread_t tid;
		pthread_create(&tid, NULL, (void *)MainCabin_USBL_PowerOnHandle, NULL);
		g_tid_usbl = tid;
	}

	/*	Sonar	*/
	/*
	if(g_sonar_power_flag == 1)
	{
		pthread_t tid;
		pthread_create(&tid, NULL, (void *)MainCabin_Sonar_PowerOnHandle, NULL);
		g_tid_sonar = tid;
	}
	*/

	return 0;
}


/*******************************************************************
* 函数原型:int MainCabin_PowerOffAllDeviceExceptReleaser(void)
* 函数简介:断电CTD、DTU、DVL、USBL、Sonar设备。
* 函数参数:无
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int MainCabin_PowerOffAllDeviceExceptReleaser(void)
{
	for(int id = CTD; id <= Sonar; id++)
	{
		if(MainCabin_SwitchPowerDevice(id, -1) < 0) 
		{
			return -1;
		}
		usleep(200000);
	}

	strcpy(g_maincabin_data_pack.deviceState, "DEVCLOS");

	printf("传感器已经全部断电\n");

	/*	CTD	*/
	if(g_ctd_power_flag == -1)
	{
		CTD_Close();
		printf("CTD线程已关闭\n");
	}

	/*	USBL	*/
	if(g_usbl_power_flag == -1)
	{
		USBL_Close();
		printf("USBL线程已关闭\n");
	}

	/*	数传电台	*/
	if(g_dtu_power_flag == -1)
	{
		DTU_Close();
		printf("DTU线程已关闭\n");
	}

	/*	Sonar	*/
	if(g_sonar_power_flag == -1)
	{
		Sonar_Close();
		printf("Sonar线程已关闭\n");
	}

	/*	DVL	*/
	if(g_dvl_power_flag == -1)
	{
		DVL_Close();
		printf("DVL线程已关闭\n");
	}

	return 0;
}


/*******************************************************************
* 函数原型:int MainCabin_ReadRawData(void)
* 函数简介:读取一帧原始数据。
* 函数参数:无
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int MainCabin_ReadRawData(void)
{
    // ... 原有入口检查 ...
    if(g_maincabin_tcpcliConnectFlag != 1 || g_maincabin_tcpclisock_fd < 0) return -1;

    memset(g_maincabin_readbuf, 0, sizeof(g_maincabin_readbuf));
    pthread_rwlock_wrlock(&g_maincabin_rwlock);
    
    // 调用接收
    int nbyte = TCP_RecvData_Block(g_maincabin_tcpclisock_fd, g_maincabin_readbuf, sizeof(g_maincabin_readbuf), g_maincabin_data_protocol.length);

    // [关键修改] 如果接收失败（返回-1）或 长度不对
    if(nbyte != g_maincabin_data_protocol.length)
    {
        printf("[MainCabin] 读取错误或连接断开 (nbyte=%d)\n", nbyte);
        
        // 标记连接断开，以便下次循环触发重连
        g_maincabin_tcpcliConnectFlag = -1; 
        
        pthread_rwlock_unlock(&g_maincabin_rwlock);
        return -1;
    }
    pthread_rwlock_unlock(&g_maincabin_rwlock);
    return 0;
}


/*******************************************************************
* 函数原型:int MainCabin_ParseData(void)
* 函数简介:进行主控舱信息的解析，从原始数据中提取数据，之后进行计算，结果保存到数据结构体中。
* 函数参数:无
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int MainCabin_ParseData(void)
{
	/*	0.入口检查	*/
	if(strncmp((const char *)g_maincabin_readbuf, "invalid", 7) == 0)
	{
		printf("CTD_parseData:原始数据无效\n");
		return -1;
	}

	static unsigned short humidityU16 = 0;
	static unsigned short temperatureU16 = 0;
	static unsigned int pressureU32 = 0;
	static unsigned char humidityUArray[2] = {0};
	static unsigned char temperatureUArray[2] = {0};
	static unsigned char pressureUArray[4] = {0};
	static unsigned char isLeak01[8] = {0};
	static unsigned char isLeak02[8] = {0};

	/*	1.解析数据	*/
	/*原始数据的总长度是117，其中每一个数据信息长度13，在原始数据的位置不确定，所以需要此操作*/
	for(int i = 0; i < 9; i++)
	{
		/*		提取泄露检测01		*/
		if(g_maincabin_readbuf[i*13 + 3] == 0X01 && g_maincabin_readbuf[i*13 + 4] == 0X80)
			memcpy(isLeak01, &g_maincabin_readbuf[i*13 + 5], 4);

		/*		提取泄露检测02		*/
		else if(g_maincabin_readbuf[i*13 + 3] == 0X01 && g_maincabin_readbuf[i*13 + 4] == 0X88)
			memcpy(isLeak02, &g_maincabin_readbuf[i*13 + 5], 4);

		/*		提取气压信息		*/
		else if(g_maincabin_readbuf[i*13 + 3] == 0X02 && g_maincabin_readbuf[i*13 + 4] == 0X00)
			memcpy(pressureUArray, &g_maincabin_readbuf[i*13 + 5], 4);
		
		/*		提取温湿度数据		*/
		else if(g_maincabin_readbuf[i*13 + 3] == 0X02 && g_maincabin_readbuf[i*13 + 4] == 0X80)
		{
			memcpy(humidityUArray, &g_maincabin_readbuf[i*13 + 9], 2);
			memcpy(temperatureUArray, &g_maincabin_readbuf[i*13 + 11], 2);
		}
	}
	
	pressureU32 = pressureUArray[0] << 24 | pressureUArray[1] << 16 | pressureUArray[2] << 8 |pressureUArray[3];
	humidityU16 = humidityUArray[0] << 8 | humidityUArray[1];
	temperatureU16 = temperatureUArray[0] << 8 | temperatureUArray[1];

	pthread_rwlock_wrlock(&g_maincabin_rwlock);
	g_maincabin_data_pack.pressure  = Tool_parseIEEE754(pressureU32);
	g_maincabin_data_pack.humidity = (humidityU16 * 100.0f) / 65535.0;
	g_maincabin_data_pack.temperature = ((temperatureU16 * 175.0f) / 65535.0) - 45;
	
	memset(g_maincabin_data_pack.isLeak01, 0,sizeof(g_maincabin_data_pack.isLeak01));
	memset(g_maincabin_data_pack.isLeak02, 0,sizeof(g_maincabin_data_pack.isLeak02));

	if(isLeak01[0] == 0x00 && isLeak01[1] == 0x00 && isLeak01[2] == 0xFF && isLeak01[3] == 0xFF)
	{
		strcpy(g_maincabin_data_pack.isLeak01, "01LEAK");
	}
	else
	{
		strcpy(g_maincabin_data_pack.isLeak01, "01GOOD");
	}
	
	if(isLeak02[0] == 0x00 && isLeak02[1] == 0x00 && isLeak02[2] == 0xFF && isLeak02[3] == 0xFF)
	{
		strcpy(g_maincabin_data_pack.isLeak02, "01LEAK");
	}
	else
	{
		strcpy(g_maincabin_data_pack.isLeak02, "01GOOD");
	}

	pthread_rwlock_wrlock(&g_maincabin_rwlock);

	return 0;
}	


/*******************************************************************
* 函数原型:char *MainCabin_DataPackageProcessing(void)
* 函数简介:将MainCabin的数据进行按格式打包
* 函数参数:无
* 函数返回值: 成功返回打包好的数据地址，失败返回NULL
*****************************************************************/
char *MainCabin_DataPackageProcessing(void)
{	
	static char maincabinSendDataBuf[100] = {0};
		
	snprintf(maincabinSendDataBuf, sizeof(maincabinSendDataBuf), \
					"@%f@%f@%f@%s@%s@%s@%s@%s@", \
					g_maincabin_data_pack.temperature, g_maincabin_data_pack.humidity, g_maincabin_data_pack.pressure, \
				g_maincabin_data_pack.isLeak01, g_maincabin_data_pack.isLeak02, \
				g_maincabin_data_pack.deviceState, g_maincabin_data_pack.releaser1State, g_maincabin_data_pack.releaser2State);
		
	return maincabinSendDataBuf;
}
	

/*******************************************************************
* 函数原型:void MainCabin_PrintSensorData(void)
* 函数简介:打印保存在数据结构体中的数据
* 函数参数:无
* 函数返回值: 无
*****************************************************************/ 	
void MainCabin_PrintSensorData(void)
{	
	time_t now = time(NULL);	
	struct tm *nowtime = localtime(&now);
    printf("TIME: %d-%02d-%02d %02d:%02d:%02d\n",nowtime->tm_year + 1900,nowtime->tm_mon + 1,nowtime->tm_mday,\
    											nowtime->tm_hour,nowtime->tm_min,nowtime->tm_sec);
    										
    printf("************主控舱数据信息**************\n");
    printf("温度:%f ℃\n", g_maincabin_data_pack.temperature);
	printf("湿度:%f %%\n", g_maincabin_data_pack.humidity);
	printf("气压:%f hPa\n", g_maincabin_data_pack.pressure);
	printf("泄露01:%s \n", g_maincabin_data_pack.isLeak01);
	printf("泄露02:%s \n", g_maincabin_data_pack.isLeak02);
	printf("传感器设备上电:%s \n", g_maincabin_data_pack.deviceState);
	printf("释放器1:%s \n", g_maincabin_data_pack.releaser1State);
	printf("释放器2:%s \n", g_maincabin_data_pack.releaser2State);
	printf("************************************\n");
}

/*******************************************************************
* 函数原型:void *MainCabin_CTD_PowerOnHandle(void *arg)
* 函数简介:CTD的上电初始化线程
* 函数参数:无
* 函数返回值: 无
*****************************************************************/ 	
void *MainCabin_CTD_PowerOnHandle(void *arg)
{
	printf("CTD上电初始化\n");
	sleep(3);

	if(Task_CTD_Init() < 0)
	{
		printf("CTD上电初始化失败\n");
		return NULL;
	}

	printf("CTD初始化完毕......\n");

	return NULL;
}

/*******************************************************************
* 函数原型:void *MainCabin_DVL_PowerOnHandle(void *arg)
* 函数简介:CTD的上电初始化线程
* 函数参数:无
* 函数返回值: 无
*****************************************************************/ 	
void *MainCabin_DVL_PowerOnHandle(void *arg)
{
	printf("DVL上电初始化, 需要初始化10秒\n");
	sleep(10);

	if(Task_DVL_Init() < 0)
	{
		printf("DVL上电初始化失败\n");
		return NULL;
	}

	printf("DVL初始化完毕......\n");

	return NULL;
}

/*******************************************************************
* 函数原型:void *MainCabin_DTU_PowerOnHandle(void *arg)
* 函数简介:数传电台的上电初始化线程
* 函数参数:无
* 函数返回值: 无
*****************************************************************/ 	
void *MainCabin_DTU_PowerOnHandle(void *arg)
{
	printf("数传电台上电初始化\n");
	sleep(3);

	if(Task_DTU_Init() < 0)
	{
		printf("数传电台上电初始化失败\n");
		return NULL;
	}

	printf("数传电台初始化完毕......\n");

	return NULL;
}

/*******************************************************************
* 函数原型:void *MainCabin_USBL_PowerOnHandle(void *arg)
* 函数简介:USBL的上电初始化线程
* 函数参数:无
* 函数返回值: 无
*****************************************************************/ 	
void *MainCabin_USBL_PowerOnHandle(void *arg)
{
	printf("USBL上电初始化\n");
	sleep(3);

	if(Task_USBL_Init() < 0)
	{
		printf("USBL上电初始化失败\n");
		return NULL;
	}

	printf("USBL初始化完毕......\n");

	return NULL;
}

/*******************************************************************
* 函数原型:void *MainCabin_Sonar_PowerOnHandle(void *arg)
* 函数简介:Sonar的上电初始化线程
* 函数参数:无
* 函数返回值: 无
*****************************************************************/ 	
void *MainCabin_Sonar_PowerOnHandle(void *arg)
{
	printf("Sonar上电初始化\n");
	sleep(3);

	if(Task_Sonar_Init() < 0)
	{
		printf("Sonar上电初始化失败\n");
		return NULL;
	}

	printf("Sonar初始化完毕......\n");

	return NULL;
}


