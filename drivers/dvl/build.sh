#! /bin/bash

gcc *.c ../maincabin/MainCabin.c ../../sys/SerialPort/SerialPort.c ../../sys/socket/TCP/tcp.c ../../sys/epoll/epoll_manager.c ../../tool/tool.c -lpthread -lm -Wall