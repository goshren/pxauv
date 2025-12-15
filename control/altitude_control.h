#ifndef __ALTITUDE_CONTROL_H__
#define __ALTITUDE_CONTROL_H__

// 全局开关和目标值
extern volatile int g_altitude_control_enabled;
extern volatile double g_target_altitude;

// API
void AltitudeControl_Init(void);
void AltitudeControl_Start(double target);
void AltitudeControl_Stop(void);
// 核心算法，被DVL线程调用
void AltitudeControl_Loop(float current_altitude); 

void AltitudeControl_SafetyCheck(void); 

#endif