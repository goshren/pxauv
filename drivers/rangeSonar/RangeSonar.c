/************************************************************************************
					文件名：RangeSonar.c
					最后一次修改时间：2025/6/29
					修改内容：
 ************************************************************************************/

#include "RangeSonar.h"
#include "../../sys/socket/TCP/tcp.h"
#include "../../sys/epoll/epoll_manager.h"


/************************************************************************************
 									全局变量(其他文件可使用)
*************************************************************************************/
/*	设备的文件描述符	*/
int g_rangeSonar_tcpclisock_fd = -1;						//作为客户端的文件描述符

/*	与服务器端连接的状态	*/
volatile int g_rangeSonar_status = -1;		//-1为未连接，1为已连接

/*	数据保存的结构体	*/
rangeSonarDataPack_t g_rangeSonar_data_pack = {
	.distanceFirstObstacle = 0
};

/************************************************************************************
 									全局变量(仅可本文件使用)
*************************************************************************************/
/*	读取原始数据之后 存放的数组	*/
static unsigned char g_rangeSonar_readbuf[32] = {0};

/*	读写锁	*/
static pthread_rwlock_t g_rangeSonar_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/*	主控舱数据协议	*/
static rangeSonarDataProtocol_t g_rangeSonar_data_protocol = {
	.length = 17,
	.timeout_ms = 200,
	.head_marker = {0xAB, 0xA0, 0x0D, 0x01}
};

/************************************************************************************
 									指令
*************************************************************************************/
/*	上电初始化指令	*/
static const unsigned char g_rangeSonar_cmd_init[6] = {0xAA, 0xA2, 0x00, 0x00, 0x00, 0x08};

/*******************************************************************
 * 函数原型:int RangeSonar_getFD(void)
 * 函数简介:返回文件描述符
 * 函数参数:无
 * 函数返回值: 文件描述符数值
 *****************************************************************/
int RangeSonar_getFD(void)
{
	return g_rangeSonar_tcpclisock_fd;
}

/*******************************************************************
* 函数原型:int RangeSonar_Init(void)
* 函数简介:主要进行TCP连接到测距声呐的服务器端。
* 函数参数:无
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int RangeSonar_Init(void)
{
	/*	1.初始化客户端	*/
	g_rangeSonar_tcpclisock_fd = TCP_InitClient(TCP_RANGESONAR_IP, TCP_RANGESONAR_PORT);
	if(g_rangeSonar_tcpclisock_fd < 0)
	{
		return -1;
	}

	/*	2.忽略SIGPIPE信号	*/
	signal(SIGPIPE, SIG_IGN);
	
	return 0;
}


/*******************************************************************
* 函数原型:int RangeSonar_Close(void)
* 函数简介:主要断开连接。
* 函数参数:无
* 函数返回值:无。
*****************************************************************/
int RangeSonar_Close(void)
{
	 /*	1.发送FIN包，通知对方不再发送数据	*/
    shutdown(g_rangeSonar_tcpclisock_fd, SHUT_WR);  // 关闭写端
    
    /*	2.完全关闭套接字	*/
    close(g_rangeSonar_tcpclisock_fd);
	g_rangeSonar_tcpclisock_fd = -1;

	return 0;
}


/*******************************************************************
* 函数原型:int RangeSonar_SendInitConfig(void)
* 函数简介:发送上电配置指令(必须执行一次)，回等待传感器回答。
* 函数参数:无
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int RangeSonar_SendInitConfig(void)
{
	/*	0.入口检查	*/
    if (g_rangeSonar_tcpclisock_fd < 0) {
        return -1;
    }

	/*	1.发送上电配置	*/
	int ret = TCP_SendData(g_rangeSonar_tcpclisock_fd, g_rangeSonar_cmd_init, sizeof(g_rangeSonar_cmd_init));
	if(ret < 0 || ret != sizeof(g_rangeSonar_cmd_init))
	{
		return -1;
	}

    return 0;
}


/*******************************************************************
* 函数原型:int RangeSonar_ReadRawData(void)
* 函数简介:读取一帧原始数据。
* 函数参数:无
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int RangeSonar_ReadRawData(void)
{
	/*	0.入口检查	*/
	if(g_rangeSonar_tcpclisock_fd < 0 || g_rangeSonar_readbuf == NULL)		//未连接 和 文件描述符错误
	{
		return -1;
	}
	//printf("ceji\n");

	/*	1.数据读取	*/
	memset(g_rangeSonar_readbuf, 0, sizeof(g_rangeSonar_readbuf));
	pthread_rwlock_wrlock(&g_rangeSonar_rwlock);
	int  nbyte = TCP_RecvData_Block(g_rangeSonar_tcpclisock_fd, g_rangeSonar_readbuf, sizeof(g_rangeSonar_readbuf),\
															17);
	
	/*	2.数据检查	*/
	//if(nbyte != g_rangeSonar_data_protocol.length )
	{
	//	goto rangeSonar_read_data_invalid;
	}
#if 0
	for(int i = 0; i < sizeof(g_rangeSonar_data_protocol.head_marker); i++)
	{
		if(g_rangeSonar_readbuf[i] != g_rangeSonar_data_protocol.head_marker[i])
		{
			goto rangeSonar_read_data_invalid;
		}
	}
	//printf("cejijieshu\n");
#endif	
	pthread_rwlock_unlock(&g_rangeSonar_rwlock);
	return 0;

rangeSonar_read_data_invalid:
	memset(g_rangeSonar_readbuf, 0, sizeof(g_rangeSonar_readbuf));
	strcpy((char *)g_rangeSonar_readbuf, "invalid");
	pthread_rwlock_unlock(&g_rangeSonar_rwlock);
	return -1;
}


/*******************************************************************
* 函数原型:int RangeSonar_ParseData(void)
* 函数简介:进行主控舱信息的解析，从原始数据中提取数据，之后进行计算，结果保存到数据结构体中。
* 函数参数:无
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int RangeSonar_ParseData(void)
{
	/*	0.入口检查	*/
	if(strncmp((const char *)g_rangeSonar_readbuf, "invalid", 7) == 0)
	{
		printf("RangeSonar_ParseData:原始数据无效\n");
		return -1;
	}

	int temp = 0;
	temp = (int)(g_rangeSonar_readbuf[4] * 8 + g_rangeSonar_readbuf[5]);

	/*	1.解析数据	*/
	pthread_rwlock_wrlock(&g_rangeSonar_rwlock);

	g_rangeSonar_data_pack.distanceFirstObstacle = temp;

	pthread_rwlock_unlock(&g_rangeSonar_rwlock);

	return 0;
}


/*******************************************************************
* 函数原型:int RangeSonar_getDistanceFirstObstacleValue(void)
* 函数简介:获得第一个障碍物距离，单位cm
* 函数参数:无
* 函数返回值: 返回distanceFirstObstacle的数值。
*****************************************************************/ 
int RangeSonar_getDistanceFirstObstacleValue(void)
{
	return g_rangeSonar_data_pack.distanceFirstObstacle;
}


/*******************************************************************
* 函数原型:char *RangeSonar_DataPackageProcessing(void)
* 函数简介:将RangeSonar的数据进行按格式打包
* 函数参数:无
* 函数返回值: 成功返回打包好的数据地址，失败返回NULL
*****************************************************************/
char *RangeSonar_DataPackageProcessing(void)
{	
	static char RangeSonarSendDataBuf[32] = {0};
		
	snprintf(RangeSonarSendDataBuf, sizeof(RangeSonarSendDataBuf), \
					"~%d~", g_rangeSonar_data_pack.distanceFirstObstacle);

	return RangeSonarSendDataBuf;
}


/*******************************************************************
* 函数原型:void RangeSonar_PrintSonarData(void)
* 函数简介:打印保存在数据结构体中的数据
* 函数参数:无
* 函数返回值: 无
*****************************************************************/ 	
void RangeSonar_PrintSonarData(void)
{	
	time_t now = time(NULL);	
	struct tm *nowtime = localtime(&now);
    printf("TIME: %d-%02d-%02d %02d:%02d:%02d\n",nowtime->tm_year + 1900,nowtime->tm_mon + 1,nowtime->tm_mday,\
    											nowtime->tm_hour,nowtime->tm_min,nowtime->tm_sec);
    										
    printf("************测距声呐数据信息**************\n");
    printf("第一个障碍物距离:%d cm\n", g_rangeSonar_data_pack.distanceFirstObstacle);
	printf("************************************\n");
}

