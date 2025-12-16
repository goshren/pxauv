#include "main.h"

/*  任务线程    */
#include "../task/task_thread.h"
/* 引入主控舱驱动头文件，以便调用上电函数 */
#include "../drivers/maincabin/MainCabin.h"
// 引入头文件
#include "../control/depth_control.h"
#include "../control/altitude_control.h"
#include "../task/task_mission.h"
#include "../control/navigation_control.h"

int main(int argc, const char *argv[])
{
    printf("程序正在运行......\n");

    /*  1.数据库  */
    if(Task_Database_Init() < 0)
    {
        goto end;
    }
    printf("数据库初始化完毕......\n");

    /*  2.Epoll管理器相关任务初始化   */
    if(Task_Epoll_Init() < 0)
    {
        goto end;
    }
    printf("Epoll管理器初始化完毕......\n");

    /*  3.主控舱    */
    if(Task_MainCabin_Init() < 0)
    {
        goto end;
    }
    printf("主控舱初始化完毕.......\n");
    

    /*  4.推进器    */
    if(Task_Thruster_Init() < 0)                     
    {
        goto end;
    }            
    printf("推进器初始化完毕.......\n");


    /*  5.上位机连接 -- 服务器*/
    if(Task_ConnectHost_Init() < 0)
    {
        goto end;
    }
    printf("上位机连接 -- 服务器 -- 初始化完毕.......\n");
    
    /*  6.GPS   */
    if(Task_GPS_Init() < 0)
    {
        goto end;
    }
    printf("GPS初始化完毕.......\n"); 
    // ==========================================
    // [重要] 7. 预编程自主任务初始化
    // 如果不加这一步，USBL 收到 'M' 指令后，g_mission_running 会变 1，
    // 但因为没有线程去轮询检查这个变量，电机将没有任何反应！
    // ==========================================
    if(Task_Mission_Init() < 0)
    {
        goto end;
    }
    printf("任务模块初始化完毕......\n");

    // [新增] 导航模块初始化
    Nav_Init();
    printf("任务模块初始化完毕......\n");


    while(1) {
        // 每 1秒 检查一次系统安全
        DepthControl_SafetyCheck();
        AltitudeControl_SafetyCheck();
        sleep(1); // 防止占用 CPU
    }

end:
    printf("程序异常退出!\n");
    return 0;
}

