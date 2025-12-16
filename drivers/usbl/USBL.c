/************************************************************************************
					文件名：USBL.c
					最后一次修改时间：2025/12/12
					修改内容：适配 8字节 ASCII 预编程任务协议 (U/D/L/R/F/B + 1-9)
*************************************************************************************/

#include "USBL.h"
#include "../../sys/epoll/epoll_manager.h"
#include "../../sys/SerialPort/SerialPort.h"
#include "../thruster/Thruster.h"
/* 引入主控舱头文件，用于控制释放器 */
#include "../../drivers/maincabin/MainCabin.h" 
/* [新增] 引入任务管理头文件 */
#include "../../task/task_mission.h"
#include "../../control/navigation_control.h"

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
static char g_usbl_readbuf[128] = {0}; 

 /*******************************************************************
 * 函数原型:int USBL_getFD(void)
 * 函数简介:返回文件描述符
 *****************************************************************/
int USBL_getFD(void)
{
	return g_usbl_fd;
}


 /*******************************************************************
 * 函数原型:int USBL_Init(void)
 * 函数简介:打开USBL串口设备
 *****************************************************************/
int USBL_Init(void)
{
	if(g_usbl_deviceName == NULL || g_usbl_fd > 0)
	{
		return -1;
	}

	g_usbl_fd = -1;

    g_usbl_fd = SerialPort_open(g_usbl_deviceName, &g_usbl_oldSerialPortConfig);
	if(g_usbl_fd < 0)
	{
		return -1;
	}

	if(SerialPort_configBaseParams(g_usbl_fd, 115200,  1, 8, 'N') < 0)
	{
		close(g_usbl_fd); 
        g_usbl_fd = -1;
		return -1;
	}

	return 0;
}


 /*******************************************************************
 * 函数原型:int USBL_Close(void)
 * 函数简介:关闭USBL
 *****************************************************************/
int USBL_Close(void)
{
 	if(g_usbl_fd < 0)
 	{
 		return -1;
 	}
 	
	pthread_rwlock_wrlock(&g_usbl_rwlock);
	if(SerialPort_close(g_usbl_fd, &g_usbl_oldSerialPortConfig) < 0)
	{
		return -1;
	}

	epoll_manager_del_fd(g_epoll_manager_fd, g_usbl_fd);

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
*****************************************************************/ 
ssize_t USBL_ReadRawData(void)
{
	if(g_usbl_fd < 0 || g_usbl_readbuf == NULL)
	{
		return -1;
	}

	pthread_rwlock_wrlock(&g_usbl_rwlock);
	memset(g_usbl_readbuf, 0, sizeof(g_usbl_readbuf));
		
	ssize_t nread = 0;
	int totalnByte = 0;
    char tmp_char = 0;

	while(totalnByte < sizeof(g_usbl_readbuf) - 1)
	{
		nread = read(g_usbl_fd, &tmp_char, 1);
		if(nread > 0)
		{
            g_usbl_readbuf[totalnByte++] = tmp_char;
            
            // 快速结束判断: '#' 结尾
            if(g_usbl_readbuf[0] == '#' && totalnByte > 5 && tmp_char == '#')
            {
                break; 
            }
            // 换行符结尾
            if(tmp_char == '\n')
            {
                break;
            }
            // 简单保护：如果读到8个&，直接截断处理
            if(totalnByte == 8 && strncmp(g_usbl_readbuf, "&&&&&&&&", 8) == 0) 
            {
                break; 
            }
		}
		else
		{
			break;
		}
	}

    if(totalnByte == 0)
    {
        pthread_rwlock_unlock(&g_usbl_rwlock);
        return 0;
    }

	g_usbl_readbuf[totalnByte] = '\0';

	/* 数据初步校验 */
    int is_valid = 0;
	if(g_usbl_readbuf[0] == '$' && g_usbl_readbuf[totalnByte-1] == '\n') is_valid = 1;
    else if(g_usbl_readbuf[0] == '#' && g_usbl_readbuf[totalnByte-1] == '#') is_valid = 1;
    else if(g_usbl_readbuf[0] == '#') is_valid = 1;
    // [新增] 放行 &&&&&&&&
    else if(strncmp(g_usbl_readbuf, "&&&&&&&&", 8) == 0) is_valid = 1;

	if(is_valid == 0)
	{
		memset(g_usbl_readbuf, 0, sizeof(g_usbl_readbuf));
		tcflush(g_usbl_fd, TCIFLUSH);
		pthread_rwlock_unlock(&g_usbl_rwlock);
		return -1;
	}

	pthread_rwlock_unlock(&g_usbl_rwlock);
	return totalnByte;
}

/* ==========================================================
 * 辅助函数：将字符转换为动作枚举 
 * 'U'=UP, 'D'=DOWN, 'L'=LEFT, 'R'=RIGHT, 'F'=FORWARD, 'B'=BACKWARD
 * ========================================================== */
static uint8_t USBL_CharToAction(char c) {
    switch(c) {
        case 'U': return M_ACT_UP;
        case 'D': return M_ACT_DOWN;
        case 'L': return M_ACT_LEFT;
        case 'R': return M_ACT_RIGHT;
        case 'F': return M_ACT_FORWARD; // 用 'F' 代表 "FD"
        case 'B': return M_ACT_BACKWARD;// 用 'B' 代表 "BD"
        default:  return M_ACT_STOP;    // 其他字符默认为停止
    }
}

/* ==========================================================
 * 辅助函数：将字符转换为持续时间(秒)
 * '1'=10s, '2'=20s ... '9'=90s
 * ========================================================== */
static uint8_t USBL_CharToDuration(char c) {
    if (c >= '1' && c <= '9') {
        return (c - '0') * 10;
    }
    return 0; // '0' 或其他字符代表 0秒
}

 /*******************************************************************
 * 函数原型:int USBL_ParseData(void)
 * 函数简介:解析USBL接收到的数据
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
     * 0. 最高优先级中断指令：&&&&&&&&
     * 只有这串字符可以强制打断正在执行的预编程任务
     * ========================================================== */
    if(strncmp(g_usbl_readbuf, "&&&&&&&&", 8) == 0)
    {
        printf("[USBL MISSION] 收到强制中断指令 &&&&&&&&\n");
        Task_Mission_Stop(); // 立即停止任务
        
        memset(g_usbl_readbuf, 0, sizeof(g_usbl_readbuf));
        pthread_rwlock_unlock(&g_usbl_rwlock);
        return 0;
    }

    /* ==========================================================
     * 1. 过滤干扰数据 (如 $41 开头)
     * ========================================================== */
    if(strncmp(g_usbl_readbuf, "$41", 3) == 0)
    {
        memset(g_usbl_readbuf, 0, sizeof(g_usbl_readbuf));
        pthread_rwlock_unlock(&g_usbl_rwlock);
        return 0;
    }

    /* ==========================================================
     * 2. 情况1：直接透传的明文控制指令 (例如: "#UP$$01#")
     * ========================================================== */
    if(g_usbl_readbuf[0] == '#')
    {
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
    static unsigned char tempHexArrey[64] = {0}; // 解码后的 Payload

    if(g_usbl_readbuf[0] == '$' && strlen(g_usbl_readbuf) > 38) 
    {
        memset(g_usbl_dataPack.recvdata, 0, sizeof(g_usbl_dataPack.recvdata));

        // 提取 HEX 长度
        lenC[0] = g_usbl_readbuf[37];
        lenC[1] = g_usbl_readbuf[38];
        lenC[2] = '\0';
        sscanf(lenC, "%x", &len); 

        if(len > 0 && 2*len < sizeof(temp)) 
        {
            // 复制 HEX 字符串
            strncpy(temp, g_usbl_readbuf + 39, 2 * len);
            temp[2 * len] = '\0'; 

            // HEX -> Char 转换
            for(int i = 0; i < 2*len; i = i + 2)
            {
                sscanf(&temp[i],"%2hhx", &tempHexArrey[i / 2]); 	
            }
            tempHexArrey[len] = '\0'; 

            memcpy(g_usbl_dataPack.recvdata, tempHexArrey, sizeof(tempHexArrey));
            
            /* -------------------------------------------------------------
             * [新增] 预编程任务指令解析 (8字节 ASCII)
             * 格式: [&] [Act1] [T1] [Act2] [T2] [Act3] [T3] [&]
             * 示例: & F 3 R 2 D 1 &  (前进30s -> 右转20s -> 下潜10s)
             * ------------------------------------------------------------- */
            if(len == 8 && tempHexArrey[0] == '&' && tempHexArrey[7] == '&')
            {
                // 1. 状态检查：如果当前有任务在跑，直接忽略新指令！
                if(Task_Mission_IsRunning()) {
                    printf("[USBL MISSION] 警告：任务正在执行中，新指令被拒绝！(请先发送 &&&&&&&& 中止)\n");
                }
                else {
                    MissionStep_t steps[MISSION_STEP_COUNT];
                    
                    // 第1组动作 (Index 1, 2)
                    steps[0].action   = USBL_CharToAction(tempHexArrey[1]);
                    steps[0].duration = USBL_CharToDuration(tempHexArrey[2]);

                    // 第2组动作 (Index 3, 4)
                    steps[1].action   = USBL_CharToAction(tempHexArrey[3]);
                    steps[1].duration = USBL_CharToDuration(tempHexArrey[4]);

                    // 第3组动作 (Index 5, 6)
                    steps[2].action   = USBL_CharToAction(tempHexArrey[5]);
                    steps[2].duration = USBL_CharToDuration(tempHexArrey[6]);

                    printf("[USBL MISSION] 解析成功: %c%c -> %c%c -> %c%c\n",
                           tempHexArrey[1], tempHexArrey[2],
                           tempHexArrey[3], tempHexArrey[4],
                           tempHexArrey[5], tempHexArrey[6]);
                           
                    printf("  Step1: Act=%d, Time=%ds\n", steps[0].action, steps[0].duration);
                    printf("  Step2: Act=%d, Time=%ds\n", steps[1].action, steps[1].duration);
                    printf("  Step3: Act=%d, Time=%ds\n", steps[2].action, steps[2].duration);

                    // 3. 启动任务
                    Task_Mission_UpdateAndStart(steps);
                }
            }

                   // [新增] B. 导航：下发目标点 (+...+) (22字节)
    // 格式: +Lat(9)+Lon(10)+
    else if (tempHexArrey[0] == '+' && strlen((char*)tempHexArrey) >= 21) 
    {
        // 停止其他模式
        Task_Mission_Stop(); 
        
        double lat = 0, lon = 0;
        // 利用 sscanf 的强大功能跳过中间的 + 号
        if (sscanf((char*)tempHexArrey, "+%lf+%lf+", &lat, &lon) == 2) {
            Nav_SetTarget(lat, lon);
        } else {
            printf("[USBL ERR] 目标坐标解析失败: %s\n", tempHexArrey);
        }
    }
    
    // [新增] C. 导航：当前定位数据 (/.../) (22字节)
    // 格式: /Lat(9)/Lon(10)/
    else if (tempHexArrey[0] == '/' && strlen((char*)tempHexArrey) >= 21) 
    {
        double lat = 0, lon = 0;
        if (sscanf((char*)tempHexArrey, "/%lf/%lf/", &lat, &lon) == 2) {
            Nav_UpdateCurrentPos(lat, lon);
        }
    }

            /* A. 解析电机指令 (#...) */
            else if(tempHexArrey[0] == '#' && tempHexArrey[7] == '#' && tempHexArrey[3] == '$' && tempHexArrey[4] == '$')
            {
                char ctl_cmd[3] = {'\0'}; 
                int ctl_arg = 0;
                sscanf((char *)(&tempHexArrey[1]), "%2s", ctl_cmd);
                sscanf((char *)(&tempHexArrey[5]), "%2d", &ctl_arg);
                
                printf("[USBL EXEC THRUSTER] 匹配成功 -> CMD:%s ARG:%d\n", ctl_cmd, ctl_arg);
                Thruster_ControlHandle(ctl_cmd, ctl_arg);
            } 
            /* B. 解析释放器指令 (@...) */
            else if(tempHexArrey[0] == '@')
            {
                char *payload_str = (char *)tempHexArrey;
                if(strstr(payload_str, "open1") != NULL) MainCabin_SwitchPowerDevice(Releaser1, 1);
                else if(strstr(payload_str, "close1") != NULL) MainCabin_SwitchPowerDevice(Releaser1, -1);
                else if(strstr(payload_str, "open2") != NULL) MainCabin_SwitchPowerDevice(Releaser2, 1);
                else if(strstr(payload_str, "close2") != NULL) MainCabin_SwitchPowerDevice(Releaser2, -1);
            }
        }
        else
        {
             printf("[USBL ERR] 解析长度 len=%d 异常或过长，跳过解析\n", len);
        }
    }

    // 清理静态变量
	memset(lenC, 0, sizeof(lenC));
	memset(temp, 0, sizeof(temp));
	memset(tempHexArrey, 0, sizeof(tempHexArrey));
	len = 0;

	pthread_rwlock_unlock(&g_usbl_rwlock);
	return 0;	
}