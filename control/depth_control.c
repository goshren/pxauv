#include "depth_control.h"
#include "../drivers/thruster/Thruster.h"
#include <stdio.h>
#include <math.h>

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
    g_depth_control_enabled = 1;
}

void DepthControl_Stop(void) {
    g_depth_control_enabled = 0;
    Thruster_Stop();
}

void DepthControl_Loop(double current_depth) {
    if (!g_depth_control_enabled) return;

    double error = g_target_depth - current_depth;
    
    // 打印调试信息，方便上位机监控
    printf("[AutoDepth] Target: %.2f, Curr: %.2f, Err: %.2f\n", g_target_depth, current_depth, error);
    // [新增] 安全保护：如果深度数据异常（例如在空气中或传感器故障），强制停止
    if (current_depth < 0.1) { // 假设有效作业深度至少0.5米
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