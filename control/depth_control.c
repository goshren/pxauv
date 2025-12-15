#include "depth_control.h"
#include "../drivers/thruster/Thruster.h"
#include <stdio.h>
#include <math.h>
#include <time.h> // 引入时间头文件

static time_t g_last_control_time = 0; // 上次控制的时间

volatile int g_depth_control_enabled = 0;
volatile double g_target_depth = 0.0;

// 参数调优宏定义
#define DEAD_ZONE       0.50   // 死区：±50cm
#define LEVEL_LOW_TH    1.00   // 低速阈值：1m
#define LEVEL_HIGH_TH   2.00   // 高速阈值：2m

// 浮力配平档位 (如果机器人在水里会自动上浮，这里可能需要填 1 或 2，表示停止时其实要保持微弱下潜)
#define BUOYANCY_COMPENSATION_LEVEL THRUSTER_STOP 

void DepthControl_Start(double target) {
    g_target_depth = target;
    
    // [关键修改] 启动时重置时间戳！
    // 这相当于告诉看门狗：“我刚启动，请给我 3秒钟 时间等传感器数据”
    g_last_control_time = time(NULL); 
    
    g_depth_control_enabled = 1;
    printf("[AutoDepth] 定深启动，目标: %.2f，等待传感器数据...\n", target);
}

void DepthControl_Stop(void) {
    g_depth_control_enabled = 0;
    Thruster_Stop();
}

void DepthControl_Loop(double current_depth) {
    g_last_control_time = time(NULL); // 更新时间戳

    if (!g_depth_control_enabled) return;

    double error = g_target_depth - current_depth;
    
    // 打印调试信息，方便上位机监控
    printf("[AutoDepth] Target: %.2f, Curr: %.2f, Err: %.2f\n", g_target_depth, current_depth, error);
    // [新增] 安全保护：如果深度数据异常（例如在空气中或传感器故障），强制停止
    if (current_depth < 0.3) { // 假设有效作业深度至少0.3米
        printf("[AutoDepth] 警告：当前深度过浅 (%.2f)，可能在水面或数据异常，暂停推进！\n", current_depth);
        Thruster_Stop();
        return;
    }

    // 1. 进入死区 (已到达目标附近)
    if (fabs(error) <= DEAD_ZONE) {
        // 如果是正浮力机器人，可能需要 Thruster_Sinking(1) 来维持
        if (BUOYANCY_COMPENSATION_LEVEL == THRUSTER_STOP) {
             Thruster_Stop();
        } else {
             Thruster_Sinking(BUOYANCY_COMPENSATION_LEVEL);
        }
        return;
    }

    // 2. 动作判断
    if (error > 0) { 
        // 目标 > 当前，说明在上方，需要下潜 (Sinking)
        if (error > LEVEL_HIGH_TH) {
            Thruster_Sinking(THRUSTER_LEVEL_5); // 误差大，全速下潜
        } else if (error > LEVEL_LOW_TH) {
            Thruster_Sinking(THRUSTER_LEVEL_3); // 误差中，中速下潜
        } else {
            Thruster_Sinking(THRUSTER_LEVEL_1); // 误差小，慢速下潜
        }
    } else {
        // 目标 < 当前，说明在下方，需要上浮 (Floating)
        double abs_error = fabs(error);
        if (abs_error > LEVEL_HIGH_TH) {
            Thruster_Floating(THRUSTER_LEVEL_5);
        } else if (abs_error > LEVEL_LOW_TH) {
            Thruster_Floating(THRUSTER_LEVEL_3);
        } else {
            Thruster_Floating(THRUSTER_LEVEL_1);
        }
    }
}
// [新增] 安全检查函数，供主循环调用
void DepthControl_SafetyCheck(void) {
    // 1. 如果没开启定深，直接退出，不消耗 CPU，也不检查传感器
    if (!g_depth_control_enabled) return;

    time_t now = time(NULL);
    
    // 2. 检查超时
    // 如果 Start() 之后 3秒内，或者上一次 Loop() 之后 3秒内
    // 都没有新的传感器数据进来，说明传感器没开或挂了。
    if (difftime(now, g_last_control_time) > 3.0) {
        printf("[AutoDepth] 错误：传感器数据超时 (>3s)，可能未打开传感器？强制停止！\n");
        DepthControl_Stop(); // 触发停机保护
    }
}