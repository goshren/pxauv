#ifndef EPOLL_MANAGER_SIMPLE_H
#define EPOLL_MANAGER_SIMPLE_H

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>

int epoll_manager_init(void);
int epoll_manager_get_fd(void);
int epoll_manager_add_fd(int fd, uint32_t events);
int epoll_manager_mod_fd(int fd, uint32_t events);
int epoll_manager_del_fd(int fd);
int epoll_manager_wait(struct epoll_event *events, int maxevents, int timeout);
void epoll_manager_cleanup(void);


#endif // EPOLL_MANAGER_SIMPLE_H