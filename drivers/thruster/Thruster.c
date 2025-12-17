/************************************************************************************
					文件名：Thruster.c
                    描述：推进器控制模块实现
					最后修改时间：2025-06-26
 ************************************************************************************/

#include "Thruster.h"
#include "../../sys/SerialPort/SerialPort.h"

/************************************************************************************
 									全局变量(可以extern的变量)
*************************************************************************************/
/*  工作状态    */
volatile int g_thruster_status = -1;        //-1为关闭串口，1为开启串口

/************************************************************************************
 									全局变量(仅可本文件使用)
*************************************************************************************/
/*	设备路径	*/
static char *g_thrusterDeviceName = "/dev/thruster";

/*	设备的文件描述符	*/
static int g_thruster_fd = -1;

/*  互斥锁  */
static pthread_mutex_t g_thruster_mutex = PTHREAD_MUTEX_INITIALIZER;

/*  旧的串口配置  */
static struct termios g_oldSerialPortConfig = {0};

/*  线程标志    */
static volatile int g_heartbeat_running = 0;

/*  电机心跳包命令  */
static const unsigned char HEARTBEAT_CMDS[8][8] = {
    {0x01, 0x06, 0x17, 0x70, 0x00, 0x01, 0x4C, 0x65},       //电机1_1
    {0x02, 0x06, 0x17, 0x70, 0x00, 0x01, 0x4C, 0x56},       //电机2_1
    {0x03, 0x06, 0x17, 0x70, 0x00, 0x01, 0x4D, 0x87},       //电机3_1
    {0x04, 0x06, 0x17, 0x70, 0x00, 0x01, 0x4C, 0x30},       //电机4_1

    {0x01, 0x06, 0x17, 0x70, 0x00, 0x02, 0x0C, 0x64},       //电机1_2
    {0x02, 0x06, 0x17, 0x70, 0x00, 0x02, 0x0C, 0x57},       //电机2_2
    {0x03, 0x06, 0x17, 0x70, 0x00, 0x02, 0x0D, 0x86},       //电机3_2
    {0x04, 0x06, 0x17, 0x70, 0x00, 0x02, 0x0C, 0x31}        //电机4_2
};

/*  上电设置命令    */
static const unsigned char STARTUP_CMDS[4][8] = {
    {0x01, 0x06, 0x17, 0x71, 0x00, 0x01, 0x1D, 0xA5},       //电机1初始化
    {0x02, 0x06, 0x17, 0x71, 0x00, 0x01, 0x1D, 0x96},       //电机2初始化
    {0x03, 0x06, 0x17, 0x71, 0x00, 0x01, 0x1C, 0x47},       //电机3初始化
    {0x04, 0x06, 0x17, 0x71, 0x00, 0x01, 0x1D, 0xF0}        //电机4初始化
};

/*  电机控制命令 (正向/反向 x 5个级别)  */
static const unsigned char MOTOR_CMDS[4][2][5][13] = {
    // 电机1
    {
        // 正向
        {
            {0x01, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xEF, 0x98, 0x12, 0xD0}, //级别1
            {0x01, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xDF, 0xF8, 0x06, 0xFB}, //级别2
            {0x01, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xCE, 0xC8, 0x0A, 0xBC}, //级别3
            {0x01, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xBE, 0x60, 0x2E, 0xC2}, //级别4
            {0x01, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xAD, 0xF8, 0x22, 0x58}  //级别5
        },
        // 反向
        {
            {0x01, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x10, 0x68, 0x53, 0x40}, //级别1
            {0x01, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x20, 0x0D, 0x47, 0x32}, //级别2
            {0x01, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x31, 0x38, 0x4B, 0x2C}, //级别3
            {0x01, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x41, 0xA0, 0x6F, 0x46}, //级别4
            {0x01, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x52, 0x08, 0x63, 0xC8}  //级别5
        }
    },

    //电机2
    {
        //正向
        {
            {0x02, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x10, 0x68, 0x5C, 0x04}, //级别1
            {0x02, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x20, 0xD0, 0x48, 0x76}, //级别2
            {0x02, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x31, 0x38, 0x44, 0x68}, //级别3
            {0x02, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x41, 0xA0, 0x60, 0x02}, //级别4
            {0x02, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x52, 0x08, 0x6C, 0x8C}  //级别5
        },
        //反向
        {
            {0x02, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xEF, 0x98, 0x1D, 0x94}, //级别1
            {0x02, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xDF, 0xF8, 0x09, 0xBC}, //级别2
            {0x02, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xCE, 0xC8, 0x05, 0xFB}, //级别3
            {0x02, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xBE, 0x60, 0x21, 0x86}, //级别4
            {0x02, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xAD, 0xF8, 0x2D, 0x1C}  //级别5
        }
    },

    //电机3
    {
        //正向
        {
            {0x03, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xEF, 0x98, 0x19, 0x68}, //级别1
            {0x03, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xDF, 0xF8, 0x0D, 0x40}, //级别2
            {0x03, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xCE, 0xC8, 0x01, 0x04}, //级别3
            {0x03, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xBE, 0x60, 0x25, 0x7A}, //级别4
            {0x03, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xAD, 0xF8, 0x29, 0xE0}  //级别5
        },
        //反向
        {
            {0x03, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x10, 0x68, 0x58, 0xF8}, //级别1
            {0x03, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x20, 0xD0, 0x4C, 0x8A}, //级别2
            {0x03, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x31, 0x38, 0x40, 0x94}, //级别3
            {0x03, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x41, 0xA0, 0x64, 0xFE}, //级别4
            {0x03, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x52, 0x08, 0x68, 0x70}  //级别5
        }
    },

    //电机4
    {
        //正向
        {
            {0x04, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x10, 0x68, 0x42, 0x8C}, //级别1
            {0x04, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x20, 0xD0, 0x56, 0xFE}, //级别2
            {0x04, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x31, 0x38, 0x5A, 0xE0}, //级别3
            {0x04, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x41, 0xA0, 0x7E, 0x8A}, //级别4
            {0x04, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x52, 0x08, 0x72, 0x04}  //级别5
        },
        //反向
        {
            {0x04, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xEF, 0x98, 0x03, 0x1C}, //级别1
            {0x04, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xDF, 0xF8, 0x17, 0x34}, //级别2
            {0x04, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xCE, 0xC8, 0x1B, 0x70}, //级别3
            {0x04, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xBE, 0x60, 0x3F, 0x0E}, //级别4
            {0x04, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0xFF, 0xFF, 0xAD, 0xF8, 0x33, 0x94}  //级别5
        }
    }
};

/*  停止命令    */
static const unsigned char STOP_CMDS[4][13] = {
    {0x01, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x5F, 0x6E}, //电机1停止
    {0x02, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x50, 0x2A}, //电机2停止
    {0x03, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x54, 0xD6}, //电机3停止
    {0x04, 0x10, 0x17, 0x73, 0x00, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x4E, 0xA2}  //电机4停止
};

/************************************************************************************
 									辅助函数(仅本文件可使用)
*************************************************************************************/
/*******************************************************************
* 函数原型:static int Thruster_ConfigureSerial(int fd)
* 函数简介:配置串口。
* 函数参数:fd:文件描述符
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
static int Thruster_ConfigureSerial(int fd)
{
    struct termios options;
    
    if (tcgetattr(fd, &options) != 0) {
        perror("tcgetattr failed:Thruster serialport");
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
        perror("tcsetattr failed:Thruster serialport");
        return -1;
    }
    
    return 0;
}


/*******************************************************************
* 函数原型:static int Thruster_SendCommandWithResponse(const unsigned char *cmd, size_t len, 
                                 unsigned char *response, size_t resp_len) 
* 函数简介:向推进器发送命令，并且等待回应。
* 函数参数:cmd:命令
* 函数参数:len:命令长度
* 函数参数:response:回应的数据
* 函数参数:resp_len:回应的长度
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
static int Thruster_SendCommandWithResponse(const unsigned char *cmd, size_t len, 
                                 unsigned char *response, size_t resp_len) 
{
    if (g_thruster_fd < 0) return -1;
    
    pthread_mutex_lock(&g_thruster_mutex);
    
    // 清空缓冲区
    tcflush(g_thruster_fd, TCIOFLUSH);
    
    // 发送命令
    if (write(g_thruster_fd, cmd, len) != len) {
        pthread_mutex_unlock(&g_thruster_mutex);
        return -1;
    }

    usleep(20000);
    
    // 读取响应
    //int n = read(g_thruster_fd, response, resp_len);
    
    pthread_mutex_unlock(&g_thruster_mutex);
    
    return 0;//(n > 0) ? 0 : -1;
}


/************************************************************************************
 									公共接口实现(外部可调用)
*************************************************************************************/
/*******************************************************************
* 函数原型:int Thruster_Init_default(void) 
* 函数简介:初始化推进器的串口，可直接调用进行初始化
* 函数参数:无
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int Thruster_Init_default(void)  
{
    if (g_thrusterDeviceName == NULL || g_thruster_fd >= 0) {
        return 0; // 已经初始化
    }
    
    g_thruster_fd = open(g_thrusterDeviceName, O_RDWR | O_NOCTTY | O_NDELAY);
    if (g_thruster_fd < 0) {
        return -1;
    }
    
    // 恢复阻塞模式
    fcntl(g_thruster_fd, F_SETFL, 0);
    
    if (Thruster_ConfigureSerial(g_thruster_fd) < 0) {
        close(g_thruster_fd);
        g_thruster_fd = -1;
        return -1;
    }
    
    return 0;
}


/*******************************************************************
* 函数原型:int Thruster_Init(void) 
* 函数简介:初始化推进器的串口，调用的SerialPort.h
* 函数参数:无
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int Thruster_Init(void) 
{
    /*	0.入口检查	*/
	if(g_thrusterDeviceName == NULL || g_thruster_fd > 0)
	{
		return -1;
	}

	g_thruster_fd = -1;

	/*	1.打开串口	*/
    g_thruster_fd = SerialPort_open(g_thrusterDeviceName, &g_oldSerialPortConfig);
	if(g_thruster_fd < 0)
	{
		return -1;
	}

	/*	2.配置串口	*/
	if(SerialPort_configBaseParams(g_thruster_fd, 115200,  1, 8, 'N') < 0)
	{
        close(g_thruster_fd);  // 关闭已打开的文件描述符
        g_thruster_fd = -1;
		return -1;
	}

	return 0;
}


/*******************************************************************
* 函数原型:int Thruster_SendInitConfig(void)
* 函数简介:发送上电配置指令(必须执行一次)。
* 函数参数:无
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int Thruster_SendInitConfig(void)
{
    if (g_thruster_fd < 0) {
        return -1;
    }

    unsigned char response[8] = {0};
    for (int i = 0; i < 4; i++) {
            if (Thruster_SendCommandWithResponse(STARTUP_CMDS[i], 8, response, 8) < 0) {
                return -1;
            }
            usleep(50000);
    }
    
    return 0;
}


/*******************************************************************
* 函数原型:void Thruster_Cleanup_default(void) 
* 函数简介:推进器的默认结束清理函数。
* 函数参数:无
* 函数返回值:无
*****************************************************************/
void Thruster_Cleanup_default(void)
{
    if (g_thruster_fd >= 0) {
        Thruster_Stop();
        close(g_thruster_fd);
        g_thruster_fd = -1;
    }
}


/*******************************************************************
* 函数原型:void Thruster_Cleanup(void) 
* 函数简介:推进器的默认结束清理函数。
* 函数参数:无
* 函数返回值:无
*****************************************************************/
void Thruster_Cleanup(void)
{
    if (g_thruster_fd >= 0) {
        Thruster_Stop();
        SerialPort_close(g_thruster_fd, &g_oldSerialPortConfig);
        g_thruster_fd = -1;
    }
}


/*******************************************************************
* 函数原型:int Thruster_SendCommand(ThrusterMotorID motor, const unsigned char *cmd, size_t len)
* 函数简介:向推进器发送命令。
* 函数参数:motor:电机ID的枚举
* 函数参数:cmd:命令
* 函数参数:len:命令长度
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int Thruster_SendCommand(ThrusterMotorID motor, const unsigned char *cmd, size_t len)
{
    unsigned char response[16];
    return Thruster_SendCommandWithResponse(cmd, len, response, sizeof(response));
}


/*******************************************************************
* 函数原型:int Thruster_SetMotorPower(ThrusterMotorID motor, ThrusterPowerLevel level, ThrusterDirection dir)
* 函数简介:设置某个电机的转动档位和方向。
* 函数参数:motor:电机ID的枚举
* 函数参数:level:档位
* 函数参数:dir:转动方向
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int Thruster_SetMotorPower(ThrusterMotorID motor, ThrusterPowerLevel level, ThrusterDirection dir)
{
#if 0
    if (motor == THRUSTER_ALL_MOTORS) {
        for (int i = 0; i < 4; i++) {
            if (Thruster_SetMotorPower(i+1, level, dir) < 0) {
                return -1;
            }
        }
        return 0;
    }
#endif

    if (motor < 1 || motor > 4) return -1;
    if (level < THRUSTER_STOP || level > THRUSTER_LEVEL_5) return -1;
    
    const unsigned char (*cmd)[13] = NULL;
    
    if (level == THRUSTER_STOP) {
        cmd = &STOP_CMDS[motor-1];
    } else {
        cmd = &MOTOR_CMDS[motor-1][dir][level-1];
    }
    
    return Thruster_SendCommand(motor, *cmd, 13);
}


// [新增] 仅停止水平电机 (用于导航到达、手动接管水平方向)
int Thruster_StopHorizontal(void) {
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_1, THRUSTER_STOP, THRUSTER_DIR_FORWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_2, THRUSTER_STOP, THRUSTER_DIR_FORWARD) < 0) return -1;
    printf("推进器：水平电机停止\n");
    return 0;
}

// [新增] 仅停止垂直电机 (用于定深/定高到达、手动接管垂直方向)
int Thruster_StopVertical(void) {
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_3, THRUSTER_STOP, THRUSTER_DIR_BACKWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_4, THRUSTER_STOP, THRUSTER_DIR_BACKWARD) < 0) return -1;
    printf("推进器：垂直电机停止\n");
    return 0;
}
/*******************************************************************
* 函数原型:int Thruster_Stop(void)
* 函数简介:停止所有电机。
* 函数参数:无
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int Thruster_Stop(void) {
    int ret1 = Thruster_StopHorizontal();
    int ret2 = Thruster_StopVertical();
    if (ret1 < 0 || ret2 < 0) return -1;
    printf("推进器：全部停止\n");
    return 0;
}




/*******************************************************************
* 函数原型:int Thruster_Floating(ThrusterPowerLevel level)
* 函数简介:推进器上浮运动。
* 函数参数:level:档位
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int Thruster_Floating(ThrusterPowerLevel level)
{
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_1, THRUSTER_STOP, THRUSTER_DIR_FORWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_2, THRUSTER_STOP, THRUSTER_DIR_FORWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_3, level, THRUSTER_DIR_BACKWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_4, level, THRUSTER_DIR_BACKWARD) < 0) return -1;
    printf("推进器：上浮，档位：%d\n", level);
    return 0;
}


/*******************************************************************
* 函数原型:int Thruster_Sinking(ThrusterPowerLevel level) 
* 函数简介:推进器下沉运动。
* 函数参数:level:档位
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int Thruster_Sinking(ThrusterPowerLevel level) 
{
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_1, THRUSTER_STOP, THRUSTER_DIR_FORWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_2, THRUSTER_STOP, THRUSTER_DIR_FORWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_3, level, THRUSTER_DIR_FORWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_4, level, THRUSTER_DIR_FORWARD) < 0) return -1;
    printf("推进器：下沉，档位：%d\n", level);
    return 0;
}


/*******************************************************************
* 函数原型:int Thruster_Forward(ThrusterPowerLevel level)
* 函数简介:推进器前进运动。
* 函数参数:level:档位
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int Thruster_Forward(ThrusterPowerLevel level)
{
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_1, level, THRUSTER_DIR_FORWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_2, level, THRUSTER_DIR_FORWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_3, THRUSTER_STOP, THRUSTER_DIR_FORWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_4, THRUSTER_STOP, THRUSTER_DIR_FORWARD) < 0) return -1;
    printf("推进器：前进，档位：%d\n", level);
    return 0;
}


/*******************************************************************
* 函数原型:int Thruster_Backward(ThrusterPowerLevel level)
* 函数简介:推进器后退运动。
* 函数参数:level:档位
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int Thruster_Backward(ThrusterPowerLevel level)
{
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_1, level, THRUSTER_DIR_BACKWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_2, level, THRUSTER_DIR_BACKWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_3, THRUSTER_STOP, THRUSTER_DIR_FORWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_4, THRUSTER_STOP, THRUSTER_DIR_FORWARD) < 0) return -1;
    printf("推进器：后退，档位：%d\n", level);
    return 0;
}


/*******************************************************************
* 函数原型:int Thruster_TurnLeft(ThrusterPowerLevel level)
* 函数简介:推进器左转运动。
* 函数参数:level:档位
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int Thruster_TurnLeft(ThrusterPowerLevel level)
{
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_1, level, THRUSTER_DIR_FORWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_2, level, THRUSTER_DIR_BACKWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_3, THRUSTER_STOP, THRUSTER_DIR_FORWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_4, THRUSTER_STOP, THRUSTER_DIR_FORWARD) < 0) return -1;
    printf("推进器：左转，档位：%d\n", level);
    return 0;
}


/*******************************************************************
* 函数原型:int Thruster_TurnRight(ThrusterPowerLevel level)
* 函数简介:推进器右转运动。
* 函数参数:level:档位
* 函数返回值:成功返回0，失败返回-1。
*****************************************************************/
int Thruster_TurnRight(ThrusterPowerLevel level)
{
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_1, level, THRUSTER_DIR_BACKWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_2, level, THRUSTER_DIR_FORWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_3, THRUSTER_STOP, THRUSTER_DIR_FORWARD) < 0) return -1;
    if (Thruster_SetMotorPower(THRUSTER_MOTOR_4, THRUSTER_STOP, THRUSTER_DIR_FORWARD) < 0) return -1;
    printf("推进器：右转，档位：%d\n", level);
    return 0;
}


/*******************************************************************
* 函数原型:static void *Thruster_HeartbeatThread(void *arg)
* 函数简介:心跳线程函数。
* 函数参数:arg:参数
* 函数返回值:无
*****************************************************************/
static void *Thruster_HeartbeatThread(void *arg)
{
    (void)arg;
    unsigned char response[8];
    
    while (g_heartbeat_running) {
        for (int i = 0; i < 8 && g_heartbeat_running; i++) {
            Thruster_SendCommandWithResponse(HEARTBEAT_CMDS[i], 8, response, sizeof(response));
            usleep(100000);
        }
    }
    
    return NULL;
}


/*******************************************************************
* 函数原型:pthread_t Thruster_StartHeartbeatThread(void)
* 函数简介:创建开始发送心跳线程函数。
* 函数参数:无
* 函数返回值:成功返回线程的tid，失败返回0
*****************************************************************/
pthread_t Thruster_StartHeartbeatThread(void)
{
    if (g_heartbeat_running) {
        return 0;
    }
    
    g_heartbeat_running = 1;
    pthread_t tid;
    if (pthread_create(&tid, NULL, Thruster_HeartbeatThread, NULL) != 0) {
        g_heartbeat_running = 0;
        return 0;
    }
    usleep(100000);
    return tid;
}


/*******************************************************************
* 函数原型:void Thruster_StopHeartbeatThread(pthread_t tid)
* 函数简介:停止发送心跳，即结束心跳线程。
* 函数参数:tid:心跳线程的tid
* 函数返回值:无
*****************************************************************/
void Thruster_StopHeartbeatThread(pthread_t tid)
{
    g_heartbeat_running = 0;
    if (tid) {
        pthread_join(tid, NULL);
    }
}


/*******************************************************************
* 函数原型:void Thruster_ControlHandle(const char *cmd, int arg)
* 函数简介:通过指令控制电机的动作，上浮、下沉等
* 函数参数:ctl_cmd 动作， ctl_arg档位
* 函数返回值: 无
*****************************************************************/
void Thruster_ControlHandle(const char *cmd, int arg) 
{
    if (!cmd || strlen(cmd) < 2) return;
    
    ThrusterPowerLevel level = (arg >= 1 && arg <= 5) ? arg : THRUSTER_LEVEL_1;
    
    if (strncmp(cmd, "UP", 2) == 0) {
        Thruster_Floating(level);
    } else if (strncmp(cmd, "DN", 2) == 0) {
        Thruster_Sinking(level);
    } else if (strncmp(cmd, "LT", 2) == 0) {
        Thruster_TurnLeft(level);
    } else if (strncmp(cmd, "RT", 2) == 0) {
        Thruster_TurnRight(level);
    } else if (strncmp(cmd, "FD", 2) == 0) {
        Thruster_Forward(level);
    } else if (strncmp(cmd, "BD", 2) == 0) {
        Thruster_Backward(level);
    } else if (strncmp(cmd, "ZE", 2) == 0) {
        Thruster_Stop();
    }
}

/*  一键使用    */
int Thruster_Task(void)
{
    /*  1.推进器初始化    */
    if(Thruster_Init() < 0)                  
    {
        return -1;
    }            
    usleep(500000);

    /*  2.发送上电配置  */
    if(Thruster_SendInitConfig() < 0)  
    {
        return -1;
    }

    /*  3.打开心跳  */
    if(Thruster_StartHeartbeatThread() == 0)
    {
        return -1;
    }

    sleep(5);   //心跳延时

    return 0;
}
