#include "task_mission.h"
#include "../drivers/thruster/Thruster.h"
#include "../drivers/dvl/DVL.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <math.h> 

/* 全局变量 */
static volatile int g_mission_running = 0;
static MissionStep_t g_current_mission[MISSION_STEP_COUNT]; 
static pthread_mutex_t g_mission_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 辅助宏：角度归一化到 [0, 360) */
#define NORMALIZE_ANGLE(a)  (fmod((a) + 360.0f, 360.0f))

/* 辅助函数：计算角度最小差值 (target - current) -> [-180, 180] */
static float AngleDiff(float target, float current) {
    float diff = target - current;
    while (diff > 180.0f)  diff -= 360.0f;
    while (diff <= -180.0f) diff += 360.0f;
    return diff;
}

/* * 辅助函数：执行动作 (电机输出)
 * 修改：移除了上浮/下潜，保留前进后退4档逻辑
 */
static void Run_Action(uint8_t action) {
    ThrusterPowerLevel level = THRUSTER_LEVEL_1; 
    if (action == M_ACT_FORWARD || action == M_ACT_BACKWARD) level = THRUSTER_LEVEL_4;

    switch (action) {
        case M_ACT_FORWARD:  Thruster_Forward(level); break;
        case M_ACT_BACKWARD: Thruster_Backward(level); break;
        case M_ACT_LEFT:     Thruster_TurnLeft(level); break;
        case M_ACT_RIGHT:    Thruster_TurnRight(level); break;
        
        case M_ACT_STOP:     
        default:             
            // [关键修改] 仅停止水平电机，保留垂直定深推力
            Thruster_StopHorizontal(); 
            break;
    }
}

/* 闭环转向等待函数 */
static void Wait_For_Turn(float target_delta, int is_left) {
    float start_heading = DVL_getHeadingValue();
    float target_heading = start_heading;

    if (is_left) {
        target_heading = NORMALIZE_ANGLE(start_heading - target_delta);
    } else {
        target_heading = NORMALIZE_ANGLE(start_heading + target_delta);
    }

    printf("[Mission Turn] 开始转向: Start=%.1f, Target=%.1f (Delta=%.1f)\n", 
           start_heading, target_heading, target_delta);

    int has_started_turning = (target_delta >= 350.0f) ? 0 : 1;
    int max_wait_sec = (int)(target_delta / 90.0f) * 15 + 10; 
    int elapsed = 0;

    while (g_mission_running && elapsed < max_wait_sec * 10) { 
        float current_heading = DVL_getHeadingValue();
        float err = fabs(AngleDiff(target_heading, current_heading));

        if (!has_started_turning) {
            if (err > 30.0f) {
                has_started_turning = 1;
                printf("[Mission Turn] 已偏离原点，开始检测回归...\n");
            }
        } else {
            if (err < 8.0f) { 
                printf("[Mission Turn] 转向完成! Err=%.1f\n", err);
                break;
            }
        }

        usleep(100000); 
        elapsed++;
    }

    Thruster_StopHorizontal();
}

/* 任务调度线程 */
void *Task_Mission_WorkThread(void *arg) {
    printf("[Mission] 预编程任务线程就绪...\n");
    
    while (1) {
        if (!g_mission_running) {
            usleep(200000); 
            continue;
        }

        printf("[Mission] --- 开始执行预编程序列 ---\n");

        for (int i = 0; i < MISSION_STEP_COUNT; i++) {
            if (!g_mission_running) break; 

            MissionStep_t step;
            pthread_mutex_lock(&g_mission_mutex);
            step = g_current_mission[i];
            pthread_mutex_unlock(&g_mission_mutex);

            if (step.duration == 0) break; 

            // 执行动作
            Run_Action(step.action);

            // 分支逻辑：转向 vs 直行
            if (step.action == M_ACT_LEFT || step.action == M_ACT_RIGHT) {
                int cmd_val = step.duration / 10; 
                float turn_angle = 0.0f;

                if (cmd_val == 1) turn_angle = 90.0f;
                else if (cmd_val == 2) turn_angle = 180.0f;
                else if (cmd_val == 3) turn_angle = 270.0f;
                else turn_angle = 360.0f; 

                printf("[Mission] 步骤[%d]: 转向控制 -> 指令%d, 目标%.0f度\n", 
                       i+1, cmd_val, turn_angle);
                
                Wait_For_Turn(turn_angle, (step.action == M_ACT_LEFT));
            } 
            else {
                // 时间控制逻辑 (仅剩前进/后退/停止)
                printf("[Mission] 步骤[%d]: 时间控制 -> 动作%d, 持续%d秒\n", 
                       i+1, step.action, step.duration);
                
                for (int t = 0; t < step.duration; t++) {
                    if (!g_mission_running) break; 
                    sleep(1);
                }
                Thruster_StopHorizontal();
            }
            
            usleep(100000);
        }

        if (g_mission_running) {
            printf("[Mission] 序列完成，自动悬停。\n");
        } else {
            printf("[Mission] ⚠ 任务被强制中断停止！\n");
        }
        
        Thruster_StopHorizontal();
        g_mission_running = 0; 
    }
    return NULL;
}

/* 初始化 */
int Task_Mission_Init(void) {
    pthread_t tid;
    if (pthread_create(&tid, NULL, Task_Mission_WorkThread, NULL) < 0) return -1;
    return 0;
}

/* 启动任务 */
void Task_Mission_UpdateAndStart(MissionStep_t steps[MISSION_STEP_COUNT]) {
    pthread_mutex_lock(&g_mission_mutex);
    memcpy(g_current_mission, steps, sizeof(MissionStep_t) * MISSION_STEP_COUNT);
    g_mission_running = 1;
    pthread_mutex_unlock(&g_mission_mutex);
    printf("[Mission] 收到新指令，任务启动！\n");
}

/* 强制停止 */
void Task_Mission_Stop(void) {
    if (g_mission_running) {
        printf("[Mission] 收到停止信号，正在终止...\n");
        g_mission_running = 0;
        Thruster_StopHorizontal();
    }
}

/* 查询任务是否正在运行 */
int Task_Mission_IsRunning(void) {
    return g_mission_running;
}