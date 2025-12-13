#include "main.h"

/*  任务线程    */
#include "../task/task_thread.h"
/* 引入主控舱驱动头文件，以便调用上电函数 */
#include "../drivers/maincabin/MainCabin.h"
#include "../task/task_mission.h" // 引入新头文件

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

    /* [新增] 7. 预编程自主任务初始化 */
    if(Task_Mission_Init() < 0)
    {
        goto end;
    }
    printf("任务模块初始化完毕......\n");


    while(1) {
        sleep(10); // 防止占用 CPU
    }

end:
    printf("程序异常退出!\n");
    return 0;
}

