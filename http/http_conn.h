#ifndef HTTPCONNECTION_H
#define HTTPCONNETCION_H

#include<iostream>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<stdio.h>
#include<cstdlib>
#include<sys/types.h>
#include<sys/stat.h>
#include<unistd.h>
#include<string.h>
#include<string>
#include<signal.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<sys/uio.h>
#include<map>

#include"../lock/locker.h"
#include"../log/log.h"
#include"../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"

using namespace std;


class http_conn{
public:
    //方法的类型
    enum METHOD{
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    
    //主状态机的几种状态
    enum CHECK_STATE{
        CHECK_STATE_REQUESTLINE = 0,    //正在分析请求行
        CHECK_STATE_HEADER,             //正在分析请求头
        CHECK_STATE_CONTENT             //正在分析请求数据
    };

    //HTTP的几种状态码对应的状态
    enum HTTP_CODE{
        NO_REQUEST,             //表示请求不完整
        GET_REQUEST,            //表示读完整个请求
        BAD_REQUEST,            //读取请求出错
        NO_RESOURCE,            //服务端没有该资源
        FORBIDDEN_REQUEST,      //不允许的请求
        FILE_REQUEST,           //文件请求
        INTERNAL_ERROR,         //服务器内部错误
        CLOSED_CONNECTION       //关闭连接
    };

    //从状态机的状态，读取一行之后的状态
    enum LINE_STATUS{
        LINE_OK = 0,            //读到完整的行
        LINE_BAD,               //读取行失败
        LINE_OPEN               //行数据尚不完整
    };


public:
    http_conn(){};
    ~http_conn(){};

    //静态的epoll，可以被所有用户共享
    static int m_epollfd;
    //静态的用户数量
    static int m_user_count;
    //读缓冲区的大小
    static const int READ_BUFFER_SIZE = 3072;
    //写缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 2048;
    //设置读取文件的名称 m_real_file 大小
    static const int FILENAME_LEN = 200;

    MYSQL *mysql;

    //该成员变量，表示在 reactor 模式下，需要读取数据还是需要写入数据
    int m_state; 
    
              
    //处理的函数
    void process();

    //初始化连接函数，当有新的客户进来时，执行的函数
    void init(int sockfd, const sockaddr_in & addr, char *root, int TRIGMode,
              int close_log, string user, string passwd, string sqlname);

    //关闭连接
    void close_conn();

    //读数据
    bool read();

    //写数据
    bool write();

    //根据数据库连接，把数据库的信息 key-value 存放到 map，m_users 中
    void initmysql_result(connection_pool * connPool);

    //获得地址
    sockaddr_in *get_address(){return &m_address;};
    int timer_flag;
    int improv;

private:

    //初始化解析相关的成员变量
    void init();

    //释放mmap创建的内存空间
    void unmap();
    
    //解析 HTTP 请求-------------------------------
    HTTP_CODE process_read();
    //解析请求行
    HTTP_CODE parse_request_line(char * text);
    //解析请求头
    HTTP_CODE parse_headers(char * text);
    //解析请求体
    HTTP_CODE parse_content(char * text);

    //获取一行数据
    char * get_line(){return m_read_buf + m_start_line;}
    //解析一行数据，判断其行的数据是否是完整，可用的
    //m_checked_index 从 m_start_line 开始向后检查，检查到 '\r\n'的时候返回
    //通过 getline() 获取 m_start_line 到 m_checked_index 的一行
    LINE_STATUS parse_line();

    //对请求的资源进行判断，返回响应状态
    HTTP_CODE do_request();

    //生成 HTTP 响应报文--------------------------------
    // 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
    bool process_write(HTTP_CODE ret);
    //向 m_write_buf 中添加响应行
    bool add_status_line(int status, const char *title);
    //向缓冲区中写入响应头，//添加响应报文的头部信息和空行, 这里的长度是文件的长度
    bool add_headers(int content_length);
    //向缓冲区写入内容，响应体？
    bool add_content(const char* content);

    //相应头中添加 content type
    bool add_content_type();
    //相应头中添加 content length
    bool add_content_length(int content_length);
    //相应的编码方式
    bool add_encoding();
    //相应头中添加 是否是长连接
    bool add_linger();
    //相应头中添加 空内容体
    bool add_blank_line();


    //更新m_write_idx指针和缓冲区m_write_buf中的内容,真正的向写缓冲区写入的函数
    bool add_response(const char *format, ...);

private:

    //客户端的 socket
    int m_sockfd;
    //通信的 sock 地址
    struct sockaddr_in m_address;

    //读缓冲区
    char m_read_buf[READ_BUFFER_SIZE];
    //目前已经读到的数据的后一个位置，也就是下次读的起始位置
    int m_read_idx;

    //主状态机当前的状态
    CHECK_STATE m_check_state;
    //读缓冲区中，从状态机已经检查的位置
    int m_checked_index;
    //读缓冲区中正在解析行的起始位置，滞后于 m_checked_idx
    int m_start_line;

    METHOD m_method;                    //请求方法
    char *m_url;                        //解析到的url
    char *m_version;                    //协议版本
    char *m_host;                       //host
    bool m_linger;                      //LINGER，客户端close的时候不会发送FIN，而是会立即或者等待一段时间直接发送RST断开连接，同时清除发送队列里的所有未发送的数据
    int m_content_length;               //内容的长度
    char *m_string;                     //存储请求 content 数据

    char *m_file_address;               //读取服务器上的文件地址
    char m_real_file[FILENAME_LEN];     //存储读取文件的路径名称
    struct stat m_file_stat;            //文件的状态
    // const char *doc_root;
    

    //写缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;

    struct iovec m_iv[2];               //io 向量机制，用于指定某些内存块，然后同时发送，例如 文件的地址 和 写缓冲区的地址
    int m_iv_count;                     //用来确定总共几块内存需要发送

    int cgi;                            //是否启用 POST
    char *doc_root;                     //根路径

    map<string, string> m_users;
    locker m_lock;

    int m_TRIGMode;                     //用来表示监听端和客户端的触发模式
    int m_close_log;                    //表示是否启用日志


    //数据库相关
    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};








#endif