#include "task_thread.h"

/*  1.Epoll管理器   */
#include "../sys/epoll/epoll_manager.h"

/*  2.主控舱    */
#include "../drivers/maincabin/MainCabin.h"

/*  3.推进器    */
#include "../drivers/thruster/Thruster.h"

/*  4.上位机连接 -- 服务器*/
#include "../drivers/connectHost/connectHost.h"

/*  5.GPS    */
#include "../drivers/gps/GPS.h"

/*  6.CTD   */
#include "../drivers/ctd/CTD.h"

/*  7.DVL   */
#include "../drivers/dvl/DVL.h"

/*  8.DTU   */
#include "../drivers/dtu/DTU.h"

/*  9.USBL   */
#include "../drivers/usbl/USBL.h"

/*  10.Sonar*/
#include "../drivers/sonar/Sonar.h"

/*  12.TCP  */
#include "../sys/socket/TCP/tcp.h"

/*  13.sqlite3  */
#include "../sys/sqlite3_db/Database.h"

// [新增] 必须包含这个头文件，否则会出现 implicit declaration 警告
#include "../control/depth_control.h"
#include "../control/altitude_control.h"
#include "../control/navigation_control.h"
/************************************************************************************
 									外部变量
*************************************************************************************/
extern int g_connecthost_tcpser_listen_sock_fd;             //上位机的传输套接字fd
extern int g_connecthost_tcpser_accept_sock_fd;         //上位机的传输套接字fd
extern volatile int g_connecthost_tcpserConnectFlag;		//-1为未连接，1为已连接
extern char g_tcpserRecvBuf[256];

extern volatile int g_ctd_status;           //CTD是否可工作的状态
extern volatile int g_dvl_status;           //DVL是否可工作的状态
extern volatile int g_dtu_status;          //数传电台是否可工作的状态
extern volatile int g_usbl_status;         //USBL是否可工作的状态
extern volatile int g_sonar_status;         //Sonar是否可工作的状态


extern maincabinDataPack_t g_maincabin_data_pack;       //主控舱数据结构体
extern gpsDataPack_t g_gps_DataPack;                                        //GPS数据结构体
extern ctdDataPack_t g_ctdDataPack;                                             //CTD数据结构体
extern dvlDataPack_t g_dvlDataPack;                                             //DVL数据结构体
extern usblDataPack_t g_usbl_dataPack;                                      //USBL数据结构体
extern unsigned char g_dtu_recvbuf[MAX_DTU_RECV_DATA_SIZE] ;       //数传电台数据数组
extern sonarDataPack_t g_sonar_dataPack;                                //Sonar数据结构体

/************************************************************************************
 									全局变量(外界可以使用)
*************************************************************************************/
/*  Epoll管理器文件描述符   */
int g_epoll_manager_fd = -1;

/************************************************************************************
 									全局变量(仅可本文件使用)
*************************************************************************************/
/*  条件变量  && 互斥锁*/
static pthread_cond_t g_maincabin_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_maincabin_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t g_gps_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_gps_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t g_ctd_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t g_ctd_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t g_dvl_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t g_dvl_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t g_dtu_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t g_dtu_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t g_usbl_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t g_usbl_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t g_sonar_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t g_sonar_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t g_connecthost_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t g_connecthost_mutex = PTHREAD_MUTEX_INITIALIZER;

/*  任务工作标志   */
static volatile int g_maincabin_work_flag = -1;      //-1为不工作，1为开始工作
static volatile int g_gps_work_flag = -1;      //-1为不工作，1为开始工作
static volatile int g_ctd_work_flag = -1;      //-1为不工作，1为开始工作
static volatile int g_dvl_work_flag = -1;      //-1为不工作，1为开始工作
static volatile int g_dtu_work_flag = -1;      //-1为不工作，1为开始工作
static volatile int g_usbl_work_flag = -1;      //-1为不工作，1为开始工作
static volatile int g_sonar_work_flag = -1;      //-1为不工作，1为开始工作
static volatile int g_connecthost_work_flag = -1;      //-1为不工作，1为开始工作

/*******************************************************************
 * 函数原型:int Task_Database_Init(void)
 * 函数简介:初始化数据库
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int Task_Database_Init(void)
{
    /*  1.初始化数据库 */
    g_database = Database_init(g_database);
    if(g_database == NULL)
    {
        return -1;
    }

    return 0;
}


/*******************************************************************
 * 函数原型:int Task_Epoll_Init(void)
 * 函数简介:Epoll任务初始化 创建Epoll工作线程
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
int Task_Epoll_Init(void)
{
    /*  1.初始化Epoll管理器 */
    g_epoll_manager_fd = epoll_manager_create();
    if(g_epoll_manager_fd < 0)
    {
        printf("Task_Epoll_Init:Epoll 管理器创建错误\n");
        return -1;
    }

    /*  2.创建任务线程  */
    pthread_t tid;
    if(pthread_create(&tid, NULL, (void *)Task_Epoll_WorkThread, (void *)&g_epoll_manager_fd) < 0)
    {
        printf("Task_Epoll_Init:Epoll 管理器创建工作线程错误\n");
        return -1;
    }
    usleep(100000);//等待线程创建

    return 0;
}

/*******************************************************************
 * 函数原型:void *Task_Epoll_WorkThread(void *arg)
 * 函数简介:Epoll工作线程
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
void *Task_Epoll_WorkThread(void *arg)
{
    pthread_detach(pthread_self());

    printf("Epoll工作线程创建成功......\n");

    int epoll_fd = *(int *)arg;
    struct epoll_event events[30] = {0};

    while(1)
    {
        /*  等待有事件发生  */
        int nfds = epoll_manager_wait(epoll_fd, events, 30, 200);
        if(nfds < 0 || nfds == 0) continue;

        if(nfds > 0)
        {
            for(int i = 0; i < nfds; i++)
            {
                int fd = events[i].data.fd;

                /*  主控舱*/
                if(fd == MainCabin_getFD()){

                    g_maincabin_work_flag = 1;
                    pthread_cond_signal(&g_maincabin_cond);
                }

                /*  GPS */
                if(fd == GPS_getFD()){
                    g_gps_work_flag = 1;
                    pthread_cond_signal(&g_gps_cond);
                }

                /*  CTD  */
                if(fd == CTD_getFD()){
                    g_ctd_work_flag = 1;
                    pthread_cond_signal(&g_ctd_cond);
                }

                /*  DVL  */
                if(fd == DVL_getFD()){
                    g_dvl_work_flag = 1;
                    pthread_cond_signal(&g_dvl_cond);
                }

                /*  数传电台    */
                if(fd == DTU_getFD()){
                    g_dtu_work_flag = 1;
                    pthread_cond_signal(&g_dtu_cond);
                }

                /*  USBL    */
                if(fd == USBL_getFD()){
                    g_usbl_work_flag = 1;
                    pthread_cond_signal(&g_usbl_cond);
                }

                /*  上位机连接  */
                if(fd == g_connecthost_tcpser_accept_sock_fd){
                    g_connecthost_work_flag = 1;
                    pthread_cond_signal(&g_connecthost_cond);
                }
            }
        }
    }
}

 /*******************************************************************
 * 函数原型:int Task_MainCabin_Init(void)
 * 函数简介:主控舱相关任务初始化
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/ 
int Task_MainCabin_Init(void)
{
    /*  1.主控舱初始化  */
    if(MainCabin_Init() < 0)
    {
        return -1;
    }

    /*  打开释放器*/
    if(MainCabin_SwitchPowerDevice(Releaser1, 1) < 0 || MainCabin_SwitchPowerDevice(Releaser2, 1) < 0)
    {
        return -1;
    }

    /*  2.加入Epoll监听文件描述符   */
    if(epoll_manager_add_fd(g_epoll_manager_fd ,MainCabin_getFD(), EPOLLIN) < 0)
    {
        return -1;
    }
        printf("释放器已打开......\n");

    /*  3.创建工作线程  */
    pthread_t tid;
    if(pthread_create(&tid, NULL, (void *)Task_MainCabin_WorkThread, NULL) < 0)
    {
        printf("Task_MainCabin_Init:主控舱工作线程创建错误\n");
        return -1;
    }
    usleep(100000);//等待线程创建

    return 0;
}

/*******************************************************************
 * 函数原型:void *Task_MainCabin_WorkThread(void *arg)
 * 函数简介:主控舱工作线程
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/
void *Task_MainCabin_WorkThread(void *arg)
{
    while(1)
    {
        pthread_mutex_lock(&g_maincabin_mutex);
        pthread_cond_wait(&g_maincabin_cond, &g_maincabin_mutex);
        if(g_maincabin_work_flag == 1)        //开始工作
        {
            if(MainCabin_ReadRawData() == 0)
            {
                if(MainCabin_ParseData() == 0)
                {
                    char *msg = MainCabin_DataPackageProcessing();
                    int len = strlen(msg);
                    TCP_SendData(g_connecthost_tcpser_accept_sock_fd, (unsigned char *)msg, len);
                    memset(msg, 0, len);

                    Database_insertMainCabinData(g_database, &g_maincabin_data_pack);
                }
            }
            g_maincabin_work_flag = -1;
            pthread_mutex_unlock(&g_maincabin_mutex);
        }
    }
}

 /*******************************************************************
 * 函数原型:int Task_Thruster_Init(void)
 * 函数简介:推进器相关任务初始化
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/ 
int Task_Thruster_Init(void)
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

    return 0;
}

 /*******************************************************************
 * 函数原型:int Task_ConnectHost_Init(void)
 * 函数简介:上位机连接相关任务初始化
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/ 
int Task_ConnectHost_Init(void)
{
    /*  1.初始化连接 -- 监听 */
    if(ConnectHost_Init() < 0)
    {
        return -1;
    }

     /*  2.创建工作线程  */
    pthread_t tcpServertid;
	if(pthread_create(&tcpServertid, NULL, (void*)Task_ConnectHost_WorkThread, NULL) < 0)
	{
		perror("Task_ConnectHost_Init:create tcp server thread error");
		return -1;
	}

    usleep(100000);

    return 0;
}

 /*******************************************************************
 * 函数原型:void *Task_ConnectHost_WorkThread(void *arg)
 * 函数简介:上位机连接线程函数
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/ 
void *Task_ConnectHost_WorkThread(void *arg)
{
    /* 设置线程分离 */
    pthread_detach(pthread_self());

    /*  客户端信息  */
	struct sockaddr_in client_addr = {0};
    socklen_t  client_len = 0;
    client_len = sizeof(client_addr);
    char ipstr[INET_ADDRSTRLEN] = {0};

    int recvDataSize = 0;

    while(1)
    {
        while(g_connecthost_tcpserConnectFlag == -1)
        {
            g_connecthost_tcpser_accept_sock_fd = accept(g_connecthost_tcpser_listen_sock_fd, (struct sockaddr *)&client_addr, &client_len);
            if (g_connecthost_tcpser_accept_sock_fd < 0)
            {
                perror("Task_ConnectHost_WorkThread: accept");
                close(g_connecthost_tcpser_listen_sock_fd);
                pthread_exit((void *)-1);
            }

            if (inet_ntop(AF_INET, &client_addr.sin_addr.s_addr, ipstr, INET_ADDRSTRLEN) == NULL)
            {
                perror("tcp server error:inet_ntop");
                close(g_connecthost_tcpser_accept_sock_fd);
                continue;
            }

            printf("tcp 与客户端连接成功,目标IP,%s\n", ipstr);

            epoll_manager_add_fd(g_epoll_manager_fd, g_connecthost_tcpser_accept_sock_fd, EPOLLIN);

            g_connecthost_tcpserConnectFlag = 1;
        }

        while(g_connecthost_tcpserConnectFlag == 1)
        {
            if(g_connecthost_work_flag == 1)
            {
                pthread_cond_wait(&g_connecthost_cond, &g_connecthost_mutex);
                recvDataSize = recv(g_connecthost_tcpser_accept_sock_fd, g_tcpserRecvBuf, sizeof(g_tcpserRecvBuf), 0);
                if(recvDataSize == 0)
                {
                    printf("客户端：%s已断开连接\n", ipstr);
                    g_connecthost_tcpserConnectFlag = -1;			//-1为未连接，1为已连接

                    epoll_manager_del_fd(g_epoll_manager_fd, g_connecthost_tcpser_accept_sock_fd);

                    close(g_connecthost_tcpser_accept_sock_fd);
                    g_connecthost_tcpser_accept_sock_fd = -1;
                    memset(g_tcpserRecvBuf, 0, sizeof(g_tcpserRecvBuf));
                    break;
                }
                else if(recvDataSize < 0)
                {
                    perror("Task_ConnectHost_WorkThread:tcp server thread occured error, closing tcp \n");
                    epoll_manager_del_fd(g_epoll_manager_fd, g_connecthost_tcpser_accept_sock_fd);
                    close(g_connecthost_tcpser_accept_sock_fd);
                    g_connecthost_tcpser_accept_sock_fd = -1;
                    g_connecthost_tcpserConnectFlag = -1;			//-1为未连接，1为已连接
                    break;
                }
                else
                {
                    g_tcpserRecvBuf[recvDataSize] = '\0';
                    printf("tcp server recv size:%d recv data:%s\n", recvDataSize, g_tcpserRecvBuf);

                    /*		解析电机推进器指令		*/
                    if(g_tcpserRecvBuf[0] == '#' && g_tcpserRecvBuf[7] == '#' && g_tcpserRecvBuf[3] == '$' && g_tcpserRecvBuf[4] == '$')	//指令判断处理
                    {
                        // [新增] 必须添加！收到手动指令，强制结束自动定深
                        DepthControl_Stop(); 
                        char ctl_cmd[3] = {0}; 
                        int ctl_arg = 0;

                        sscanf(&g_tcpserRecvBuf[1], "%2s", ctl_cmd);
                        sscanf(&g_tcpserRecvBuf[5], "%2d", &ctl_arg);
                        Thruster_ControlHandle(ctl_cmd, ctl_arg);
                    }

                    /*		解析主控舱传感器供电指令		*/
                    else if(g_tcpserRecvBuf[0] == '=' && g_tcpserRecvBuf[recvDataSize-1] == '=')
                    {
                        if(strstr(g_tcpserRecvBuf, "open") != NULL)
                        {
                            MainCabin_PowerOnAllDeviceExceptReleaser();
                            printf("传感器已经全部供电\n");
                        }
                        else if(strstr(g_tcpserRecvBuf, "close") != NULL)
                        {
                            MainCabin_PowerOffAllDeviceExceptReleaser();
                            printf("传感器已经全部断电\n");
                        }
                    }

                    /*		解析释放器打开和关闭			*/
                    else if(g_tcpserRecvBuf[0] == '@' && g_tcpserRecvBuf[recvDataSize-1] == '@')
                    {
                        if(strstr(g_tcpserRecvBuf, "open1") != NULL)
                        {
                            if(MainCabin_SwitchPowerDevice(Releaser1, 1) == 0)
                            {
                                printf("释放器_1:已打开\n");
                            }
                        }
                        else if(strstr(g_tcpserRecvBuf, "open2") != NULL)
                        {
                            if(MainCabin_SwitchPowerDevice(Releaser2, 1) == 0)
                            {
                                printf("释放器_2:已打开\n");
                            }
                        }
                        else if(strstr(g_tcpserRecvBuf, "close1") != NULL)
                        {
                            if(MainCabin_SwitchPowerDevice(Releaser1, -1) == 0)
                            {
                                printf("释放器_1:已关闭\n");
                            }
                        }
                        else if(strstr(g_tcpserRecvBuf, "close2") != NULL)
                        {
                            if(MainCabin_SwitchPowerDevice(Releaser2, -1) == 0)
                            {
                                printf("释放器_2:已关闭\n");
                            }
                        }
                    }

                    /* 新增：解析定深控制指令  */
                // 假设 g_tcpserRecvBuf 是接收缓冲区
                else if(g_tcpserRecvBuf[0] == '!' && g_tcpserRecvBuf[recvDataSize-1] == '!')
                    {
                    if(strncmp(g_tcpserRecvBuf, "!AD:OFF!", 8) == 0)
                        {
                         DepthControl_Stop(); // 停止定深函数
                            printf("定深模式已关闭\n");
                        }
                    else if(strncmp(g_tcpserRecvBuf, "!AD:", 4) == 0)
                        {
                         double target = 0.0;
                             if(sscanf(g_tcpserRecvBuf, "!AD:%lf!", &target) == 1)
                                {
                                DepthControl_Start(target); // 启动定深函数
                                    printf("收到定深指令，目标深度: %.2f 米\n", target);
                                }
                        }
                    }
                // --- [新增] 定高控制 ---
                else if(strncmp(g_tcpserRecvBuf, "!AH:", 4) == 0) // AH = Auto Height
                    {
                        DepthControl_Stop(); // [互斥] 开启定高前，先关定深
        
                        if(strncmp(g_tcpserRecvBuf, "!AH:OFF!", 8) == 0) {
                        AltitudeControl_Stop();
                        } else {
                            double target = 0.0;
                        if(sscanf(g_tcpserRecvBuf, "!AH:%lf!", &target) == 1) {
                        AltitudeControl_Start(target);
                        }
                    }
    }


                    g_connecthost_work_flag = -1;
                }

                Database_insertTCPRecvData(g_database, g_tcpserRecvBuf);
            
                memset(g_tcpserRecvBuf, 0, sizeof(g_tcpserRecvBuf));
                recvDataSize = 0;	
            }
        }
    }
    
    return NULL;
}


 /*******************************************************************
 * 函数原型:int Task_GPS_Init(void)
 * 函数简介:GPS相关任务初始化
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/ 
int Task_GPS_Init(void)
{
    /*  GPS初始化   */
    if(GPS_Init() < 0)
    {
        return -1;
    }

    /*  2.加入Epoll监听文件描述符   */
    if(epoll_manager_add_fd(g_epoll_manager_fd ,GPS_getFD(), EPOLLIN) < 0)
    {
        return -1;
    }

    /*  3.创建工作线程  */
    pthread_t tid;
    if(pthread_create(&tid, NULL, (void *)Task_GPS_WorkThread, NULL) < 0)
    {
        printf("Task_GPS_Init:GPS工作线程创建错误\n");
        return -1;
    }
    usleep(100000);//等待线程创建
    
    return 0;
}

/*******************************************************************
 * 函数原型:void *Task_GPS_WorkThread(void *arg)
 * 函数简介:GPS工作线程
 * 函数参数:无
 * 函数返回值: NULL
 *****************************************************************/
void *Task_GPS_WorkThread(void *arg)
{
    while(1)
    {
        pthread_mutex_lock(&g_gps_mutex);
        pthread_cond_wait(&g_gps_cond, &g_gps_mutex);
        if(g_gps_work_flag == 1)        //开始工作
        {
            if(GPS_ReadRawData() > 0)
            {
                if(GPS_ParseData() != -1)
                {
                    char *msg = GPS_DataPackageProcessing();
                    int len = strlen(msg);
                    TCP_SendData(g_connecthost_tcpser_accept_sock_fd, (unsigned char *)msg, len);
                    memset(msg, 0, len);

                    Database_insertGPSData(g_database, &g_gps_DataPack);
                }
            }
            g_gps_work_flag = -1;
            pthread_mutex_unlock(&g_gps_mutex);
        }
    }

    return NULL;
}

 /*******************************************************************
 * 函数原型:int Task_CTD_Init(void)
 * 函数简介:CTD相关任务初始化
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/ 
int Task_CTD_Init(void)
{
    /*  CTD初始化   */
    if(CTD_Init() < 0)
    {
        return -1;
    }

    /*  2.加入Epoll监听文件描述符   */
    if(epoll_manager_add_fd(g_epoll_manager_fd ,CTD_getFD(), EPOLLIN) < 0)
    {
        return -1;
    }

    g_ctd_status = 1;

    /*  3.创建工作线程  */
    pthread_t tid;
    if(pthread_create(&tid, NULL, (void *)Task_CTD_WorkThread, NULL) < 0)
    {
        printf("Task_CTD_Init:CTD工作线程创建错误\n");
        return -1;
    }
    usleep(100000);//等待线程创建

    return 0;
}

/*******************************************************************
 * 函数原型:void *Task_CTD_WorkThread(void *arg)
 * 函数简介:CTD工作线程
 * 函数参数:无
 * 函数返回值: NULL
 *****************************************************************/
void *Task_CTD_WorkThread(void *arg)
{
    while(g_ctd_status == 1)
    {
        pthread_testcancel(); // 取消点
        pthread_mutex_lock(&g_ctd_mutex);
        pthread_testcancel(); // 取消点
        pthread_cond_wait(&g_ctd_cond, &g_ctd_mutex);
        if(g_ctd_work_flag == 1)        //开始工作
        {
            if(CTD_ReadRawData() == 0)
            {
                if(CTD_ParseData() == 0)
                {
                    char *msg = CTD_DataPackageProcessing();
                    int len = strlen(msg);
                    TCP_SendData(g_connecthost_tcpser_accept_sock_fd, (unsigned char *)msg, len);
                    memset(msg, 0, len);
                    Database_insertCTDData(g_database, &g_ctdDataPack);
                    // 2. [新增] 触发定深控制逻辑
                    // 只有当数据是最新的时候才计算一次控制，完美匹配 1Hz 频率
                    DepthControl_Loop(g_ctdDataPack.depth);
                }
            }
            g_ctd_work_flag = -1;
            pthread_mutex_unlock(&g_ctd_mutex);
        }
    }

    return NULL;
}

 /*******************************************************************
 * 函数原型:int Task_DVL_Init(void)
 * 函数简介:DVL相关任务初始化
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/ 
int Task_DVL_Init(void)
{
    /*  DVL初始化   */
    if(DVL_Init() < 0)
    {
        return -1;
    }

    /*  2.发送上电配置指令  */
    if(DVL_SendCmd_OpenDVLDevice() < 0)
    {
        return -1;
    }

    if(DVL_SendCmd_SetDVLSendFreq() < 0)
    {
        return -1;
    }

    /*  3.加入Epoll监听文件描述符   */
    if(epoll_manager_add_fd(g_epoll_manager_fd ,DVL_getFD(), EPOLLIN) < 0)
    {
        return -1;
    }

    g_dvl_status = 1;

    /*  3.创建工作线程  */
    pthread_t tid;
    if(pthread_create(&tid, NULL, (void *)Task_DVL_WorkThread, NULL) < 0)
    {
        printf("Task_DVL_Init:DVL工作线程创建错误\n");
        return -1;
    }
    usleep(100000);//等待线程创建

    return 0;
}

/*******************************************************************
 * 函数原型:void *Task_DVL_WorkThread(void *arg)
 * 函数简介:DVL工作线程
 * 函数参数:无
 * 函数返回值: NULL
 *****************************************************************/
void *Task_DVL_WorkThread(void *arg)
{
    while(g_dvl_status == 1)
    {
        pthread_testcancel(); // 取消点
        pthread_mutex_lock(&g_dvl_mutex);
        pthread_testcancel(); // 取消点
        pthread_cond_wait(&g_dvl_cond, &g_dvl_mutex);
        if(g_dvl_work_flag == 1)        //开始工作
        {
            if(DVL_ReadRawData() == 0)
            {
                if(DVL_ParseData() == 0)
                {
                    char *msg = DVL_DataPackageProcessing();
                    int len = strlen(msg);
                    TCP_SendData(g_connecthost_tcpser_accept_sock_fd, (unsigned char *)msg, len);
                    memset(msg, 0, len);

                    Database_insertDVLData(g_database, &g_dvlDataPack);
                    // 2. [新增] 触发定高控制逻辑
                    // 1Hz 更新率，使用 buttomDistance (注意原文件拼写是 u)
                    AltitudeControl_Loop(g_dvlDataPack.buttomDistance);
                    // [新增] 导航控制 (挂载在这里！)
                    // 利用 DVL 提供的航向角 (heading) 进行控制
                    Nav_Loop(g_dvlDataPack.heading);
                }
            }
            g_dvl_work_flag = -1;
            pthread_mutex_unlock(&g_dvl_mutex);
        }
    }

    return NULL;
}

 /*******************************************************************
 * 函数原型:int Task_DTU_Init(void)
 * 函数简介:数传电台相关任务初始化
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/ 
int Task_DTU_Init(void)
{
    /*  1.DTU初始化   */
    if(DTU_Init() < 0)
    {
        return -1;
    }

    /*  2.加入Epoll监听文件描述符   */
    if(epoll_manager_add_fd(g_epoll_manager_fd ,DTU_getFD(), EPOLLIN) < 0)
    {
        return -1;
    }

    g_dtu_status = 1;

    /*  3.创建工作线程  */
    pthread_t tid;
    if(pthread_create(&tid, NULL, (void *)Task_DTU_WorkThread, NULL) < 0)
    {
        printf("Task_DTU_Init:数传电台工作线程创建错误\n");
        return -1;
    }
    usleep(100000);//等待线程创建

    return 0;
}

/*******************************************************************
 * 函数原型:void *Task_DTU_WorkThread(void *arg)
 * 函数简介:数传电台工作线程
 * 函数参数:无
 * 函数返回值: NULL
 *****************************************************************/
void *Task_DTU_WorkThread(void *arg)
{
    while(g_dtu_status == 1)
    {
        pthread_testcancel(); // 取消点
        pthread_mutex_lock(&g_dtu_mutex);
        pthread_testcancel(); // 取消点
        pthread_cond_wait(&g_dtu_cond, &g_dtu_mutex);
        if(g_dtu_work_flag == 1)        //开始工作
        {
            if(DTU_RecvData() > 0)
            {
                DTU_ParseData();

                Database_insertDTURecvData(g_database, g_dtu_recvbuf);
            }
            g_dtu_work_flag = -1;
            pthread_mutex_unlock(&g_dtu_mutex);
        }
    }

    printf("DTU工作线程已退出\n");
    return NULL;
}

 /*******************************************************************
 * 函数原型:int Task_USBL_Init(void)
 * 函数简介:USBL相关任务初始化
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/ 
int Task_USBL_Init(void)
{
    /*  1.USBL初始化   */
    if(USBL_Init() < 0)
    {
        return -1;
    }

    /*  2.加入Epoll监听文件描述符   */
    if(epoll_manager_add_fd(g_epoll_manager_fd ,USBL_getFD(), EPOLLIN) < 0)
    {
        return -1;
    }

    g_usbl_status = 1;

    /*  3.创建工作线程  */
    pthread_t tid;
    if(pthread_create(&tid, NULL, (void *)Task_USBL_WorkThread, NULL) < 0)
    {
        printf("Task_USBL_Init:USBL工作线程创建错误\n");
        return -1;
    }
    usleep(100000);//等待线程创建

    return 0;
}

/*******************************************************************
 * 函数原型:void *Task_USBL_WorkThread(void *arg)
 * 函数简介:USBL工作线程
 * 函数参数:无
 * 函数返回值: NULL
 *****************************************************************/
void *Task_USBL_WorkThread(void *arg)
{
    while(g_usbl_status == 1)
    {
        pthread_testcancel(); // 取消点
        pthread_mutex_lock(&g_usbl_mutex);
        pthread_testcancel(); // 取消点
        pthread_cond_wait(&g_usbl_cond, &g_usbl_mutex);
        if(g_usbl_work_flag == 1)        //开始工作
        {
            if(USBL_ReadRawData() > 0)
            {
                if(USBL_ParseData() == 0)
                {
                    Database_insertUSBLData(g_database, &g_usbl_dataPack);
                }
            }
            g_usbl_work_flag = -1;
            pthread_mutex_unlock(&g_usbl_mutex);
        }
    }

    printf("USBL工作线程已退出\n");
    return NULL;
}

 /*******************************************************************
 * 函数原型:int Task_Sonar_Init(void)
 * 函数简介:Sonar相关任务初始化
 * 函数参数:无
 * 函数返回值: 成功返回0，失败返回-1
 *****************************************************************/ 
int Task_Sonar_Init(void)
{
    /*  1.Sonar初始化   */
    if(Sonar_Init() < 0)
    {
        return -1;
    }

    /*  2.发送上电配置  */
    if(Sonar_writeCmd_mtStopAlive() < 0 || Sonar_writeCmd_mtSendVersion() < 0 || Sonar_writeCmd_mtHeadCommand() < 0)
    {
        return -1;
    }

    /*  3.加入Epoll监听文件描述符   */
    if(epoll_manager_add_fd(g_epoll_manager_fd ,Sonar_getFD(), EPOLLIN) < 0)
    {
        return -1;
    }

    g_sonar_status = 1;

    /*  3.创建工作线程  */
    pthread_t tid;
    if(pthread_create(&tid, NULL, (void *)Task_Sonar_WorkThread, NULL) < 0)
    {
        printf("Task_Sonar_Init:Sonar工作线程创建错误\n");
        return -1;
    }
    usleep(100000);//等待线程创建

    return 0;
}

/*******************************************************************
 * 函数原型:void *Task_Sonar_WorkThread(void *arg)
 * 函数简介:Sonar工作线程
 * 函数参数:无
 * 函数返回值: NULL
 *****************************************************************/
void *Task_Sonar_WorkThread(void *arg)
{
    while(g_sonar_status == 1)
    {
        Sonar_SendDataRequest();

        pthread_testcancel(); // 取消点
        pthread_mutex_lock(&g_sonar_mutex);
        pthread_testcancel(); // 取消点
        pthread_cond_wait(&g_sonar_cond, &g_sonar_mutex);
        if(g_sonar_work_flag == 1)        //开始工作
        {
            if(Sonar_ReadRawData() == 0)
            {
                if(Sonar_ParseData() == 0)
                {
                    Database_insertSonarData(g_database, &g_sonar_dataPack);
                }
            }
        }

        g_sonar_work_flag = -1;
        pthread_mutex_unlock(&g_sonar_mutex);
    }

    printf("Sonar工作线程已退出\n");
    return NULL;
}

