#include "navigation_control.h"
#include "../drivers/thruster/Thruster.h"
#include <stdio.h>
#include <math.h>
#include <time.h>

/* 常量定义 */
#define PI 3.14159265358979323846
#define DEG2RAD(x) ((x) * PI / 180.0)
#define RAD2DEG(x) ((x) * 180.0 / PI)
#define EARTH_RADIUS 6378137.0 // 地球半径 (米)

/* 调参宏 */
#define NAV_ALIGN_THRESHOLD  15.0  // 对准阈值：航向误差 < 15度 允许直行
#define NAV_RE_ALIGN_TRIGGER 30.0  // 重新对准阈值：航向误差 > 30度 切换回原地旋转
#define NAV_ARRIVAL_DIST     3.0   // 到达阈值：距离 < 3米 视为到达
#define NAV_DATA_TIMEOUT     5.0   // 数据超时时间 (秒)

/* 状态变量 */
volatile int g_nav_control_enabled = 0;
static double g_target_lat = 0.0, g_target_lon = 0.0;
static double g_curr_lat = 0.0,   g_curr_lon = 0.0;
static time_t g_last_pos_time = 0; // 上次收到定位数据的时间

/* 状态机枚举 */
typedef enum {
    NAV_STATE_IDLE = 0,
    NAV_STATE_ALIGNING, // 原地旋转对准
    NAV_STATE_CRUISING, // 直行巡航
    NAV_STATE_ARRIVED   // 到达
} NavState_t;

static NavState_t g_nav_state = NAV_STATE_IDLE;

/* 初始化 */
void Nav_Init(void) {
    g_nav_control_enabled = 0;
    g_nav_state = NAV_STATE_IDLE;
}

/* 设置目标点 (收到 +...+) */
void Nav_SetTarget(double lat, double lon) {
    g_target_lat = lat;
    g_target_lon = lon;
    g_nav_control_enabled = 1; // 收到目标自动开启导航
    g_nav_state = NAV_STATE_ALIGNING; // 重置为对准状态
    printf("[AutoNav] 收到新目标: Lat=%.6f, Lon=%.6f -> 导航启动！\n", lat, lon);
}

/* 更新当前位置 (收到 /.../) */
void Nav_UpdateCurrentPos(double lat, double lon) {
    g_curr_lat = lat;
    g_curr_lon = lon;
    g_last_pos_time = time(NULL); // 喂狗
    // printf("[AutoNav] 定位更新: Lat=%.6f, Lon=%.6f\n", lat, lon);
}

/* 停止导航 */
void Nav_Stop(void) {
    if (g_nav_control_enabled) {
        g_nav_control_enabled = 0;
        g_nav_state = NAV_STATE_IDLE;
        Thruster_Stop();
        printf("[AutoNav] 导航已停止。\n");
    }
}

/* 辅助：计算两点距离 (单位:米) */
static double Calc_Distance(double lat1, double lon1, double lat2, double lon2) {
    double x = (lon2 - lon1) * DEG2RAD(1.0) * EARTH_RADIUS * cos(DEG2RAD((lat1 + lat2) / 2.0));
    double y = (lat2 - lat1) * DEG2RAD(1.0) * EARTH_RADIUS;
    return sqrt(x * x + y * y);
}

/* 辅助：计算目标方位角 (0-360度, 北=0) */
static double Calc_Bearing(double lat1, double lon1, double lat2, double lon2) {
    double y = sin(DEG2RAD(lon2 - lon1)) * cos(DEG2RAD(lat2));
    double x = cos(DEG2RAD(lat1)) * sin(DEG2RAD(lat2)) -
               sin(DEG2RAD(lat1)) * cos(DEG2RAD(lat2)) * cos(DEG2RAD(lon2 - lon1));
    double bearing = atan2(y, x);
    return fmod((RAD2DEG(bearing) + 360.0), 360.0);
}

/* 核心循环 (1Hz) */
void Nav_Loop(float current_heading) {
    if (!g_nav_control_enabled) return;

    // 1. 安全看门狗检查
    time_t now = time(NULL);
    if (difftime(now, g_last_pos_time) > NAV_DATA_TIMEOUT) {
        printf("[AutoNav] 错误：定位数据超时 (>5s)，强制停车！\n");
        Nav_Stop();
        return;
    }

    // 2. 计算导航参数
    double dist = Calc_Distance(g_curr_lat, g_curr_lon, g_target_lat, g_target_lon);
    double target_heading = Calc_Bearing(g_curr_lat, g_curr_lon, g_target_lat, g_target_lon);
    
    // 计算航向误差 (-180 ~ +180)
    double head_err = target_heading - current_heading;
    if (head_err > 180.0)  head_err -= 360.0;
    if (head_err < -180.0) head_err += 360.0;

    printf("[AutoNav] Dist: %.1fm | Bear: %.1f | CurrHead: %.1f | Err: %.1f | State: %d\n", 
           dist, target_heading, current_heading, head_err, g_nav_state);

    // 3. 状态机逻辑
    switch (g_nav_state) {
        case NAV_STATE_ALIGNING:
            // 阶段A：对准
            if (fabs(head_err) < NAV_ALIGN_THRESHOLD) {
                g_nav_state = NAV_STATE_CRUISING;
            } else {
                // 原地旋转 (使用2档，避免太快转过头)
                if (head_err > 0) Thruster_TurnRight(THRUSTER_LEVEL_2);
                else              Thruster_TurnLeft(THRUSTER_LEVEL_2);
            }
            break;

        case NAV_STATE_CRUISING:
            // 阶段B：巡航
            if (dist < NAV_ARRIVAL_DIST) {
                g_nav_state = NAV_STATE_ARRIVED;
            } 
            else if (fabs(head_err) > NAV_RE_ALIGN_TRIGGER) {
                // 偏航太大，切回旋转模式
                g_nav_state = NAV_STATE_ALIGNING; 
                Thruster_Stop(); 
            } 
            else {
                // 航向基本正确，全速前进 (使用4档)
                // 注意：这里无法同时做“前进+转向”，只能纯前进
                // 靠上面的 RE_ALIGN_TRIGGER 来纠偏
                Thruster_Forward(THRUSTER_LEVEL_4);
            }
            break;

        case NAV_STATE_ARRIVED:
            // 阶段C：到达
            Thruster_Stop();
            printf("[AutoNav] 到达目标点！停止推进。\n");
            g_nav_control_enabled = 0; // 任务结束
            break;
            
        default:
            Thruster_Stop();
            break;
    }
}