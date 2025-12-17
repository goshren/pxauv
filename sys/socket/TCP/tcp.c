/************************************************************************************
                    文件名：tcp.c
                    文件说明：主要编写TCP客户端、服务器、发送数据的代码
                    最后一次修改时间：2025/6/27
*************************************************************************************/

#include "tcp.h"

/*******************************************************************
 * 函数原型:int TCP_InitClient(char *ipaddr , unsigned short int port)
 * 函数简介:TCP客户端初始化
 * 函数参数:ipaddr：目标IP地址，port：目标端口号
 * 函数返回值: 成功时返回客户端的套接字文件描述符，失败时返回-1
 *****************************************************************/
int TCP_InitClient(char *ipaddr , unsigned short int port)
{
    /*  1.创建套接字文件    */

    int tcp_clientfd = -1;
    tcp_clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if(tcp_clientfd < 0)
    {
        perror("TCP_InitClient:socket error");
        return -1;
    }

    /*  2.设置端口复用  */
    int optval = 1;                                 // 这里设置为端口复用，所以随便写一个值
    int rev = -1;
    rev = setsockopt(tcp_clientfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if(rev == -1)
    {
        perror("TCP_InitClient:setsockopt fail");
        close(tcp_clientfd);
        return -1;
    }

    /*  3.使用connect连接到目标IP   */
    struct sockaddr_in addr_init = {0};
    addr_init.sin_family = AF_INET;
    addr_init.sin_addr.s_addr = inet_addr(ipaddr);
    addr_init.sin_port = htons(port);
    socklen_t addrlen = sizeof(addr_init);

    rev = connect(tcp_clientfd, (struct sockaddr *)&addr_init, addrlen);

    /*  4.连接服务器 -- 设置简单的超时重连    */
/*   
    for(int i = 0; i < 5; i++)
    {
        rev = connect(tcp_clientfd, (struct sockaddr *)&addr_init, addrlen);
        
        if(rev < 0)
        {
            perror("TCP_InitClient:connect error");
            printf("TCP_InitClient:try to connect after 1 second\n");
            sleep(1);

            if(i == -1)
            {
                printf("TCP_InitClient:server log out, please connect again later\n");
                close(tcp_clientfd);
                return -1;
            }
        }
        
        else
        {
            break; 
        }
    }
*/    
    printf("TCP_InitClient:目标(服务器)IP:%s, 端口号:%d\n", ipaddr, port);
    return tcp_clientfd;
}


/*******************************************************************
 * 函数原型:int TCP_InitServer(char *ipaddr , unsigned short int port)
 * 函数简介:TCP服务器初始化
 * 函数参数:ipaddr：本机IP地址，port：目标端口号
 * 函数返回值: 成功时返回服务器的监听套接字文件描述符，失败时返回-1
 *****************************************************************/
int TCP_InitServer(char *ipaddr , unsigned short int port)
{
    /*  1.创建套接字文件    */
    int tcp_serverfd = -1;
    tcp_serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if(tcp_serverfd < 0)
    {
        perror("TCP_InitServer:socket error");
        return -1;
    }

    /*  2.设置端口复用  */
    int optval = 1;                                 // 这里设置为端口复用，所以随便写一个值
    int rev = -1;
    rev = setsockopt(tcp_serverfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if(rev == -1)
    {
        perror("TCP_InitServer:setsockopt fail");
        close(tcp_serverfd);
        return -1;
    }

    /*  3.绑定IP和端口号   */
    struct sockaddr_in addr_init = {0};
    addr_init.sin_family = AF_INET;
    addr_init.sin_addr.s_addr = inet_addr(ipaddr);
    addr_init.sin_port = htons(port);
    socklen_t addrlen = sizeof(addr_init);

    rev = bind(tcp_serverfd, (struct sockaddr *)&addr_init, addrlen);
    if(rev < 0)
    {
        perror("TCP_InitServer:bind error");
        return -1;
    }

    /*  4.监听   */
    rev = listen(tcp_serverfd, TCP_MAX_LISTEN);
    if(rev < 0)
    {
        perror("TCP_InitServer:listen error");
        return -1;
    }

    return tcp_serverfd;
}


/*******************************************************************
 * 函数原型: int TCP_SendData(int tcpfd, unsigned char *databuf, int datasize)
 * 功能: TCP阻塞式发送数据
 * 参数:
 *   tcpfd - 套接字文件描述符
 *   databuf - 要发送的数据缓冲区
 *   datasize - 数据长度(字节数)
 * 返回值:
 *   成功返回发送的长度，失败返回-1
 * 注意事项:
 *   1. 会自动处理EINTR中断
 *   2. 确保datasize不超过TCP_MAX_SEND_SIZE
 ********************************************************************/
int TCP_SendData(int tcpfd, const unsigned char *databuf, int datasize) 
{
    /* 0. 入口检查 */
    if(tcpfd < 0 || databuf == NULL || datasize <= 0 || datasize > TCP_MAX_SEND_SIZE-1)
    {
        return -1;
    }

    /* 1. 发送数据（带重试机制）*/
    int total_sent = 0;
    while(total_sent < datasize)
    {
        int sent = send(tcpfd, databuf + total_sent, datasize - total_sent, MSG_NOSIGNAL); // 避免SIGPIPE信号

        if (sent < 0)
        {
            if (errno == EINTR)
            {
                continue; // 被信号中断，重试
            }
            
            // [新增] 优雅处理连接断开的情况
            if (errno == EPIPE || errno == ECONNRESET)
            {
                // 对方已断开连接，这是正常现象，直接返回失败即可，不要 perror 刷屏
                // printf("TCP: 客户端已断开 (EPIPE/RESET)\n"); // 可选：仅调试时打开
                return -1;
            }

            perror("TCP_SendData:send failed");
            return -1;
        }

        total_sent += sent;
    }

    return total_sent;
}


/*******************************************************************
* 函数原型:int TCP_RecvData(unsigned char *tcpReadBuf, size_t bufsize) 
* 函数功能: 从TCP接收数据
* 参数说明:tcpfd:tcp的文件描述符
* 参数说明:tcpReadBuf:数据存储缓冲区
* 参数说明:bufsize:缓冲区大小
* 参数说明:nbyte:接收的数据大小
* 参数说明:timeout_ms:超时时间
* 返回值:成功返回接收数据的长度，失败返回-1
********************************************************************/
int TCP_RecvData(int tcpfd, unsigned char *tcpReadBuf, size_t bufsize, int nbyte,int timeout_ms) 
{
	/* 0.*入口检查	*/
    if(tcpfd < 0 || tcpReadBuf == NULL || bufsize >= TCP_MAX_RECV_SIZE || bufsize < 0 || nbyte <=0 || timeout_ms <= 0)
	{
        return -1;
    }
    
    memset(tcpReadBuf, 0, bufsize);


	/*	1.使用epoll机制监听是否有可读数据	*/
	int epoll_fd = epoll_create1(0);
    if(epoll_fd == -1)
	{
        perror("TCP_RecvData:epoll_create1");
        return -1;
    }

	struct epoll_event event = {0};
	event.data.fd = tcpfd;		 //监听的文件描述符
	event.events = EPOLLIN;			//监听读取事件
	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcpfd, &event) == -1)
	{
        perror("TCP_RecvData:epoll_ctl");
        close(epoll_fd);
        return -1;
    }


    /* 非阻塞读取设置 */
    int orig_flags = fcntl(tcpfd, F_GETFL, 0);
    fcntl(tcpfd, F_SETFL, orig_flags | O_NONBLOCK);

	/*	2.超时设置	*/
	struct timespec start, current;
	clock_gettime(CLOCK_MONOTONIC, &start);		//获取开始时间

	int elapsed_ms = 0;			//已花费的时间
	int remaining_ms = 0;	 //剩余时间
    int total_read = 0;

	while(total_read < nbyte)
	{
		/*	获取剩余时间	*/
        clock_gettime(CLOCK_MONOTONIC, &current); // 计算剩余超时时间
        elapsed_ms = (current.tv_sec - start.tv_sec) * 1000 + (current.tv_nsec - start.tv_nsec) / 1000000;
        remaining_ms = timeout_ms - elapsed_ms;
        
		/*	超时结束循环	*/
        if(remaining_ms <= 0) break;

        /*	等待读取事件	*/
        int nfds = epoll_wait(epoll_fd, &event, 2, remaining_ms);
        if(nfds == -1)
		{
			/*	系统调用中断	*/
            if(errno == EINTR)
			{
				continue;		
			}
            perror("TCP_RecvData:epoll_wait");
            break;
        }
		else if(nfds == 0) 
		{
			/*	超时	*/
			//perror("TCP_RecvData:nfds");
            break; 
        }

        /*	读取数据	*/
        ssize_t n = recv(tcpfd, tcpReadBuf + total_read, nbyte - total_read, 0);
        if(n > 0)
        {
            total_read += n;
        }
        else if (n == 0)
        {
            printf("TCP_RecvData:Connection closed by peer\n");
            break;
        }
        else
        {
            if(errno != EAGAIN && errno != EWOULDBLOCK)
            {
                perror("TCP_RecvData:recv");
                break;
            }
        }
    }

     /* 清理资源 */
    fcntl(tcpfd, F_SETFL, orig_flags); // 恢复原flags
    close(epoll_fd);

    return (total_read > 0) ? total_read : -1;
}



int TCP_RecvData_Block(int tcpfd, unsigned char *tcpReadBuf, size_t bufsize, int nbyte) 
{
    /* 入口检查 */
    if(tcpfd < 0 || tcpReadBuf == NULL || bufsize >= TCP_MAX_RECV_SIZE || bufsize < 0 || nbyte <= 0)
    {
        return -1;
    }
    
    memset(tcpReadBuf, 0, bufsize);

    int total_read = 0;
    while(total_read < nbyte)
    {
        /* 阻塞读取数据 */
        ssize_t n = recv(tcpfd, tcpReadBuf + total_read, nbyte - total_read, 0);
     
        if(n > 0)
        {
            total_read += n;
        } 
        else if(n == 0)     /* 对端关闭连接 */
        {
            break;
        } 
        else                /* 错误处理 */
        {
            if(errno == EINTR)
            {
                continue;  // 被信号中断，继续尝试
            }
            break;
        }
    }

    return (total_read > 0) ? total_read : -1;
}