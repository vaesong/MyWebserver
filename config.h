#ifndef CONFIG_H
#define CONFIG_H

#include"webserver.h"

using namespace std;

class Config{
public:
    Config();
    ~Config(){};

    void parse_arg(int argc, char* argv[]);

    //端口号
    int PORT;

    // //数据库的用户名
    // string User;
    // //数据库的密码
    // string PassWord;
    // //数据库的名称
    // string DatabaseName;

    //日志的写入方式
    int LOGWrite;

    //组合的事件触发模式
    int TRIGMode;

    //监听端的事件触发模式
    int LISTENTrigmode;

    //客户端的事件触发模式
    int CONNTrigmode;

    //是否优雅的关闭连接
    int OPT_LINGER;

    //数据库池中的连接数量
    int sql_num;

    //线程池内的线程数
    int thread_num;

    //是否关闭日志
    int close_log;

    //并发模型的选择
    int actor_model;
};








#endif