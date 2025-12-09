/************************************************************************************
					文件名：SerialPort.c
                    文件说明：封装操作串口的函数
					最后一次修改时间：2025/6/25
					修改内容：
 ************************************************************************************/

#include "SerialPort.h"

/************************************************************************************
 									全局变量
 ************************************************************************************/ 
/*  波特率  */
const int g_baudrate_arr[] = {B50, B75, B150, B200, B300, B600, B1200, B2400, B4800, B9600, B19200, B38400, B57600, B115200, B230400, B460800};
const int g_baudrate_param[] = {50, 75, 150, 200, 300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800};


/*******************************************************************
 * 函数原型:int SerialPort_open(const char *serialportName, struct termios *opt)
 * 函数简介:非阻塞式打开串口设备，并且保存当前串口的配置信息
 * 函数参数:serialportName:串口设备字符串
 * 函数参数:opt:把串口配置信息保存到的结构体地址
 * 函数返回值: 成功打开串口设备时，返回文件描述符，失败返回-1
 *****************************************************************/
int SerialPort_open(const char *serialportName, struct termios *opt)
{
    /*  0.入口检查  */
    if(serialportName == NULL || opt == NULL)
    {
        return -1;
    }

    /*  1.打开文件  */
    int fd = open(serialportName, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if(fd < 0)
    {
        return -1;
    }

    /*  2.获取当前串口属性  */
    if(tcgetattr(fd, opt) != 0)
    {
        return -1;
    }

    // 恢复阻塞模式
    fcntl(fd, F_SETFL, 0);

    return fd;
}


/*******************************************************************
 * 函数原型:int SerialPort_close(int fd, struct termios *opt)
 * 函数简介:关闭串口设备，恢复打开之前的串口配置，并清空串口缓冲区
 * 函数参数:fd:串口设备文件描述符
 *  函数参数:opt:把之前保存的串口配置信息进行恢复
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int SerialPort_close(int fd, struct termios *opt)
{
    /*  0. 入口检查 */
    if(fd < 0)
    {
        return -1;  
    }

    /*  1. 清空输入/输出缓冲区（非必须，但建议）*/
    tcflush(fd, TCIOFLUSH);

    /*  2.恢复串口配置  */
    if(tcsetattr(fd, TCSANOW, opt))
    {
        return -1;
    }

    /*  2. 关闭设备 */
    if(close(fd) < 0)
    {
        perror("SerialPort_close:关闭失败");
        return -1;
    }

    return 0;  
}


/*******************************************************************
 * 函数原型:int SerialPort_setBaudrate(int fd, int baudrate)
 * 函数简介:设置串口波特率
 * 函数参数:fd:串口设备的文件描述符
 * 函数参数:baudrate:想要设置的波特率
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int SerialPort_setBaudrate(int fd, int baudrate)
{
    /*  0. 入口检查 */
    if(fd < 0 || baudrate < 0)
    {
        return -1;
    }

    int ret = -1;

   /*  1.获取当前串口属性  */
    struct termios opt;
    if(tcgetattr(fd, &opt) != 0)
    {
        return -1;
    }

    /*  2.设置波特率    */
    for(int i = 0; i < sizeof(g_baudrate_arr) / sizeof(int); i++)
    {
        if(baudrate == g_baudrate_param[i])
        {
            cfsetispeed(&opt, g_baudrate_arr[i]);
            cfsetospeed(&opt, g_baudrate_arr[i]);
            ret = tcsetattr(fd, TCSANOW, &opt);
        }
    }

    /*  3.清空串口缓冲区数据    */
    tcflush(fd, TCIOFLUSH);

    return ret;
}


/*******************************************************************
 * 函数原型:int SerialPort_setStopbit(int fd, int stopbit)
 * 函数简介:设置串口的停止位
 * 函数参数:fd:串口设备的文件描述符
 * 函数参数:stopbit:想要设置的停止位, 1:1位，2:2位
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int SerialPort_setStopbit(int fd, int stopbit)
{
    /*  0. 入口检查 */
    if(fd < 0 || stopbit <=0 || stopbit > 2)
    {
        return -1;
    }
    
    int ret = -1;

   /*  1.获取当前串口属性  */
    struct termios opt;
    if(tcgetattr(fd, &opt) != 0)
    {
        return -1;
    }

    /*  2.设置停止位    */
    if(stopbit == 1)
    {
        opt.c_cflag &= ~CSTOPB;
    }
    else if(stopbit == 2)
    {
        opt.c_cflag |= CSTOPB;
    }

    ret = tcsetattr(fd, TCSANOW, &opt);

    /*  3.清空串口缓冲区数据    */
    tcflush(fd, TCIOFLUSH);

    return ret;
}


/*******************************************************************
 * 函数原型:int SerialPort_setDatabits(int fd, int databits)
 * 函数简介:设置串口的数据位
 * 函数参数:fd:串口设备的文件描述符
 * 函数参数:databits:想要设置的数据位, 5:5位，6:6位，7:7位，8:8位
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int SerialPort_setDatabits(int fd, int databits)
{
    /*  0. 入口检查 */
    if(fd < 0 || databits < 5 || databits > 8)
    {
        return -1;
    }
    
    int ret = -1;

   /*  1.获取当前串口属性  */
    struct termios opt;
    if(tcgetattr(fd, &opt) != 0)
    {
        return -1;
    }

    /*  2.设置数据位    */
    opt.c_cflag &= ~CSIZE;      //清空相关设置
    switch(databits)
    {
        case 5:
            opt.c_cflag |= CS5;
            break;

        case 6:
            opt.c_cflag |= CS6;
        
        case 7:
            opt.c_cflag |= CS7;
            break;

        case 8:
            opt.c_cflag |= CS8;
            break;

        default:
            break;
    }

    ret = tcsetattr(fd, TCSANOW, &opt);

    /*  3.清空串口缓冲区数据    */
    tcflush(fd, TCIOFLUSH);

    return ret;
}


/*******************************************************************
 * 函数原型:int SerialPort_setParity(int fd, char parity)
 * 函数简介:设置串口的校验位
 * 函数参数:fd:串口设备的文件描述符
 * 函数参数:parity:想要设置的校验位, 'N':无校验，'O':奇校验，'E':偶校验
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int SerialPort_setParity(int fd, char parity)
{
    /*  0. 入口检查 */
    if(fd < 0 || (parity != 'N' &&  parity != 'O' && parity != 'E'))
    {
        return -1;
    }
    
    int ret = -1;

   /*  1.获取当前串口属性  */
    struct termios opt;
    if(tcgetattr(fd, &opt) != 0)
    {
        return -1;
    }

    /*  2.设置校验位    */
    switch(parity)
    {
        case 'N':
            opt.c_cflag &= ~PARENB;      // 关闭奇偶校验
            opt.c_iflag &= ~INPCK;          // 禁用输入校验检查
            break;

        case 'O':
            opt.c_cflag |= (PARODD | PARENB);   // 启用奇校验
            opt.c_iflag |= (INPCK | ISTRIP);            // 启用输入校验并剥离校验位
            break;

        case 'E':
            opt.c_cflag |= PARENB;                   // 启用偶校验
            opt.c_cflag &= ~PARODD;              // 清除奇校验标志（即使用偶校验）
            opt.c_iflag |= (INPCK | ISTRIP);    // 启用输入校验并剥离校验位
            break;

        default:
            break;
    }
    
    ret = tcsetattr(fd, TCSANOW, &opt);

    /*  3.清空串口缓冲区数据    */
    tcflush(fd, TCIOFLUSH);

    return ret;
}


/*******************************************************************
 * 函数原型:int SerialPort_setFlowControl(int fd, int enable)
 * 函数简介:设置串口的流控位
 * 函数参数:fd:串口设备的文件描述符
 * 函数参数:enable:1:打开， 0:关闭
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int SerialPort_setFlowControl(int fd, int enable)
{
    /*  0.入口检查  */
   if(fd < 0 || (enable != 0 && enable != 1)) 
   {
        return -1;
    }

    int ret = -1;

    /*  1.获取当前串口属性  */
    struct termios opt;
    if(tcgetattr(fd, &opt) != 0)
    {
        return -1;
    }

    /*2.设置或清除 CRTSCTS 标志位    */
    if(enable == 0)
    {
        opt.c_cflag &= ~CRTSCTS; // 禁用硬件流控
    }
    else if(enable == 1)
    {
        opt.c_cflag |= CRTSCTS;  // 启用硬件流控
    }

     ret = tcsetattr(fd, TCSANOW, &opt);

    /*  3.清空串口缓冲区数据    */
    tcflush(fd, TCIOFLUSH);

    return ret;
}


/*******************************************************************
 * 函数原型:int SerialPort_setDTR(int fd, int enable)
 * 函数简介:设置串口的DTR
 * 函数参数:fd:串口设备的文件描述符
 * 函数参数:enable:1:打开， 0:关闭
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int SerialPort_setDTR(int fd, int enable)
{
    /*  0.入口检查  */
   if(fd < 0 || (enable != 0 && enable != 1)) 
   {
        return -1;
    }

    int ret = -1;

    /*  1.获取当前Modem信号状态  */
    int status;
    if(ioctl(fd, TIOCMGET, &status) < 0)
    {
        return -1;
    }

    /*2.设置或清除 DTR 标志位    */
    if(enable == 0)
    {
        status &= ~TIOCM_DTR; // 拉低DTR
    }
    else if(enable == 1)
    {
        status |= TIOCM_DTR;  // 拉高DTR
    }

    ret = ioctl(fd, TIOCMSET, &status);

    return ret;
}


/*******************************************************************
 * 函数原型:int SerialPort_configBaseParams(int fd, int baudrate, int stopbit, int databits, char parity)
 * 函数简介:打开串口设备基本参数，波特率，停止位，数据位，校验位，打开接收使能
 * 函数参数:serialportName:串口设备字符串
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int SerialPort_configBaseParams(int fd, int baudrate, int stopbit, int databits, char parity)
{
    /*  0.入口检查  */
    if(fd < 0 || baudrate < 0 || (stopbit != 1 && stopbit != 2) || \
        databits < 5 || databits >8 ||(parity != 'N' && parity != 'O' && parity != 'E') )
    {
        printf("SerialPort_configBaseParams:输入参数错误\n");
        return -1;
    }

    int ret = -1;

    /*  1.打开接收使能等  */
    struct termios opt;
    if(tcgetattr(fd, &opt) != 0)
    {
        return -1;
    }

    // 1.1原始输入模式
    opt.c_cflag |= (CLOCAL | CREAD);
    opt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    opt.c_iflag &= ~(IXON | IXOFF | IXANY);
    opt.c_iflag &= ~(ICRNL | INLCR);                // 禁止CR->NL和NL->CR转换
    opt.c_oflag &= ~(ONLCR | OCRNL);           // 禁止输出换行符转换
    opt.c_oflag &= ~OPOST;
    
    // 1.2设置超时和最小字符数
    opt.c_cc[VMIN] = 1;
    opt.c_cc[VTIME] = 0;

    ret = tcsetattr(fd, TCSANOW, &opt);

    /*  2.设置波特率    */
    ret = SerialPort_setBaudrate(fd, baudrate);
    if(ret < 0)
    {
        printf("SerialPort_configBaseParams:设置波特率错误\n");
        return -1;
    }

    /*  3.设置停止位    */
    ret = SerialPort_setStopbit(fd, stopbit);
    if(ret < 0)
    {
        printf("SerialPort_configBaseParams:设置停止位错误\n");
        return -1;
    }

    /*  4.设置数据位    */
    ret = SerialPort_setDatabits(fd, databits);
    if(ret < 0)
    {
        printf("SerialPort_configBaseParams:设置数据位错误\n");
        return -1;
    }

    /*  5.设置校验位    */
    ret = SerialPort_setParity(fd, parity);
    if(ret < 0)
    {
        printf("SerialPort_configBaseParams:设置校验位错误\n");
        return -1;
    }

    /*  6.设置流控位    */
    ret = SerialPort_setFlowControl(fd, 0);
    if(ret < 0)
    {
        printf("SerialPort_configBaseParams:设置流控位错误\n");
        return -1;
    }

    /*  7.设置DTR    */
    ret = SerialPort_setDTR(fd, 0);
    if(ret < 0)
    {
        printf("SerialPort_configBaseParams:设置DTR错误\n");
        return -1;
    }

    return ret;
}


/*******************************************************************
 * 函数原型:void SerialPort_printConfig(int fd, const char *serialportName)
 * 函数简介:打印串口设备配置
 * 函数参数:fd:串口设备的文件描述符
 * 函数参数:serialportName:串口设备字符串
 * 函数返回值: 无
 *****************************************************************/
void SerialPort_printConfig(int fd, const char *serialportName)
{
    /*  0.入口检查  */
    if(fd < 0 || serialportName == NULL) 
    {    
        return;
    }

    /*  1.获取当前串口属性  */
    struct termios opt;
    if(tcgetattr(fd, &opt) != 0)
    {
        printf("SerialPort_printConfig:串口数据获取异常，串口:%s\n", serialportName);
        return;
    }

    /*  2.打印数据  */
    printf("\n=== 串口配置打印 [%s] ===\n", serialportName);

    // 2.1. 波特率
    printf("\n[1. 波特率]\n");
    printf("  Input:  ");
    switch(cfgetispeed(&opt))
    {
        case B0:      printf("0 (hang up)\n"); break;
        case B50:     printf("50\n"); break;
        case B75:     printf("75\n"); break;
        case B110:    printf("110\n"); break;
        case B134:    printf("134\n"); break;
        case B150:    printf("150\n"); break;
        case B200:    printf("200\n"); break;
        case B300:    printf("300\n"); break;
        case B600:    printf("600\n"); break;
        case B1200:   printf("1200\n"); break;
        case B1800:   printf("1800\n"); break;
        case B2400:   printf("2400\n"); break;
        case B4800:   printf("4800\n"); break;
        case B9600:   printf("9600\n"); break;
        case B19200:  printf("19200\n"); break;
        case B38400:  printf("38400\n"); break;
        case B57600:  printf("57600\n"); break;
        case B115200: printf("115200\n"); break;
        default:      printf("Unknown\n");
    }
    printf("  Output: ");
    switch (cfgetospeed(&opt)) {
        case B115200: printf("115200\n"); break;
        default:      printf("See above\n");
    }

    // 2.2. 数据帧格式
    printf("\n[2. 数据帧格式]\n");
    printf("  数据位:  ");
    switch (opt.c_cflag & CSIZE) {
        case CS5: printf("5\n"); break;
        case CS6: printf("6\n"); break;
        case CS7: printf("7\n"); break;
        case CS8: printf("8\n"); break;
        default:  printf("Unknown\n");
    }
    printf("  停止位:  %s\n", (opt.c_cflag & CSTOPB) ? "2" : "1");
    printf("  校验位:     ");
    if (opt.c_cflag & PARENB) {
        printf("%s\n", (opt.c_cflag & PARODD) ? "Odd" : "Even");
    } else {
        printf("None\n");
    }

    // 2.3. 流控设置
    printf("\n[3. 流控设置]\n");
    printf("  Hardware (RTS/CTS): %s\n", 
           (opt.c_cflag & CRTSCTS) ? "Enabled" : "Disabled");
    printf("  Software (XON/XOFF): %s\n",
           (opt.c_iflag & (IXON|IXOFF)) ? "Enabled" : "Disabled");

    // 2.4. 工作模式
    printf("\n[4. 工作模式]\n");
    printf("  Canonical Mode:    %s\n", 
           (opt.c_lflag & ICANON) ? "Enabled (line-by-line)" : "Disabled (raw)");
    printf("  Echo:              %s\n", 
           (opt.c_lflag & ECHO) ? "Enabled" : "Disabled");
    printf("  Signal Interrupt:  %s\n",
           (opt.c_lflag & ISIG) ? "Enabled" : "Disabled");

    // 2.5. 控制字符
    printf("\n[5. 控制字符]\n");
    printf("  VMIN:  %u (min bytes to read)\n", opt.c_cc[VMIN]);
    printf("  VTIME: %u (timeout in 0.1s)\n", opt.c_cc[VTIME]);

    // 2.6. 硬件信号状态
    int status;
    if (ioctl(fd, TIOCMGET, &status) == 0) {
        printf("\n[6. 硬件信号状态]\n");
        printf("  DTR: %s\tRTS: %s\n",
               (status & TIOCM_DTR) ? "ON" : "OFF",
               (status & TIOCM_RTS) ? "ON" : "OFF");
        printf("  CTS: %s\tDSR: %s\n",
               (status & TIOCM_CTS) ? "ON" : "OFF",
               (status & TIOCM_DSR) ? "ON" : "OFF");
        printf("  DCD: %s\tRI:  %s\n",
               (status & TIOCM_CAR) ? "ON" : "OFF",
               (status & TIOCM_RNG) ? "ON" : "OFF");
    } else {
        perror("ioctl(TIOCMGET) failed");
    }

    printf("\n=== 结束 ===\n");
}
