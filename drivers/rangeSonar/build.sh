#! /bin/bash

gcc *.c  ../../sys/socket/TCP/tcp.c ../../sys/epoll/epoll_manager.c  -lpthread -Wall