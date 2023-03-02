#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include"./threadpool/threadpool.h"
#include"./http/http_conn.h"

using namespace std;

#define MAX_FD 65535                //定义连接的最大数量，最多也就是文件描述符全满
#define MAX_EVENT_NUMBER 10000      //定义最大的事件数
const int TIMESLOT = 5;             //最小超时单位

class WebServer{
public:
    WebServer();                    //webserver构造函数
    ~WebServer();                   //析构函数

    void init(int port , string user, string passWord, string databaseName,
              int log_write , int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);

    void thread_pool();            //初始化线程池
    void sql_pool();               //初始化数据库
    void log_write();              //初始化日志文件
    void trig_mode();              //触发模式，根据传入的 trigmode 设置监听端 以及 客户端的事件触发模式
    void eventListen();            //监听事件
    void eventLoop();              //循环监听事件，根据文件描述符及事件类型进行处理
    void timer(int connfd, struct sockaddr_in client_address);  //把用户加到数组中，并且处理定时器, 是客户端来连接的初始化函数
    void adjust_timer(util_timer *timer);                       //调整定时器的时间，每次收到或发送数据都更改时间
    void deal_timer(util_timer *timer, int sockfd);             //处理定时器
    bool dealclinetdata();                                      //处理新来的客户端
    bool dealwithsignal(bool& timeout, bool& stop_server);      //处理信号的数据
    void dealwithread(int sockfd);                              //处理读事件，以及调整定时器等 
    void dealwithwrite(int sockfd);                             //处理写事件，以及调整定时器等


private:
    //基础的成员变量
    int m_port;                     //对外连接的端口
    char* m_root;                   //资源的根目录
    int m_log_write;                //设置是同步写入还是异步写入
    int m_close_log;                //是否关闭日志
    int m_actormodel;               //使用的 actor 模型

    int m_pipefd[2];                //定义和信号通信的管道
    int m_epollfd;                  //系统内核的事件表
    http_conn *users;               //存放客户端用户的数组

    //数据库相关
    connection_pool *m_connPool;
    string m_user;                  //登陆数据库用户名
    string m_password;              //登陆数据库密码
    string m_databaseName;          //使用数据库名
    int m_sql_num;

    //线程池相关
    threadpool<http_conn> *m_pool;  //线程池
    int m_thread_num;               //准备创建的线程数量

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];   //内核的事件表

    //其他
    int m_listenfd;                 //监听的文件描述符  
    int m_OPT_LINGER;               //是否优雅的关闭连接    
    int m_TRIGMode;                 //触发模式，监听端与客户端的 4 种方式
    int m_LISTENTrigmode;           //监听端的模式？？？？
    int m_CONNTrigmode;             //客户端的触发模式????

    //定时器相关
    client_data *users_timer;       //存放带有定时器的用户数据，可以直接定时清除
    Utils utils;                    //定义工具类
};
#endif