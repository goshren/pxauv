#ifndef __EPOLL_MANAGER_H__
#define __EPOLL_MANAGER_H__

/************************************************************************************
 									包含头文件
*************************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>


/************************************************************************************
 									函数原型
*************************************************************************************/
int epoll_manager_create(void);
int epoll_manager_add_fd(int epoll_fd , int fd, uint32_t events);
int epoll_manager_mod_fd(int epoll_fd, int fd, uint32_t events);
int epoll_manager_del_fd(int epoll_fd, int fd);
int epoll_manager_wait(int epoll_fd, struct epoll_event *events, int maxevents, int timeout);
void epoll_manager_cleanup(int epoll_fd);


#endif 