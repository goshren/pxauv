/************************************************************************************
					文件名：DTU.c
					最后一次修改时间：2025/7/3
					修改内容：
*************************************************************************************/

#include "DTU.h"
#include "../../sys/SerialPort/SerialPort.h"
#include "../../sys/epoll/epoll_manager.h"
#include "../thruster/Thruster.h"
#include "../../drivers/maincabin/MainCabin.h"

/************************************************************************************
 									外部变量
*************************************************************************************/
extern int g_epoll_manager_fd;		//Epoll管理器
extern pthread_cond_t g_dtu_cond;
extern pthread_mutex_t g_dtu_mutex;
extern pthread_t g_tid_dtu;

/************************************************************************************
 									全局变量(可以extern的变量)
*************************************************************************************/
/*  工作状态    */
volatile int g_dtu_status = -1;			//-1为关闭串口，1为开启串口

/*	读取原始数据之后 存放的数组	*/
unsigned char g_dtu_recvbuf[MAX_DTU_RECV_DATA_SIZE] = {0};

/************************************************************************************
 									全局变量(仅可本文件使用)
*************************************************************************************/
/*	设备路径	*/
static char *g_dtu_deviceName = "/dev/radio";

/*	设备的文件描述符	*/
static int g_dtu_fd = -1;

/*	发送数据 存放的数组	*/
unsigned char g_dtu_sendbuf[MAX_DTU_SEND_DATA_SIZE] = {0};

/*	读写锁	*/
static pthread_rwlock_t g_dtu_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/*	旧的串口配置	*/
static struct termios g_dtu_oldSerialPortConfig = {0};

 /*******************************************************************
 * 函数原型:int DTU_getFD(void)
 * 函数简介:返回文件描述符
 * 函数参数:无
 * 函数返回值: 文件描述符数值
 *****************************************************************/
int DTU_getFD(void)
{
	return g_dtu_fd;
}

/*******************************************************************
 * 函数原型:int DTU_Init_default(void)
 * 函数简介:打开DTU串口设备，并且保存旧的配置
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int DTU_Init_default(void)
{
 	/* 1. 打开DTU设备 */
 	g_dtu_fd = open(g_dtu_deviceName, O_RDWR | O_NOCTTY | O_NDELAY);
    if(g_dtu_fd < 0) 
    {
        return -1;
    }
    
    /* 2.获取旧的串口的配置 */ 
    if(tcgetattr(g_dtu_fd, &g_dtu_oldSerialPortConfig))
    {
        close(g_dtu_fd);
        return -1;
    }
    
    /* 3.配置新的串口参数 */
    struct termios options = {0};
    options.c_cflag |= CLOCAL | CREAD; 		// 忽略调制解调器控制线并允许接收数据
    options.c_cflag &= ~PARENB; 			// 无奇偶校验
    options.c_cflag &= ~CSTOPB; 			// 1位停止位
    options.c_cflag &= ~CSIZE; 				// 清除数据位大小
    options.c_cflag |= CS8; 				// 8位数据位
    options.c_cflag &= ~CRTSCTS; 			// 关闭硬件流控制

    cfsetispeed(&options, B57600);			// 输入波特率
    cfsetospeed(&options, B57600);			// 输出波特率

    options.c_cc[VMIN] = 1;
	options.c_cc[VTIME] = 10;

    /* 4.写入配置、使配置生效 */
    if(tcsetattr(g_dtu_fd, TCSANOW, &options) < 0) 
    {
        close(g_dtu_fd);
        return -1;
    }
    
    return 0;
 }

 /*******************************************************************
 * 函数原型:int DTU_Init(void)
 * 函数简介:打开DTU串口设备，并且保存旧的配置
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int DTU_Init(void)
{
	/*	0.入口检查	*/
	if(g_dtu_deviceName == NULL || g_dtu_fd > 0)
	{
		return -1;
	}

	g_dtu_fd = -1;

	/*	1.打开串口	*/
    g_dtu_fd = SerialPort_open(g_dtu_deviceName, &g_dtu_oldSerialPortConfig);
	if(g_dtu_fd < 0)
	{
		return -1;
	}

	/*	2.配置串口	*/
	if(SerialPort_configBaseParams(g_dtu_fd, 57600,  1, 8, 'N') < 0)
	{
		close(g_dtu_fd);  // 关闭已打开的文件描述符
        g_dtu_fd = -1;
		return -1;
	}

	return 0;
}

 /*******************************************************************
 * 函数原型:ssize_t DTU_SendData(unsigned char *sendBuf, size_t bufsize)
 * 函数简介:DTU发送数据，发送之后会刷新缓存区
 * 函数参数:
 * 		   g_dtuSendBuf:要发送的数据
 * 		   bufsize:数组大小
 * 函数返回值: 发送成功返回发送的字节数，失败就返回-1
 *****************************************************************/
ssize_t DTU_SendData(unsigned char *sendBuf, size_t bufsize)
{
	/*	1.入口检查	*/
 	if(g_dtu_fd < 0 || sendBuf == NULL || bufsize <= 0 || bufsize > MAX_DTU_SEND_DATA_SIZE - 1)
 	{
 		return -1;
 	}

 	ssize_t nwrite = 0;

	pthread_rwlock_wrlock(&g_dtu_rwlock);

 	nwrite = write(g_dtu_fd, sendBuf, bufsize + 1);
 	if(nwrite < bufsize + 1)
	{
        perror("DTU_SendData:dtu write error");
        return -1;
    }
	pthread_rwlock_unlock(&g_dtu_rwlock);

    tcflush(g_dtu_fd, TCOFLUSH);
    
    return 0;
}
 
 /*******************************************************************
 * 函数原型:ssize_t DTU_RecvData(void)
 * 函数简介:DTU接收数据,在接收数据之前会清空dtuReadBuf,之后会刷新缓存区
 * 函数参数:无
 * 函数返回值: 成功返回读取到的字节个数，失败就返回-1
 *****************************************************************/ 
ssize_t DTU_RecvData(void)
{
	/*	1.入口检查	*/
 	if(g_dtu_fd < 0 || g_dtu_recvbuf == NULL || sizeof(g_dtu_recvbuf) <= 0 || sizeof(g_dtu_recvbuf)  > MAX_DTU_RECV_DATA_SIZE)
 	{
 		return -1;
 	}

	pthread_rwlock_wrlock(&g_dtu_rwlock);
	memset(g_dtu_recvbuf, 0, sizeof(g_dtu_recvbuf));
		
	ssize_t nread = 0;
	nread = read(g_dtu_fd, g_dtu_recvbuf, sizeof(g_dtu_recvbuf) - 1);
	if(nread < 0) 
	{
		return -1;
	}
	else
	{
		g_dtu_recvbuf[nread] = '\0';
	}

	pthread_rwlock_unlock(&g_dtu_rwlock);
	
	tcflush(g_dtu_fd, TCIFLUSH);
	return nread;
} 

 /*******************************************************************
 * 函数原型:void DTU_ParseData(void)
 * 函数简介:解析DTU接收的数据
 * 函数参数:无
 * 函数返回值: 无
 *****************************************************************/ 
void DTU_ParseData(void)
{
    int recvDataSize = strlen((char *)g_dtu_recvbuf); // 获取接收数据的长度

    /*		解析电机推进器指令 (#CMD$$)		*/
    if(g_dtu_recvbuf[0] == '#' && g_dtu_recvbuf[7] == '#' && g_dtu_recvbuf[3] == '$' && g_dtu_recvbuf[4] == '$')
    {
        char ctl_cmd[3] = {0}; 
        int ctl_arg = 0;

        sscanf((char *)&g_dtu_recvbuf[1], "%2s", ctl_cmd);
        sscanf((char *)&g_dtu_recvbuf[5], "%2d", &ctl_arg);
        Thruster_ControlHandle(ctl_cmd, ctl_arg);
    }
    /*		[新增] 解析释放器打开和关闭指令 (@cmd@)			*/
    else if(g_dtu_recvbuf[0] == '@' && g_dtu_recvbuf[recvDataSize-1] == '@')
    {
        // 判断具体的控制指令
        if(strstr((char *)g_dtu_recvbuf, "open1") != NULL)
        {
			int ret1 = MainCabin_SwitchPowerDevice(Releaser1, 1); // 获取返回值
    		if(ret1 == 0) {
        		printf("DTU指令成功: 释放器1已打开\n");
    			} else {
        	printf("DTU指令失败: 释放器1打开失败! (主控舱通信异常)\n");
    		}
        }
        else if(strstr((char *)g_dtu_recvbuf, "open2") != NULL)
        {
        	int ret2 = MainCabin_SwitchPowerDevice(Releaser2, 1);
            if(ret2 == 0) {
        		printf("DTU指令成功: 释放器1已关闭\n");
    			} else {
        	printf("DTU指令失败: 释放器1关闭失败! (主控舱通信异常)\n");
    		}
        }
        else if(strstr((char *)g_dtu_recvbuf, "close1") != NULL)
        {
            MainCabin_SwitchPowerDevice(Releaser1, -1);
            printf("DTU指令: 释放器1已关闭\n");
        }
        else if(strstr((char *)g_dtu_recvbuf, "close2") != NULL)
        {
            MainCabin_SwitchPowerDevice(Releaser2, -1);
            printf("DTU指令: 释放器2已关闭\n");
        }
    }
}


 /*******************************************************************
 * 函数原型: int DTU_Close(void)
 * 函数简介:关闭DTU的文件描述符
 * 函数参数:无
 * 函数返回值:成功返回0，失败返回-1
 *****************************************************************/
 int DTU_Close(void)
 {
 	/*	0.入口检查	*/
 	if(g_dtu_fd < 0)
 	{
 		return -1;
 	}
 	
	/*	1.关闭DTU	*/
	pthread_rwlock_wrlock(&g_dtu_rwlock);
	if(SerialPort_close(g_dtu_fd, &g_dtu_oldSerialPortConfig) < 0)
	{
		return -1;
	}

	/*	2.删除对应的监听	*/
	epoll_manager_del_fd(g_epoll_manager_fd, g_dtu_fd);

	/*3.更新文件描述符和工作状态(线程退出)*/
	g_dtu_fd = -1;
	g_dtu_status = -1;

	pthread_kill(g_tid_dtu, SIGKILL);

    printf("DTU工作线程已退出\n");
	g_tid_dtu = 0; 

	pthread_mutex_unlock(&g_dtu_mutex);
	pthread_cond_signal(&g_dtu_cond);

	pthread_rwlock_unlock(&g_dtu_rwlock);

 	return 0;
 }
 	

 
