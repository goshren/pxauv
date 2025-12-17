#ifndef __TASK_MISSION_H__
#define __TASK_MISSION_H__

#include <pthread.h>
#include <stdint.h>

#define MISSION_STEP_COUNT  3    // 定义3个动作

/* 动作定义 */
typedef enum {
    M_ACT_STOP = 0,
    M_ACT_FORWARD = 1,
    M_ACT_BACKWARD = 2,
    M_ACT_LEFT = 3,
    M_ACT_RIGHT = 4,
    M_ACT_UP = 5,
    M_ACT_DOWN = 6
} MissionAction_t;

/* 任务步骤结构体 */
typedef struct {
    uint8_t action;      // 动作ID
    uint8_t duration;    // 持续时间(秒)
} MissionStep_t;

/* 初始化 */
int Task_Mission_Init(void);

/* 更新任务并启动 (USBL调用) */
void Task_Mission_UpdateAndStart(MissionStep_t steps[MISSION_STEP_COUNT]);

/* 安全中断停止 (USBL特殊指令调用) */
void Task_Mission_Stop(void);

/* [新增] 查询任务是否正在运行 */
int Task_Mission_IsRunning(void);

#endif