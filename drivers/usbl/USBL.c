/************************************************************************************
					文件名：USBL.c
					最后一次修改时间：2025/2/28
					修改内容：wending
*************************************************************************************/

#include "USBL.h"
#include "../../sys/epoll/epoll_manager.h"
#include "../../sys/SerialPort/SerialPort.h"
#include "../thruster/Thruster.h"

/************************************************************************************
 									外部变量
*************************************************************************************/
extern int g_epoll_manager_fd;		//Epoll管理器
extern pthread_cond_t g_usbl_cond;
extern pthread_mutex_t g_usbl_mutex;
extern pthread_t g_tid_usbl;

/************************************************************************************
 									全局变量(可以extern的变量)
*************************************************************************************/
/*  工作状态    */
volatile int g_usbl_status = -1;			//-1为不可工作，1为可以工作

/*	数据结构体	*/
usblDataPack_t g_usbl_dataPack = {
	.recvdata = {0}
};

/************************************************************************************
 									全局变量(仅可本文件使用)
*************************************************************************************/
/*	设备路径	*/
static char *g_usbl_deviceName = "/dev/ttyS1";

/*	设备的文件描述符	*/
static int g_usbl_fd = -1;

/*	旧的串口配置	*/
static struct termios g_usbl_oldSerialPortConfig = {0};

/*	读写锁	*/
static pthread_rwlock_t g_usbl_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/*	接收数据缓冲区	*/
static char g_usbl_readbuf[64] = {0};

 /*******************************************************************
 * 函数原型:int USBL_getFD(void)
 * 函数简介:返回文件描述符
 * 函数参数:无
 * 函数返回值: 文件描述符数值
 *****************************************************************/
int USBL_getFD(void)
{
	return g_usbl_fd;
}


 /*******************************************************************
 * 函数原型:int USBL_Init(void)
 * 函数简介:打开USBL串口设备，并且保存旧的配置
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int USBL_Init(void)
{
	/*	0.入口检查	*/
	if(g_usbl_deviceName == NULL || g_usbl_fd > 0)
	{
		return -1;
	}

	g_usbl_fd = -1;

	/*	1.打开串口	*/
    g_usbl_fd = SerialPort_open(g_usbl_deviceName, &g_usbl_oldSerialPortConfig);
	if(g_usbl_fd < 0)
	{
		return -1;
	}

	/*	2.配置串口	*/
	if(SerialPort_configBaseParams(g_usbl_fd, 115200,  1, 8, 'N') < 0)
	{
		close(g_usbl_fd);  // 关闭已打开的文件描述符
        g_usbl_fd = -1;
		return -1;
	}

	return 0;
}


 /*******************************************************************
 * 函数原型:int USBL_Close(void)
 * 函数简介:关闭USBL的文件描述符，调用SerialPort函数
 * 函数参数:无
 * 函数返回值:成功返回0，失败返回-1
 *****************************************************************/
int USBL_Close(void)
{
	/*	0.入口检查	*/
 	if(g_usbl_fd < 0)
 	{
 		return -1;
 	}
 	
	/*	1.关闭USBL	*/
	pthread_rwlock_wrlock(&g_usbl_rwlock);
	if(SerialPort_close(g_usbl_fd, &g_usbl_oldSerialPortConfig) < 0)
	{
		return -1;
	}

	/*	2.删除对应的监听	*/
	epoll_manager_del_fd(g_epoll_manager_fd, g_usbl_fd);

	/*3.更新文件描述符和工作状态(线程退出)*/
	g_usbl_fd = -1;
	g_usbl_status = -1;

	pthread_kill(g_tid_usbl, SIGKILL);
	g_tid_usbl = 0;

	//pthread_cancel(g_tid_usbl);
	//pthread_join(g_tid_usbl, NULL);

    printf("USBL工作线程已退出\n");

	pthread_mutex_unlock(&g_usbl_mutex);
	pthread_cond_signal(&g_usbl_cond);

	pthread_rwlock_unlock(&g_usbl_rwlock);

 	return 0;
}


/*******************************************************************
* 函数原型:ssize_t USBL_ReadRawData(void)
* 函数简介:USBL从串口读取数据
* 函数参数:无
* 函数返回值: 成功返回读取到的字节个数，失败就返回-1
*****************************************************************/ 
ssize_t USBL_ReadRawData(void)
{
	/*	0.入口检查	*/
	if(g_usbl_fd < 0 || g_usbl_readbuf == NULL)
	{
		return -1;
	}

	pthread_rwlock_wrlock(&g_usbl_rwlock);
	memset(g_usbl_readbuf, 0, sizeof(g_usbl_readbuf));
		
	ssize_t nread = 0;
	int totalnByte = 0;

	while(totalnByte < 63)
	{
		nread = read(g_usbl_fd, g_usbl_readbuf + totalnByte, 1);
		if(nread < 0)
		{
			memset(g_usbl_readbuf, 0, sizeof(g_usbl_readbuf));
			printf("usbl read err nread < 0\r\n");
			break;
		}
		totalnByte += nread;
	}

	g_usbl_readbuf[totalnByte] = '\0';

	//printf("usbl read:%s\r\n", g_usbl_readbuf);

	/*	数据审查	*/
	if(g_usbl_readbuf[0] != '$' && g_usbl_readbuf[62] != '\n')
	{
		memset(g_usbl_readbuf, 0, sizeof(g_usbl_readbuf));
		strcpy(g_usbl_readbuf, "invalid");
		printf("usbl data invalid\r\n");
		tcflush(g_usbl_fd, TCIFLUSH);
		pthread_rwlock_unlock(&g_usbl_rwlock);
		return -1;
	}

	pthread_rwlock_unlock(&g_usbl_rwlock);
	return nread;
}


 /*******************************************************************
 * 函数原型:int USBL_ParseData(void)
 * 函数简介:解析USBL接收到的数据
 * 函数参数:无
 * 函数返回值:成功返回0，失败返回-1 
 *****************************************************************/
int USBL_ParseData(void)
{
	/*	入口检查	*/
	if(g_usbl_readbuf == NULL || (strncmp(g_usbl_readbuf, "invaild", 7) == 0))
	{
		return -1;
	}

/*char s[64] = {"$61010F80040000000000000000EB391E050003637070FF0216"};*/

	/*	数据解析	*/
    static char lenC[3] = {0};     
    static int len = 0;
    static char temp[72] = {0};
    static unsigned char tempHexArrey[36] = {0};

	pthread_rwlock_wrlock(&g_usbl_rwlock);
    memset(g_usbl_dataPack.recvdata, 0, sizeof(g_usbl_dataPack.recvdata));

    lenC[0] = g_usbl_readbuf[37];
    lenC[1] = g_usbl_readbuf[38];
    lenC[2] = '\0';
    sscanf(lenC, "%x", &len);		//16进制数据的长度

	strncpy(temp, g_usbl_readbuf + 39, 2 * len);	//16进制的ASCII字符串复制到temp
    
    for(int i = 0; i < 2*len; i = i + 2)
    {
        sscanf(&temp[i],"%2hhx",&tempHexArrey[i / 2]);		//翻译成16进制数据	
    }
    
    memcpy(g_usbl_dataPack.recvdata, tempHexArrey, sizeof(tempHexArrey));
	printf("usbl parse data:%16s\r\n", tempHexArrey);

	pthread_rwlock_unlock(&g_usbl_rwlock);

	/*		解析电机驱动		*/
    if(tempHexArrey[0] == '#' && tempHexArrey[7] == '#' && tempHexArrey[3] == '$' && tempHexArrey[4] == '$')	//指令判断处理
    {
        char ctl_cmd[3] = {'\0'}; 
        int ctl_arg = 0;

        sscanf((char *)(&tempHexArrey[1]), "%2s", ctl_cmd);
        sscanf((char *)(&tempHexArrey[5]), "%2d", &ctl_arg);
        Thruster_ControlHandle(ctl_cmd, ctl_arg);
    }    

	memset(lenC, 0, sizeof(lenC));
	memset(temp, 0, sizeof(temp));
	memset(tempHexArrey, 0, sizeof(tempHexArrey));
	len = 0;

	return 0;	
}






