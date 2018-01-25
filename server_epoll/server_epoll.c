/*************************************************************************
> File Name: Server.c
> Author: SongLee
************************************************************************/
#include "cpu.c"
#include <netinet/in.h> // sockaddr_in
#include <sys/types.h> // socket
#include <sys/socket.h> // socket
#include <stdio.h> // printf
#include <stdlib.h> // exit
#include <string.h> // bzero
#include <pthread.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/epoll.h>

//modified by liudong16
//to keep in correspondence with mtcp
#define SERVER_PORT 8000
#define LENGTH_OF_LISTEN_QUEUE 4096
#define BUFFER_SIZE (8*1024)
#define FILE_NAME_MAX_SIZE 512
#define MAX_CPUS 12
#define MAX_FLOW_NUM 1000
#define MAX_EVENTS (MAX_FLOW_NUM * 3)

pthread_t app_thread[MAX_CPUS];
int cores[MAX_CPUS];
int core_limit;
int i;
char buffer[BUFFER_SIZE];
char file_name[FILE_NAME_MAX_SIZE+1];
int done[MAX_CPUS];

void setsock_nonblock(int sock)
{
    int opts;
    opts = fcntl(sock, F_GETFL);
    if (opts < 0)
    {
        perror("fcntl(sock, GETFL)");
        exit(1);
    }
    opts = opts|O_NONBLOCK;
    if (fcntl(sock, F_SETFL, opts) < 0)
    {
        perror("fcntl(sock, SETFL, opts)");
        exit(1);
    }
}

int CreateListeningSocket(int core, int ep)
{
    struct epoll_event ev;

    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htons(INADDR_ANY);
    server_addr.sin_port = htons(SERVER_PORT);
    //server_addr.sin_port = htons(server_port);

    // 创建socket，若成功，返回socket描述符
    int server_socket_fd = socket(PF_INET, SOCK_STREAM, 0);
    printf("[CPU %d]server_socket_fd is %d.\n", core, server_socket_fd);
    if(server_socket_fd < 0)
    {
        perror("Create Socket Failed:");
        printf("Create Socket Failed.\n");
        exit(1);
    }
    // 设置套接字选项避免地址使用错误
    int opt = 1;
    setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    /*
    if (setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Set socket option failed:");
        printf("Set socket option failed:");
        exit(1);
    };
    */

    // 绑定socket和socket地址结构
    if(-1 == (bind(server_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr))))
    {
        perror("Server Bind Failed:");
        printf("[CPU %d]Server Bind Failed.\n", core);
        exit(1);
    }

    // socket监听
    if(-1 == (listen(server_socket_fd, LENGTH_OF_LISTEN_QUEUE)))
    {
        perror("Server Listen Failed:");
        printf("[CPU %d]Server Listen Failed.\n", core);
        exit(1);
    }

    ev.events = EPOLLIN;
    ev.data.fd = server_socket_fd;
    epoll_ctl(ep, EPOLL_CTL_ADD, server_socket_fd, &ev);

    return server_socket_fd;
}

int AcceptConnection(int core, int server_socket_fd, int ep)
{
    //static int con_num = 0;
    struct epoll_event ev;
    // 定义客户端的socket地址结构
    struct sockaddr_in client_addr;
    socklen_t client_addr_length = sizeof(client_addr);

    // 接受连接请求，返回一个新的socket(描述符)，这个新socket用于同连接的客户端通信
    // accept函数会把连接到的客户端信息写到client_addr中
    int new_server_socket_fd = accept(server_socket_fd, (struct sockaddr*)&client_addr, &client_addr_length);
    //con_num++;
    //printf("[CPU %d]accept connection number is %d.\n", core, con_num);
    printf("[CPU %d]accept socket id is %d.\n", core, new_server_socket_fd);

    if (new_server_socket_fd >= 0)
    {
        //这一语句看似简单 其实决定了建立连接之后传送数据的开始 只有当连接数达到一定数量之后才开始传送数据
        if (new_server_socket_fd >= MAX_FLOW_NUM)
        {
            printf("Invalid socket id %d.\n", new_server_socket_fd);
            return -1;
        }

        //update epoll state;
        ev.events = EPOLLIN;
        ev.data.fd = new_server_socket_fd;
        //setsock_nonblock(new_server_socket_fd);
        epoll_ctl(ep, EPOLL_CTL_ADD, new_server_socket_fd, &ev);
        //printf("epoll state updates.\n");
    }
    else
    {
        perror("accept error!\n");
    }

    return new_server_socket_fd;
}

void HandleReadEvent(int new_server_socket_fd, int ep)
{
    struct epoll_event ev;

    //recv函数接受数据到buffer中;
    //char buffer[BUFFER_SIZE];
    bzero(buffer, BUFFER_SIZE);
    if (recv(new_server_socket_fd, buffer, BUFFER_SIZE, 0) < 0)
    {
        perror("Server Receive Data Failed.\n");
    }

    // 然后从buffer(缓冲区)拷贝到file_name中
    //char file_name[FILE_NAME_MAX_SIZE+1];
    bzero(file_name, FILE_NAME_MAX_SIZE+1);
    strncpy(file_name, buffer, strlen(buffer)>FILE_NAME_MAX_SIZE?FILE_NAME_MAX_SIZE:strlen(buffer));
    printf("%s\n", file_name);

    //epoll states update;
    ev.events = EPOLLOUT;
    ev.data.fd = new_server_socket_fd;
    epoll_ctl(ep, EPOLL_CTL_MOD, new_server_socket_fd, &ev);
}

void HandleWriteEvent(int new_server_socket_fd, char buffer[])
{
    // 打开文件并读取文件数据
    FILE *fp = fopen(file_name, "r");
    if(NULL == fp)
    {
        printf("File:%s Not Found\n", file_name);
    }
    else
    {
        printf("File name is %s.\n", file_name);
        bzero(buffer, BUFFER_SIZE);
        int length = 0;
        // 每读取一段数据，便将其发送给客户端，循环直到文件读完为止
        while((length = fread(buffer, sizeof(char), BUFFER_SIZE, fp)) > 0)
        {
            if(send(new_server_socket_fd, buffer, length, 0) < 0)
            {
            printf("Send File:%s Failed./n", file_name);
            break;
            }
            bzero(buffer, BUFFER_SIZE);
        }

    // 关闭文件
        fclose(fp);
        printf("File:%s Transfer Successful!\n", file_name);
    }
    //关闭与客户端的连接
    close(new_server_socket_fd);
}

void CloseConnection(int ep, int sockid)
{
    epoll_ctl(ep, EPOLL_CTL_DEL, sockid, NULL);
    close(sockid);
}

void *server_thread(void *arg)
{
    int core = *(int *)arg;
    printf("This is CPU core %d.\n", core);
    //int turn = 0;
    core_affinitize(core);
    int turn = 0;

    //epoll arguments;
    int ep;
    struct epoll_event *events;
    int nevents;
    int i, ret;
    int do_accept;

    ep = epoll_create(MAX_EVENTS);
    if (ep < 0)
    {
        printf("Failed to create epoll struct!.\n");
        exit(1);
    }
    events = (struct epoll_event *)calloc(MAX_EVENTS, sizeof(struct epoll_event));
    if (!events)
    {
        perror("Failed to allocate events!.\n");
        exit(1);
    }

    int server_socket_fd = CreateListeningSocket(core, ep);
    if (server_socket_fd < 0)
    {
        perror("Failed to create listening socket.\n");
        exit(1);
    }

    while (!done[core])
    {
        turn++;
        printf("[CPU %d]This is turn %d.\n", turn);

        nevents = epoll_wait(ep, events, MAX_EVENTS, -1);
        if (nevents < 0)
        {
            if (errno != EINTR)
                perror("epoll_wait");
            break;
        }
        printf("[CPU %d]Number of events is %d.\n", core, nevents);

        do_accept = 0;
        for (i = 0; i < nevents; i++)
        {
            if (events[i].data.fd == server_socket_fd)
            {
                do_accept = 1;
            }
            else if (events[i].events & EPOLLERR)
            {
                //errors++;
                printf("events error!\n");
                CloseConnection(ep, events[i].data.fd);
            }
            else if (events[i].events & EPOLLIN)
            {
                printf("Handle Read Events.\n");
                HandleReadEvent(events[i].data.fd, ep);
            }
            else if (events[i].events & EPOLLOUT)
            {
                printf("Handle Write Events.\n");
                HandleWriteEvent(events[i].data.fd, buffer);
            }
            else
            {
                assert(0);
            }
        }

        if (do_accept)
        {
            while (1)
            {
                ret = AcceptConnection(core, server_socket_fd, ep);
                if (ret < 0)
                {
                    printf("Accept failed.\n");
                    break;
                }
            }
        }

        //printf("[CPU %d]This is turn %d.\n", turn);
    }
    // 关闭监听用的socket
    CloseConnection(ep, server_socket_fd);
    //printf("[CPU %d]accept connection number is %d.\n", core, con_num);
}

int main(void)
{
    core_limit = 4;

    for (i = 0; i < core_limit; i++)
    {
        cores[i] = i;
        done[i] = 0;
        if (pthread_create(&app_thread[i], NULL, server_thread, (void *)&cores[i]))
        {
            perror("pthread_create failed.\n");
            exit(-1);
        }
    }

    for (i = 0; i < core_limit; i++)
    {
        pthread_join(app_thread[i], NULL);
        printf("thread %d joined.\n", i);
    }
    return 0;
}
