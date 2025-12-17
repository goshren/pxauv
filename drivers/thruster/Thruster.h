/************************************************************************************
					文件名：Thruster.h
                    描述：推进器控制模块头文件
					最后修改时间：2025-06-26
 ************************************************************************************/

#ifndef __THRUSTER_H__
#define __THRUSTER_H__


/************************************************************************************
								包含头文件
*************************************************************************************/
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <termios.h>
#include <pthread.h>
#include <stddef.h>


/************************************************************************************
								枚举
*************************************************************************************/
/*  推进器电机ID    */
typedef enum {
    THRUSTER_MOTOR_1 = 1,               //电机1
    THRUSTER_MOTOR_2,                      //电机2
    THRUSTER_MOTOR_3,                      //电机3
    THRUSTER_MOTOR_4,                      //电机4
    THRUSTER_ALL_MOTORS = 0xFF //电机5
} ThrusterMotorID;

/*  推进器功率等级  */
typedef enum {
    THRUSTER_STOP = 0,                      //停止，档位0
    THRUSTER_LEVEL_1,                       //档位1
    THRUSTER_LEVEL_2,                       //档位2
    THRUSTER_LEVEL_3,                       //档位3
    THRUSTER_LEVEL_4,                       //档位4
    THRUSTER_LEVEL_5                        //档位5
} ThrusterPowerLevel;

/*  推进器运动方向  */
typedef enum {
    THRUSTER_DIR_FORWARD,           //正转
    THRUSTER_DIR_BACKWARD          //反转
} ThrusterDirection;


/************************************************************************************
 									函数原型
*************************************************************************************/
/*  初始化与清理    */
int Thruster_Init_default(void);
int Thruster_Init(void);
void Thruster_Cleanup_default(void);
void Thruster_Cleanup(void);

/*  发送上电配置指令    */
int Thruster_SendInitConfig(void);

/*  基础控制    */
int Thruster_SendCommand(ThrusterMotorID motor, const unsigned char *cmd, size_t len);
int Thruster_SetMotorPower(ThrusterMotorID motor, ThrusterPowerLevel level, ThrusterDirection dir);

/*  运动控制    */
int Thruster_Stop(void);
// [新增] 仅停止水平电机 (1, 2号)
int Thruster_StopHorizontal(void);
// [新增] 仅停止垂直电机 (3, 4号)
int Thruster_StopVertical(void);
int Thruster_Floating(ThrusterPowerLevel level);
int Thruster_Sinking(ThrusterPowerLevel level);
int Thruster_Forward(ThrusterPowerLevel level);
int Thruster_Backward(ThrusterPowerLevel level);
int Thruster_TurnLeft(ThrusterPowerLevel level);
int Thruster_TurnRight(ThrusterPowerLevel level);

/*  线程控制    */
pthread_t Thruster_StartHeartbeatThread(void);
void Thruster_StopHeartbeatThread(pthread_t tid);

/*  高级控制接口    */
void Thruster_ControlHandle(const char *cmd, int arg);

/*  整体初始化 -- 一键使用  */
int Thruster_Task(void);

#endif

