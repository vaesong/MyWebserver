#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include"sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool(){
    m_CurConn = 0;
    m_FreeConn = 0;
}

connection_pool::~connection_pool(){
    DestroyPool();
}

//获得一个静态的数据库池
connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

//初始化数据库池，创建指定的 空闲 数据库连接，用于等待使用
void connection_pool::init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log){
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DataBaseName;
    m_close_log = close_log;

    //创建 MaxConn 个数据库连接？
    for(int i = 0; i < MaxConn; ++i){
        MYSQL* con = NULL;
        con = mysql_init(con);

        if(con == NULL){
            LOG_ERROR("MySQL Error");
            exit(1);
        }

        //创建数据库连接
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DataBaseName.c_str(), Port, NULL, 0);

        if(con == NULL){
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        connlist.push_back(con);
        ++m_FreeConn;
    }

    //当前空闲的连接数的信号量
    reserve = sem(m_FreeConn);
    //此时最大连接数就是空闲连接数
    m_MaxConn = m_FreeConn;
}

//当有请求的时候，从数据库连接池中返回一个空闲的连接，更新使用和空闲连接数
MYSQL* connection_pool::GetConnection(){
    MYSQL *con = NULL;

    //如果当前没有可用的连接
    if(connlist.size() == 0)
        return NULL;

    //等待获取信号量，然后上锁
    reserve.wait();
    lock.lock();

    //取出头结点的 连接
    con = connlist.front();
    connlist.pop_back();

    //更新已用的连接数和可用的连接数
    --m_FreeConn;
    ++m_CurConn;

    lock.unlock();
    return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL* con){
    //如果当前的连接是 NULL
    if(con == NULL)
        return false;

    lock.lock();
    
    connlist.push_back(con);
    ++m_FreeConn;
    --m_CurConn;

    lock.unlock();

    reserve.post();
    return true;
}

//销毁所有的数据库连接
void connection_pool::DestroyPool(){
    lock.lock();

    if(connlist.size() > 0){
        list<MYSQL*>::iterator it;
        for(it = connlist.begin(); it != connlist.end(); ++it){
            MYSQL* con = *it;
            mysql_close(con);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connlist.clear();
    }

    lock.unlock();
}

//获得当前空闲的数据库连接数
int connection_pool::GetFreeConn(){
    return this->m_FreeConn;
}

//把数据库连接的指针 的地址传进来，以及 数据库池的地址
connectionRAII::connectionRAII(MYSQL** SQL, connection_pool* connPool){
    *SQL = connPool->GetConnection();

    conRAII = *SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
    poolRAII->ReleaseConnection(conRAII);
}
