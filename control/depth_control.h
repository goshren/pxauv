#ifndef __DEPTH_CONTROL_H__
#define __DEPTH_CONTROL_H__

// 全局开关和目标值
extern volatile int g_depth_control_enabled;
extern volatile double g_target_depth;

// API
void DepthControl_Init(void);
void DepthControl_Start(double target);
void DepthControl_Stop(void);
void DepthControl_Loop(double current_depth); // 核心算法，被CTD线程调用
void DepthControl_SafetyCheck(void);

#endif