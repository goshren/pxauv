#! /bin/bash

# 注意：加入了 ../control/*.c
gcc *.c ../control/*.c ../drivers/*/*.c  ../sys/SerialPort/SerialPort.c ../sys/socket/TCP/tcp.c ../sys/epoll/epoll_manager.c  ../sys/sqlite3_db/Database.c ../tool/tool.c -lpthread ../task/*.c -lm -lsqlite3 -Wall