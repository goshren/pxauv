#ifndef __NAVIGATION_CONTROL_H__
#define __NAVIGATION_CONTROL_H__

// 导航控制相关的全局变量
extern volatile int g_nav_control_enabled;

// API 接口
void Nav_Init(void);
void Nav_SetTarget(double lat, double lon);       // 设置目标点 (由USBL调用)
void Nav_UpdateCurrentPos(double lat, double lon);// 更新当前位置 (由USBL调用)
void Nav_Stop(void);                              // 停止导航

// 核心控制循环，挂载在 DVL 线程中执行 (1Hz)
void Nav_Loop(float current_heading);

#endif