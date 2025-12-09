#ifndef __TASK_THREAD_H__
#define __TASK_THREAD_H__

#include <pthread.h>

/************************************************************************************
 									函数原型
*************************************************************************************/
/*	数据库相关任务初始化	*/
int Task_Database_Init(void);

/*	Epoll相关任务初始化	*/
int Task_Epoll_Init(void);
void *Task_Epoll_WorkThread(void *arg);

/*  主控舱相关任务初始化    */
int Task_MainCabin_Init(void);
void *Task_MainCabin_WorkThread(void *arg);

/*  推进器相关任务初始化    */
int Task_Thruster_Init(void);

/*  上位机连接相关任务初始化    */
int Task_ConnectHost_Init(void);
void *Task_ConnectHost_WorkThread(void *arg);

/*  GPS相关任务初始化    */
int Task_GPS_Init(void);
void *Task_GPS_WorkThread(void *arg);

/*	CTD相关任务初始化	*/
int Task_CTD_Init(void);
void *Task_CTD_WorkThread(void *arg);

/*	DVL相关任务初始化	*/
int Task_DVL_Init(void);
void *Task_DVL_WorkThread(void *arg);

/*	数传电台相关任务初始化	*/
int Task_DTU_Init(void);
void *Task_DTU_WorkThread(void *arg);

/*	USBL相关任务初始化	*/
int Task_USBL_Init(void);
void *Task_USBL_WorkThread(void *arg);

/*	Sonar相关任务初始化	*/
int Task_Sonar_Init(void);
void *Task_Sonar_WorkThread(void *arg);


#endif

