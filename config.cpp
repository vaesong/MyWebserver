#include"config.h"

Config::Config(){
    //端口号，默认是 9190
    PORT = 9190;

    // //数据库的用户名
    // User = "hrj";

    // //数据库用户的密码
    // PassWord = "MySQL@1234";

    // //数据库的名称
    // DatabaseName = "mysql";

    //日志的写入方式，默认是 0，同步
    LOGWrite = 0;

    //组合触发模式，默认是 listen LT + connfd ET
    TRIGMode = 0;

    //监听端的触发模式，默认是 LT;
    LISTENTrigmode = 0;

    //客户端的触发模式，默认是 ET；
    CONNTrigmode = 1;

    //优雅的关闭连接，默认是不使用
    OPT_LINGER = 0;

    //数据库连接池的数量，默认是 8
    sql_num = 8;

    //线程池的线程数量，默认是 8
    thread_num = 8;

    //关闭日志，默认是不关闭
    close_log = 0;

    //并发模型，默认是 proactor
    actor_model = 0;
}

void Config::parse_arg(int argc, char* argv[]){
    int opt;
    const char* str = "p:l:m:o:s:t:c:a:";
    while((opt = getopt(argc, argv, str)) != -1){
        switch(opt){
            case 'p':{
                PORT = atoi(optarg);
                break;
            }
            case 'l':{
                LOGWrite = atoi(optarg);
                break;
            }
            case 'm':{
                TRIGMode = atoi(optarg);
                break;
            }
            case 'o':{
                OPT_LINGER = atoi(optarg);
                break;
            }
            case 's':{
                sql_num = atoi(optarg);
                break;
            }
            case 't':{
                thread_num = atoi(optarg);
                break;
            }
            case 'c':{
                close_log = atoi(optarg);
                break;
            }
            case 'a':{
                actor_model = atoi(optarg);
                break;
            }
            default:
                break;
        }
    }
}