#include "altitude_control.h"
#include "../drivers/thruster/Thruster.h"
#include <stdio.h>
#include <math.h>

volatile int g_altitude_control_enabled = 0;
volatile double g_target_altitude = 0.0;

// 参数调优 (定高时，离底太近比较危险，建议死区和阈值根据实际地形调整)
#define ALT_DEAD_ZONE       0.50   // 死区：±20cm
#define ALT_LEVEL_LOW_TH    1.00   // 1档微调阈值：50cm
#define ALT_LEVEL_HIGH_TH   2.00   // 高速阈值：1.5m

// 浮力配平 (通常AUV是正浮力，停止时会自动上浮，所以维持高度可能需要微弱下潜)
// 如果你的机器人在死区内总是飘上去，这里请改为 THRUSTER_LEVEL_1
#define ALT_BUOYANCY_COMPENSATION  THRUSTER_STOP 

void AltitudeControl_Start(double target) {
    g_target_altitude = target;
    g_altitude_control_enabled = 1;
    printf("[AutoAlt] 定高模式启动，目标离底高度: %.2f 米\n", target);
}

void AltitudeControl_Stop(void) {
    if(g_altitude_control_enabled) {
        g_altitude_control_enabled = 0;
        Thruster_Stop();
        printf("[AutoAlt] 定高模式已停止\n");
    }
}

void AltitudeControl_Loop(float current_altitude) {
    if (!g_altitude_control_enabled) return;

    // [安全保护] DVL 丢失底锁时通常返回 0 或 -1，或者极小值
    // 如果离底太近（<0.3m）或者数据无效，必须强制上浮或停机，防止撞底
    if (current_altitude < 0.3) { 
        printf("[AutoAlt] 警告：离底过近或数据无效 (%.2f)，强制停机/上浮！\n", current_altitude);
        // 这里策略很关键：是停机还是紧急上浮？
        // 建议先停机，避免推进器卷入泥沙；或者用1档轻轻上浮
        Thruster_Stop(); 
        // Thruster_Floating(THRUSTER_LEVEL_1); // 可选：紧急脱困
        return;
    }

    double error = g_target_altitude - current_altitude;
    
    printf("[AutoAlt] Target: %.2f, Curr: %.2f, Err: %.2f\n", g_target_altitude, current_altitude, error);

    // 1. 死区判断
    if (fabs(error) <= ALT_DEAD_ZONE) {
        if (ALT_BUOYANCY_COMPENSATION == THRUSTER_STOP) {
             Thruster_Stop();
        } else {
             Thruster_Sinking(ALT_BUOYANCY_COMPENSATION); // 对抗正浮力
        }
        return;
    }

    // 2. 动作判断 (注意方向与定深相反！)
    if (error > 0) { 
        // 目标 > 当前 (例如目标5m，当前2m)，太低了 -> 需要上浮 (Float)
        if (error > ALT_LEVEL_HIGH_TH) {
            Thruster_Floating(THRUSTER_LEVEL_5);
        } else if (error > ALT_LEVEL_LOW_TH) {
            Thruster_Floating(THRUSTER_LEVEL_3);
        } else {
            Thruster_Floating(THRUSTER_LEVEL_1);
        }
    } else {
        // 目标 < 当前 (例如目标2m，当前5m)，太高了 -> 需要下潜 (Sink)
        double abs_error = fabs(error);
        if (abs_error > ALT_LEVEL_HIGH_TH) {
            Thruster_Sinking(THRUSTER_LEVEL_5);
        } else if (abs_error > ALT_LEVEL_LOW_TH) {
            Thruster_Sinking(THRUSTER_LEVEL_3);
        } else {
            Thruster_Sinking(THRUSTER_LEVEL_1);
        }
    }
}