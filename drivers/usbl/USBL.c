/************************************************************************************
					文件名：USBL.c
					最后一次修改时间：2025/12/12
					修改内容：增加调试打印、过滤$41干扰数据
*************************************************************************************/

#include "USBL.h"
#include "../../sys/epoll/epoll_manager.h"
#include "../../sys/SerialPort/SerialPort.h"
#include "../thruster/Thruster.h"
/* 引入主控舱头文件，用于控制释放器 */
#include "../../drivers/maincabin/MainCabin.h" 

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
/* 工作状态    */
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
static char g_usbl_readbuf[128] = {0}; // 稍微加大接收缓冲区以防万一

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
	/* 0.入口检查 */
	if(g_usbl_fd < 0 || g_usbl_readbuf == NULL)
	{
		return -1;
	}

	pthread_rwlock_wrlock(&g_usbl_rwlock);
	memset(g_usbl_readbuf, 0, sizeof(g_usbl_readbuf));
		
	ssize_t nread = 0;
	int totalnByte = 0;
    char tmp_char = 0;

    /* 循环读取 */
	while(totalnByte < sizeof(g_usbl_readbuf) - 1)
	{
		nread = read(g_usbl_fd, &tmp_char, 1);
		if(nread > 0)
		{
            g_usbl_readbuf[totalnByte++] = tmp_char;
            
            // 快速结束判断：如果指令是以 '#' 结尾 (例如 "#UP$$01#")
            if(g_usbl_readbuf[0] == '#' && totalnByte > 5 && tmp_char == '#')
            {
                break; 
            }
            // 如果是以换行符结尾
            if(tmp_char == '\n')
            {
                break;
            }
		}
		else
		{
			// 超时无数据
			break;
		}
	}

    if(totalnByte == 0)
    {
        pthread_rwlock_unlock(&g_usbl_rwlock);
        return 0;
    }

	g_usbl_readbuf[totalnByte] = '\0';

    /* [调试] 打印所有收到的原始数据，用于分析 $41 等情况 */
	printf("[USBL DEBUG] 串口RAW: [%s] (len=%d)\n", g_usbl_readbuf, totalnByte);

	/* 数据初步校验 */
    int is_valid = 0;
    // 增加对 $41 的放行，以便在 ParseData 中统一处理（或者在这里直接放行所有 $ 开头的数据）
	if(g_usbl_readbuf[0] == '$' && g_usbl_readbuf[totalnByte-1] == '\n') is_valid = 1;
    else if(g_usbl_readbuf[0] == '#' && g_usbl_readbuf[totalnByte-1] == '#') is_valid = 1;
    else if(g_usbl_readbuf[0] == '#') is_valid = 1;

	if(is_valid == 0)
	{
        // 打印无效数据以便排查
		printf("[USBL WARN] 格式无效/丢弃: [%s]\n", g_usbl_readbuf);
		memset(g_usbl_readbuf, 0, sizeof(g_usbl_readbuf));
		tcflush(g_usbl_fd, TCIFLUSH);
		pthread_rwlock_unlock(&g_usbl_rwlock);
		return -1;
	}

	pthread_rwlock_unlock(&g_usbl_rwlock);
	return totalnByte;
}


 /*******************************************************************
 * 函数原型:int USBL_ParseData(void)
 * 函数简介:解析USBL接收到的数据
 * 函数参数:无
 * 函数返回值:成功返回0，失败返回-1 
 *****************************************************************/
int USBL_ParseData(void)
{
	/* 入口检查 */
	if(g_usbl_readbuf == NULL || strlen(g_usbl_readbuf) < 3)
	{
		return -1;
	}

	pthread_rwlock_wrlock(&g_usbl_rwlock);

    /* ==========================================================
     * 1. 过滤干扰数据 (如 $41 开头)
     * ========================================================== */
    if(strncmp(g_usbl_readbuf, "$41", 3) == 0)
    {
        // 可以在这里打印，也可以选择静默丢弃
        printf("[USBL INFO] 忽略 $41 非控制数据包\n");
        memset(g_usbl_readbuf, 0, sizeof(g_usbl_readbuf));
        pthread_rwlock_unlock(&g_usbl_rwlock);
        return 0;
    }

    /* ==========================================================
     * 2. 情况1：直接透传的明文控制指令 (例如: "#UP$$01#")
     * ========================================================== */
    if(g_usbl_readbuf[0] == '#')
    {
        printf("[USBL PARSE] 识别为直发指令: %s\n", g_usbl_readbuf);
        
        if(strstr(g_usbl_readbuf, "$$") != NULL)
        {
            char ctl_cmd[3] = {0}; 
            int ctl_arg = 0;

            sscanf(&g_usbl_readbuf[1], "%2s", ctl_cmd);
            sscanf(&g_usbl_readbuf[5], "%2d", &ctl_arg);
            
            printf("[USBL EXEC] 执行动作 -> CMD:%s ARG:%d\n", ctl_cmd, ctl_arg);
            Thruster_ControlHandle(ctl_cmd, ctl_arg);
        }
        else
        {
            printf("[USBL ERR] 指令格式错误 (无$$): %s\n", g_usbl_readbuf);
        }

        memset(g_usbl_readbuf, 0, sizeof(g_usbl_readbuf));
        pthread_rwlock_unlock(&g_usbl_rwlock);
        return 0;
    }

    /* ==========================================================
     * 3. 情况2：HEX数据包 ($61...) 包含 Payload
     * ========================================================== */
    static char lenC[3] = {0};     
    static int len = 0;
    static char temp[128] = {0};
    static unsigned char tempHexArrey[64] = {0};

    // 只有 $ 开头且长度足够才走这个逻辑 (避免越界)
    if(g_usbl_readbuf[0] == '$' && strlen(g_usbl_readbuf) > 38) 
    {
        memset(g_usbl_dataPack.recvdata, 0, sizeof(g_usbl_dataPack.recvdata));

        // 提取 HEX 长度
        lenC[0] = g_usbl_readbuf[37];
        lenC[1] = g_usbl_readbuf[38];
        lenC[2] = '\0';
        sscanf(lenC, "%x", &len); 

        // [调试] 打印解析出的长度
        printf("[USBL DEBUG] HEX数据长度 len: %d (0x%s)\n", len, lenC);

        if(len > 0 && 2*len < sizeof(temp)) 
        {
            // 复制 HEX 字符串
            strncpy(temp, g_usbl_readbuf + 39, 2 * len);
            temp[2 * len] = '\0'; // 确保结束符

            // [调试] 打印截取到的 HEX 串，检查位置是否正确
            printf("[USBL DEBUG] 截取HEX串: [%s]\n", temp);
            
            // HEX -> Char 转换
            for(int i = 0; i < 2*len; i = i + 2)
            {
                sscanf(&temp[i],"%2hhx", &tempHexArrey[i / 2]); 	
            }
            tempHexArrey[len] = '\0'; 

            memcpy(g_usbl_dataPack.recvdata, tempHexArrey, sizeof(tempHexArrey));
            
            // [调试] 打印最终解码出的 Payload (重点观察这里)
            printf("[USBL PAYLOAD] 解码明文: [%s]\n", tempHexArrey);

            /* A. 解析电机指令 */
            if(tempHexArrey[0] == '#' && tempHexArrey[7] == '#' && tempHexArrey[3] == '$' && tempHexArrey[4] == '$')
            {
                char ctl_cmd[3] = {'\0'}; 
                int ctl_arg = 0;
                sscanf((char *)(&tempHexArrey[1]), "%2s", ctl_cmd);
                sscanf((char *)(&tempHexArrey[5]), "%2d", &ctl_arg);
                
                printf("[USBL EXEC THRUSTER] 匹配成功 -> CMD:%s ARG:%d\n", ctl_cmd, ctl_arg);
                Thruster_ControlHandle(ctl_cmd, ctl_arg);
            } 
            /* B. 解析释放器指令 */
            else if(tempHexArrey[0] == '@')
            {
                char *payload_str = (char *)tempHexArrey;
                int action_taken = 0;

                if(strstr(payload_str, "open1") != NULL)
                {
                    printf("[USBL EXEC RELEASER] 匹配成功 -> 打开释放器1\n");
                    MainCabin_SwitchPowerDevice(Releaser1, 1);
                    action_taken = 1;
                }
                else if(strstr(payload_str, "close1") != NULL)
                {
                    printf("[USBL EXEC RELEASER] 匹配成功 -> 关闭释放器1\n");
                    MainCabin_SwitchPowerDevice(Releaser1, -1);
                    action_taken = 1;
                }
                else if(strstr(payload_str, "open2") != NULL)
                {
                    printf("[USBL EXEC RELEASER] 匹配成功 -> 打开释放器2\n");
                    MainCabin_SwitchPowerDevice(Releaser2, 1);
                    action_taken = 1;
                }
                else if(strstr(payload_str, "close2") != NULL)
                {
                    printf("[USBL EXEC RELEASER] 匹配成功 -> 关闭释放器2\n");
                    MainCabin_SwitchPowerDevice(Releaser2, -1);
                    action_taken = 1;
                }

                if(action_taken == 0)
                {
                     printf("[USBL WARN] 检测到 '@' 开头但未匹配到已知释放器指令: %s\n", payload_str);
                }
            }
            else
            {
                // [调试] 如果既不是 # 也不是 @，打印出来看看是什么
                printf("[USBL INFO] 收到 HEX 数据包，但非控制指令 (Payload: %s)\n", tempHexArrey);
            }
        }
        else
        {
             printf("[USBL ERR] 解析长度 len=%d 异常或过长，跳过解析\n", len);
        }
    }
    // [调试] 如果以 $ 开头但长度不足
    else if(g_usbl_readbuf[0] == '$')
    {
         printf("[USBL DEBUG] 收到短 $ 包 (len=%ld): %s (忽略)\n", strlen(g_usbl_readbuf), g_usbl_readbuf);
    }

    // 清理静态变量
	memset(lenC, 0, sizeof(lenC));
	memset(temp, 0, sizeof(temp));
	memset(tempHexArrey, 0, sizeof(tempHexArrey));
	len = 0;

	pthread_rwlock_unlock(&g_usbl_rwlock);
	return 0;	
}