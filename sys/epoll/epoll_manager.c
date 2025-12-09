#include "epoll_manager.h"


/*******************************************************************
* 函数原型:int epoll_manager_create(void)
* 函数简介:创建一个epoll管理器
* 函数参数:无
* 函数返回值:成功返回epoll管理器的文件描述符，失败返回-1。
*****************************************************************/
int epoll_manager_create(void)
{
    /*  1.创建epoll实例 */
    int fd = -1;
    fd = epoll_create1(0);
    if(fd < 0)
    {
        printf("epoll_manager_create:创建Epoll管理器失败\n");
        return -1;
    }

    return fd;
}

/*******************************************************************
* 函数原型:int epoll_manager_add_fd(int epoll_fd , int fd, uint32_t events)
* 函数简介:将要监控的fd文件描述符添加到指定的epoll管理器
* 函数参数:epoll_fd:epoll管理器
* 函数参数:fd:要监控的文件描述符
* 函数参数:events 要监控的事件(EPOLLIN等)
* 函数返回值:成功0，失败返回-1。
*****************************************************************/
int epoll_manager_add_fd(int epoll_fd , int fd, uint32_t events)
{
    if(epoll_fd < 0 || fd < 0)
    {
        return -1;
    }

    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;  // 直接存储文件描述符

    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        printf("epoll_manager_add_fd:添加文件描述符 %d 到Epoll管理器 %d 失败\n", fd, epoll_fd);
        return -1;
    }

    return 0;
}

/*******************************************************************
* 函数原型:int epoll_manager_mod_fd(int epoll_fd, int fd, uint32_t events)
* 函数简介:修改指定的epoll管理器中的已监控文件描述符的事件
* 函数参数:epoll_fd:epoll管理器
* 函数参数:fd:要监控的文件描述符
* 函数参数:events 新的事件
* 函数返回值:成功0，失败返回-1。
*****************************************************************/
int epoll_manager_mod_fd(int epoll_fd, int fd, uint32_t events)
{
    if(epoll_fd < 0 || fd < 0)
    {
        return -1;
    }

    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1)
    {
        printf("epoll_manager_mod_fd:Epoll管理器 %d 中的文件描述符 %d 修改监控事件失败\n", epoll_fd, fd);
        return -1;
    }

    return 0;
}

/*******************************************************************
* 函数原型:int epoll_manager_del_fd(int epoll_fd, int fd)
* 函数简介:从epoll监控中移除文件描述符
* 函数参数:epoll_fd:epoll管理器
* 函数参数:fd:要删除的文件描述符
* 函数返回值:成功0，失败返回-1。
*****************************************************************/
int epoll_manager_del_fd(int epoll_fd, int fd)
{
    if (epoll_fd < 0 || fd < 0)
    {
        return -1;
    }

    // 从epoll中删除fd，event参数可以为NULL
    if(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1)
    {
        // 如果fd已经被关闭或不存在，忽略这个错误
        if(errno != EBADF && errno != ENOENT)
        {
            printf("epoll_manager_del_fd:Epoll管理器 %d 中的文件描述符 %d 删除失败\n", epoll_fd, fd);
            return -1;
        }
    }

    return 0;
}

/*******************************************************************
* 函数原型:int epoll_manager_wait(int epoll_fd, struct epoll_event *events, int maxevents, int timeout)
* 函数简介:等待事件发生
* 函数参数:epoll_fd:epoll管理器
* 函数参数:events 输出参数，用于存储发生的事件
* 函数参数:maxevents 最多返回的事件数
* 函数参数:timeout 超时时间(毫秒)，-1表示阻塞
* 函数返回值:成功返回就绪事件数，失败返回-1。
*****************************************************************/
int epoll_manager_wait(int epoll_fd, struct epoll_event *events, int maxevents, int timeout)
{
    if (epoll_fd < 0 || events == NULL || maxevents <= 0)
    {
        return -1;
    }

    int nfds = epoll_wait(epoll_fd, events, maxevents, timeout);
    if (nfds == -1)
    {
        // 被信号中断不算错误
        if (errno != EINTR)
        {
            printf("epoll_manager_wait:Epoll管理器 %d 等待事件发生出错\n", epoll_fd);
        }
        return -1;
    }

    return nfds;
}

/*******************************************************************
* 函数原型:void epoll_manager_cleanup(int epoll_fd)
* 函数简介:清理epoll管理器资源
* 函数参数:epoll_fd:epoll管理器
* 函数返回值:无。
*****************************************************************/
void epoll_manager_cleanup(int epoll_fd)
{
    if (epoll_fd >= 0)
    {
        close(epoll_fd);
        epoll_fd = -1;
    }
}
