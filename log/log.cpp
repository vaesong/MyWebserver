#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

//初始化，设定初始的行数为 0，并且默认为同步方式
Log::Log(){
    m_count = 0;
    m_is_async = false;
}

//析构的时候，关闭打开的文件
Log::~Log(){
    if(m_fp != NULL)
        fclose(m_fp);
}

//初始化日志文件，日志缓冲区大小，最大行数以及最长日志条队列 异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char* file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size){
    //如果设置了 max_queue_size，则设置为异步
    if(max_queue_size >= 1){
        //设置写入方式flag
        m_is_async = true;

        //创建并设置阻塞队列长度
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;

        //flush_log_thread 为回调函数，这里创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    //输出内容的长度
    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);

    //日志的最大行数
    m_split_lines = split_lines;

    //先用指针去取得格式化的事件？然后再解引用？？
    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    //strrchr: 查找字符在指定字符串中从右面开始的第一次出现的位置
    //strchr: 查找字符串中首次出现指定字符以及其后面的字符。
    //从后往前找到第一个/的位置
    const char* p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    //相当于自定义日志名  2022_10_26_ServerLog（ServerLog 即是 file_name）
    //若输入的文件名没有/，则直接将时间+文件名作为日志名
    if(p == NULL){
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else{

        //将/的位置向后移动一个位置，然后复制到logname中
        //p - file_name + 1是文件所在路径文件夹的长度
        //dirname相当于./
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    m_fp = fopen(log_full_name, "a");
    if(m_fp == NULL)
        return false;

    return true;
}


void Log::write_log(int level, const char* format, ...){
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};

    switch(level){
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[erro]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
    }

    //写入一个log，对m_count++, m_split_lines最大行数
    m_mutex.lock();
    m_count++;

    //如果日志不是今天创建的，或者已经达到最大行数了，就重新创建一个新文件
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) //everyday log
    {
        //新创建文件
        char new_log[256] = {0};

        //刷新文件内容，然后关闭
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};

        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
       
        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else
        {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }
 
    m_mutex.unlock();

    va_list valst;
    va_start(valst, format);

    string log_str;
    m_mutex.lock();

    //写入的具体时间内容格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();

    if (m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    else
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);
}


void Log::flush(void){
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}