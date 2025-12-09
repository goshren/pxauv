#include "connectHost.h"
#include "../../sys/socket/TCP/tcp.h"
#include "../../drivers/thruster/Thruster.h"
#include "../../drivers/maincabin/MainCabin.h"

#include "../../sys/sqlite3_db/Database.h"

extern sqlite3 *g_database;

/************************************************************************************
 									全局变量(其他文件可使用)
*************************************************************************************/
/*	设备的文件描述符	*/
int g_connecthost_tcpser_listen_sock_fd = -1;						//作为服务器的监听文件描述符
int g_connecthost_tcpser_accept_sock_fd = -1;					  //作为客户端的连接文件描述符

/*	与服务器端连接的状态	*/
volatile int g_connecthost_tcpserConnectFlag = -1;		//-1为未连接，1为已连接

/*  发送接收的缓冲区    */
char g_tcpserRecvBuf[256];

/*******************************************************************
* 函数原型:void *ConnectHost_Thread_TcpServer1(void *argv)
* 函数简介:服务器线程  新版
* 函数参数:无
* 函数返回值: 无
*******************************************************************/ 
void *ConnectHost_Thread_TcpServer1(void)
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
            /*  阻塞等待客户端连接  */
            g_connecthost_tcpser_accept_sock_fd = accept(g_connecthost_tcpser_listen_sock_fd, (struct sockaddr *)&client_addr, &client_len);
            if (g_connecthost_tcpser_accept_sock_fd < 0)
            {
                perror("tcp server error:accept");
                close(g_connecthost_tcpser_listen_sock_fd);
                break;
            }

            if (inet_ntop(AF_INET, &client_addr.sin_addr.s_addr, ipstr, INET_ADDRSTRLEN) == NULL)
            {
                perror("tcp server error:inet_ntop");
                close(g_connecthost_tcpser_accept_sock_fd);
                continue;
            }

            printf("tcp 与客户端连接成功,目标IP,%s\n", ipstr);
            g_connecthost_tcpserConnectFlag = 1;			//-1为未连接，1为已连接
        }
            
        while(g_connecthost_tcpserConnectFlag == 1)
        {
            recvDataSize = recv(g_connecthost_tcpser_accept_sock_fd, g_tcpserRecvBuf, sizeof(g_tcpserRecvBuf), 0);
            if(recvDataSize == 0)
            {
                printf("客户端：%s已断开连接\n", ipstr);
                g_connecthost_tcpserConnectFlag = -1;			//-1为未连接，1为已连接
                close(g_connecthost_tcpser_accept_sock_fd);
                g_connecthost_tcpser_accept_sock_fd = -1;
                memset(g_tcpserRecvBuf, 0, sizeof(g_tcpserRecvBuf));
                break;
            }
            else if(recvDataSize < 0)
            {
                perror("tcp server thread occured error, closing tcp \n");
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
                    }
                    else if(strstr(g_tcpserRecvBuf, "close") != NULL)
                    {
                        MainCabin_PowerOffAllDeviceExceptReleaser();
                    }
                }

                /*		解析释放器打开和关闭			*/
                else if(g_tcpserRecvBuf[0] == '@' && g_tcpserRecvBuf[recvDataSize-1] == '@')
                {
                    if(strstr(g_tcpserRecvBuf, "open1") != NULL)
                    {
                        MainCabin_SwitchPowerDevice(Releaser1, 1);
                    }
                    else if(strstr(g_tcpserRecvBuf, "open2") != NULL)
                    {
                        MainCabin_SwitchPowerDevice(Releaser2, 1);
                    }
                    else if(strstr(g_tcpserRecvBuf, "close1") != NULL)
                    {
                        MainCabin_SwitchPowerDevice(Releaser1, -1);
                    }
                    else if(strstr(g_tcpserRecvBuf, "close2") != NULL)
                    {
                        MainCabin_SwitchPowerDevice(Releaser2, -1);
                    }
                }

                Database_insertTCPRecvData(g_database, g_tcpserRecvBuf);
            
                memset(g_tcpserRecvBuf, 0, sizeof(g_tcpserRecvBuf));
                recvDataSize = 0;	
            }
        }
    }    

    close(g_connecthost_tcpser_listen_sock_fd);
    g_connecthost_tcpser_listen_sock_fd = -1;

    pthread_exit((void *)0);
}


/*******************************************************************
* 函数原型:void *ConnectHost_Thread_TcpServer(void *argv)
* 函数简介:服务器线程  老版
* 函数参数:无
* 函数返回值: 无
*******************************************************************/  
void *ConnectHost_Thread_TcpServer(void)
{
	/* 设置线程分离 */
    pthread_detach(pthread_self());

    /*  客户端信息  */
	struct sockaddr_in client_addr = {0};
    socklen_t  client_len = 0;
    client_len = sizeof(client_addr);
    char ipstr[INET_ADDRSTRLEN] = {0};

    while(1)
    {
        g_connecthost_tcpser_accept_sock_fd = accept(g_connecthost_tcpser_listen_sock_fd, (struct sockaddr *)&client_addr, &client_len);
        if (g_connecthost_tcpser_accept_sock_fd < 0)
        {
            perror("tcp server error:accept");
            close(g_connecthost_tcpser_listen_sock_fd);
            pthread_exit((void *)-1);
        }

        g_connecthost_tcpserConnectFlag = 1;			//-1为未连接，1为已连接
		
		int recvDataSize = 0;
        while(1)
        {
			recvDataSize = recv(g_connecthost_tcpser_accept_sock_fd, g_tcpserRecvBuf, sizeof(g_tcpserRecvBuf), 0);
			if(recvDataSize == 0)
			{
			 	printf("客户端：%s已断开连接\n", ipstr);
			 	g_connecthost_tcpserConnectFlag = -1;			//-1为未连接，0为已连接
			 	memset(g_tcpserRecvBuf, 0, strlen(g_tcpserRecvBuf));
			 	break;
			}
			else if(recvDataSize < 0)
			{
			 	perror("tcp server thread occured error, closing tcp \n");
			 	g_connecthost_tcpserConnectFlag = -1;			//-1为未连接，0为已连接
			 	break;
			}
			else
			{
			 	g_tcpserRecvBuf[recvDataSize] = '\0';
			 	printf("tcp server recv size:%d recv data:%s\n", recvDataSize, g_tcpserRecvBuf);

				/*		解析电机推进器指令		*/
				if(g_tcpserRecvBuf[0] == '#' && g_tcpserRecvBuf[7] == '#' && g_tcpserRecvBuf[3] == '$' && g_tcpserRecvBuf[4] == '$')	//指令判断处理
				{
					char ctl_cmd[3] = {'\0'}; 
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
                    }
                    else if(strstr(g_tcpserRecvBuf, "close") != NULL)
                    {
                        MainCabin_PowerOffAllDeviceExceptReleaser();
                    }
                }

				/*		解析释放器打开和关闭			*/
                else if(g_tcpserRecvBuf[0] == '@' && g_tcpserRecvBuf[recvDataSize-1] == '@')
                {
                    if(strstr(g_tcpserRecvBuf, "open1") != NULL)
                    {
                        MainCabin_SwitchPowerDevice(Releaser1, 1);
                    }
                    else if(strstr(g_tcpserRecvBuf, "open2") != NULL)
                    {
                        MainCabin_SwitchPowerDevice(Releaser2, 1);
                    }
                    else if(strstr(g_tcpserRecvBuf, "close1") != NULL)
                    {
                        MainCabin_SwitchPowerDevice(Releaser1, -1);
                    }
                    else if(strstr(g_tcpserRecvBuf, "close2") != NULL)
                    {
                        MainCabin_SwitchPowerDevice(Releaser2, -1);
                    }
                }

				Database_insertTCPRecvData(g_database, g_tcpserRecvBuf);
		 	
			 	memset(g_tcpserRecvBuf, 0, strlen(g_tcpserRecvBuf));
			 	recvDataSize = 0;	
			}
        }
        
        close(g_connecthost_tcpser_accept_sock_fd);
    }

    close(g_connecthost_tcpser_listen_sock_fd);
    pthread_exit((void *)0);
}


/*******************************************************************
* 函数原型:int ConnectHost_Init(void)
* 函数简介:初始化服务器
* 函数参数:无
* 函数返回值: 成功返回0，失败返回-1
*******************************************************************/ 
int ConnectHost_Init(void)
{
    /*	1.初始化服务器	*/
	g_connecthost_tcpser_listen_sock_fd = TCP_InitServer(TCP_SERVER_IP, TCP_SERVER_PORT);
	if(g_connecthost_tcpser_listen_sock_fd < 0)
	{
		return -1;
	}

	/*	2.忽略SIGPIPE信号	*/
	signal(SIGPIPE, SIG_IGN);

	return 0;
}


