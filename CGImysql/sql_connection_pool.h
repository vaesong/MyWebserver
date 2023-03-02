/*
提前创建 数据库池
*/
#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include<stdio.h>
#include<list>
#include<errno.h>
#include<string.h>
#include<iostream>
#include<string>
#include<mysql/mysql.h>
#include"../lock/locker.h"
#include"../log/log.h"
using namespace std;


class connection_pool{
public:
    //单例模式
    static connection_pool * GetInstance();

    //初始数据库池,创建多个数据库连接留着备用
    void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log);

    //获取数据库连接
    MYSQL* GetConnection();

    //释放数据库连接
    bool ReleaseConnection(MYSQL* conn);

    //获取空闲的数据库连接
    int GetFreeConn();

    //销毁所有连接
    void DestroyPool();


private:

    connection_pool();
    ~connection_pool();

    int m_MaxConn;      //最大连接数
    int m_CurConn;      //当前已经使用的连接数
    int m_FreeConn;     //当前空闲的连接数,已经连接成功，但是还未使用

    locker lock;
    list<MYSQL *> connlist; //连接池
    sem reserve;            //空闲的连接数的信号量

public:
    string m_url;       //主机地址
    string m_Port;      //数据库的端口号
    string m_User;      //登录数据库的用户名
    string m_PassWord;  //登录数据库密码
    string m_DatabaseName;  //使用的数据库名
    int m_close_log;    //日志的开关
};



class connectionRAII{
public:
    connectionRAII(MYSQL** con, connection_pool* connPool);
    ~connectionRAII();

private:
    MYSQL* conRAII;
    connection_pool* poolRAII;
};

#endif