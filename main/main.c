#include "main.h"

/*  任务线程    */
#include "../task/task_thread.h"

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
    
    sleep(60*40);

    while(1);

end:
    return 0;
}

