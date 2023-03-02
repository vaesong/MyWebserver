#include"config.h"

int main(int argc, char *argv[]){
    //登录的数据库名称，用户名以及密码
    string user = "root";
    string passwd = "MySQL@123";
    string databasename = "mydb";

    //命令行解析
    Config config;
    config.parse_arg(argc, argv);

    WebServer webserver;

    //初始化webserver
    webserver.init(config.PORT, user, passwd, databasename, config.LOGWrite,
                   config.OPT_LINGER, config.TRIGMode, config.sql_num,
                   config.thread_num, config.close_log, config.actor_model);

    //启动日志
    webserver.log_write();

    //数据库
    webserver.sql_pool();

    //线程池
    webserver.thread_pool();

    //触发模式
    webserver.trig_mode();

    //初始化很多东西
    webserver.eventListen();

    //循环监听事件
    webserver.eventLoop();

    return 0;
}