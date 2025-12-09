#include "epoll_manager.h"


static int g_epoll_fd = -1;  // epoll实例文件描述符

/**
 * @brief 初始化epoll管理器
 * @return 成功返回0，失败返回-1
 */
int epoll_manager_init(void)
{
    // 防止重复初始化
    if(g_epoll_fd >= 0)
    {
        return 0;
    }

    // 创建epoll实例
    g_epoll_fd = epoll_create1(0);
    if(g_epoll_fd == -1)
    {
        perror("epoll_create1 failed");
        return -1;
    }

    return 0;
}

/**
 * @brief 获取epoll文件描述符
 * @return epoll文件描述符
 */
int epoll_manager_get_fd(void)
{
    return g_epoll_fd;
}

/**
 * @brief 添加文件描述符到epoll监控
 * @param fd 要监控的文件描述符
 * @param events 要监控的事件(EPOLLIN等)
 * @return 成功返回0，失败返回-1
 */
int epoll_manager_add_fd(int fd, uint32_t events)
{
    if(g_epoll_fd < 0 || fd < 0)
    {
        errno = EINVAL;
        return -1;
    }

    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;  // 直接存储文件描述符

    if(epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        perror("epoll_ctl ADD failed");
        return -1;
    }

    return 0;
}


/**
 * @brief 修改已监控文件描述符的事件
 * @param fd 文件描述符
 * @param events 新的事件
 * @return 成功返回0，失败返回-1
 */
int epoll_manager_mod_fd(int fd, uint32_t events)
{
    if(g_epoll_fd < 0 || fd < 0)
    {
        errno = EINVAL;
        return -1;
    }

    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    if(epoll_ctl(g_epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1)
    {
        perror("epoll_ctl MOD failed");
        return -1;
    }

    return 0;
}


/**
 * @brief 从epoll监控中移除文件描述符
 * @param fd 要移除的文件描述符
 * @return 成功返回0，失败返回-1
 */
int epoll_manager_del_fd(int fd)
{
    if (g_epoll_fd < 0 || fd < 0) {
        errno = EINVAL;
        return -1;
    }

    // 从epoll中删除fd，event参数可以为NULL
    if(epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1)
    {
        // 如果fd已经被关闭或不存在，忽略这个错误
        if(errno != EBADF && errno != ENOENT)
        {
            perror("epoll_ctl DEL failed");
            return -1;
        }
    }

    return 0;
}


/**
 * @brief 等待事件发生
 * @param events 输出参数，用于存储发生的事件
 * @param maxevents 最多返回的事件数
 * @param timeout 超时时间(毫秒)，-1表示阻塞
 * @return 成功返回就绪事件数，失败返回-1
 */
int epoll_manager_wait(struct epoll_event *events, int maxevents, int timeout)
{
    if (g_epoll_fd < 0 || !events || maxevents <= 0)
    {
        errno = EINVAL;
        return -1;
    }

    int nfds = epoll_wait(g_epoll_fd, events, maxevents, timeout);
    if (nfds == -1) {
        // 被信号中断不算错误
        if (errno != EINTR) {
            perror("epoll_wait failed");
        }
        return -1;
    }

    return nfds;
}

/**
 * @brief 清理epoll管理器资源
 */
void epoll_manager_cleanup(void)
{
    if (g_epoll_fd >= 0) {
        close(g_epoll_fd);
        g_epoll_fd = -1;
    }
}