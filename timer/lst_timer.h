#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"

//首选声明一个定时器类
class util_timer;

//定义用户数据类型
struct client_data{
    //包括客户端的 fd 以及 地址
    int sockfd;
    sockaddr_in address;
    util_timer * timer;
};

//定义定时器类型
class util_timer{
public:
    util_timer():prev(NULL), next(NULL){}

public:
    //到期事件，这里是绝对时间
    time_t expire;

    //回调函数
    void (* cb_func)(client_data *);
    client_data * user_data;
    util_timer * prev;
    util_timer * next;
};


//定义定时器的容器，存放定时器
//功能：添加定时器，时间到了调整定时器，删除定时器，tick函数
class sort_timer_lst{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer* timer);
    void adjust_timer(util_timer* timer);
    void del_timer(util_timer* timer);
    void tick();

private:
    //添加到容器链表中
    void add_timer(util_timer* timer, util_timer* lst_head);

private:
    util_timer* head;
    util_timer* tail;
};


//工具类
class Utils{
public:
    Utils(){};
    ~Utils(){};

    void init(int timeslot);

    //对文件描述符设置非阻塞
    void setnonblocking(int fd);

    //把内核事件表注册读事件，ET 模式，选择开启 EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重现定时以不断触发 SIGALRM 信号
    void timer_handler();

    void show_error(int connfd, const char* info);

public:
    static int *u_pipefd;
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;

};

//定时器回调函数
void cb_func(client_data* user_data);

#endif