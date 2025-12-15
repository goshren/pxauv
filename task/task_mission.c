#include "task_mission.h"
#include "../drivers/thruster/Thruster.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>

/* 全局变量 */
static volatile int g_mission_running = 0;
static MissionStep_t g_current_mission[MISSION_STEP_COUNT]; 
static pthread_mutex_t g_mission_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 辅助函数：执行动作 (固定1档) */
static void Run_Action(uint8_t action) {
    ThrusterPowerLevel level = THRUSTER_LEVEL_1; 
    switch (action) {
        case M_ACT_FORWARD:  Thruster_Forward(level); break;
        case M_ACT_BACKWARD: Thruster_Backward(level); break;
        case M_ACT_LEFT:     Thruster_TurnLeft(level); break;
        case M_ACT_RIGHT:    Thruster_TurnRight(level); break;
        case M_ACT_UP:       Thruster_Floating(level); break;
        case M_ACT_DOWN:     Thruster_Sinking(level); break;
        case M_ACT_STOP:     
        default:             Thruster_Stop(); break;
    }
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
            if (!g_mission_running) break; // 安全检查

            MissionStep_t step;
            pthread_mutex_lock(&g_mission_mutex);
            step = g_current_mission[i];
            pthread_mutex_unlock(&g_mission_mutex);

            if (step.duration == 0) break; // 结束标志

            printf("[Mission] 步骤[%d/%d]: 动作=%d, 时间=%ds\n", 
                   i+1, MISSION_STEP_COUNT, step.action, step.duration);
            
            Run_Action(step.action);

            // 倒计时 (时刻监听停止信号)
            for (int t = 0; t < step.duration; t++) {
                if (!g_mission_running) break; 
                sleep(1);
            }
        }

        // 任务结束或被中断
        if (g_mission_running) {
            printf("[Mission] 序列完成，自动悬停。\n");
        } else {
            printf("[Mission] ⚠ 任务被强制中断停止！\n");
        }
        
        Thruster_Stop();
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
        Thruster_Stop();
    }
}

/* [新增] 查询任务是否正在运行 */
int Task_Mission_IsRunning(void) {
    return g_mission_running;
}