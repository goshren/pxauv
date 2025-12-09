#include "tcp.h"
#include <pthread.h>

#define TEST_PORT 8888
#define TEST_IP "127.0.0.1"
#define TEST_MSG "Hello, TCP Network!"

/******************** 测试工具函数 ********************/
void print_hex(const char* prefix, const unsigned char* data, int len) {
    printf("%s[%d]: ", prefix, len);
    for (int i = 0; i < len; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

/******************** 服务器线程 ********************/
void* server_thread(void* arg)
{
    int server_fd = TCP_InitServer(TEST_IP, TEST_PORT);
    if(server_fd < 0)
    {
        fprintf(stderr, "Server init failed\n");
        return NULL;
    }

    printf("Server waiting for connection...\n");
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd < 0) {
        perror("accept failed");
        close(server_fd);
        return NULL;
    }

    // 接收测试
    unsigned char recv_buf[32] = {0};
    int recv_len = 0;
    while(1)
    {
        recv_len = TCP_RecvData(client_fd, recv_buf, sizeof(recv_buf), 32, 200);
        if (recv_len > 0)
        {
            printf("read:%s\n",recv_buf);           
            // 回显测试
            TCP_SendData(client_fd, recv_buf, recv_len);
            memset(recv_buf, 0, sizeof(recv_buf));
        }
    }
    
    close(client_fd);
    close(server_fd);
    return NULL;
}

/******************** 客户端测试 ********************/
void test_client() {
    printf("\n=== Client Test ===\n");
    
    int client_fd = TCP_InitClient(TEST_IP, TEST_PORT);
    if (client_fd < 0) {
        fprintf(stderr, "Client connect failed\n");
        return;
    }

    // 发送测试
    const char* test_data = TEST_MSG;
    int send_len = strlen(test_data) + 1; // 包含结束符
    int ret = TCP_SendData(client_fd, (unsigned char*)test_data, send_len);
    printf("Client sent %d/%d bytes\n", ret, send_len);

    // 接收回显
    unsigned char echo_buf[TCP_MAX_RECV_SIZE] = {0};
    int echo_len = TCP_RecvData(client_fd, echo_buf, sizeof(echo_buf), 32, 200);
    if (echo_len > 0) {
        printf("Echo: %s\n", echo_buf);
    }

    close(client_fd);
}

/******************** 边界测试 ********************/
void test_edge_cases() {
    printf("\n=== Edge Case Test ===\n");
    
    // 无效文件描述符
    TCP_SendData(-1, (unsigned char*)"test", 4);
    
    // NULL缓冲区
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    TCP_SendData(fd, NULL, 10);
    close(fd);
    
    // 超长数据
    unsigned char large_data[TCP_MAX_SEND_SIZE + 10];
    memset(large_data, 0xAA, sizeof(large_data));
    TCP_SendData(fd, large_data, sizeof(large_data));
}

int main() {
    // 启动服务器线程
    pthread_t tid;
    pthread_create(&tid, NULL, server_thread, NULL);
    sleep(1); // 确保服务器先启动

    // 运行测试用例
    while(1)
    //test_client();
    //test_edge_cases();

    // 清理
    pthread_join(tid, NULL);
    return 0;
}