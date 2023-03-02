#ifndef THREADPOOL_H
#define THREADPOOL_H


#include<pthread.h>
#include<list>
#include"../lock/locker.h"
#include"../CGImysql/sql_connection_pool.h"
#include"../log/log.h"
#include<exception>
#include<iostream>

//定义为模板类，方便后续的代码复用
template<class T>
class threadpool{

public:
    //初始化线程的数量和最大的请求数量
    threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests);

    //析构函数
    ~threadpool();

    //proactor 模式 向线程池中的任务队列添加任务
    bool append_p(T* request);

    //reactor 模式 向线程池中的任务队列添加任务
    bool append(T* reauest, int state);

private:
    static void * worker(void * arg);
    void run();

private:
    //线程池里面线程的数量
    int m_thread_number;

    //线程池队列, 采用指针动态维护
    pthread_t * m_threads;

    //请求队列最大的请求数量
    int m_max_requests;

    //请求队列
    std::list<T*> m_workqueue;

    //互斥锁用来资源的竞争
    locker m_queuelocker;

    //信号量用来线程的同步,判断是否有任务需要处理
    sem m_queuesem;

    //线程池是否停止
    bool m_stop;

    //模型切换
    int m_actor_model;

    //数据库
    connection_pool * m_connPool;



};

template<class T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number = 10, int max_requests = 1000) :
    m_actor_model(actor_model), m_connPool(connPool),
    m_thread_number(thread_number), m_max_requests(max_requests),
    m_stop(false), m_threads(NULL){
        //首先判断传进来的参数是否合理，包括最大线程数量和任务数量
        if(thread_number <= 0 || max_requests <= 0){
            throw std::exception();
        }

        //然后开始创建 线程池的数组
        m_threads = new pthread_t[m_thread_number];
        if(!m_threads){
            throw std::exception();
        }

        //然后开始创建线程，并且把它们加入到线程池里面，还要设置为脱离状态
        for(int i = 0; i < m_thread_number; ++i){
            //创建线程，指定其位置，以及运行的函数，传递的参数，
            //这里把该线程池的 this 指针传递给了 worker。这样 worker 就能够调用该线程池的成员变量
            if( pthread_create(m_threads+i, NULL, worker, this) != 0){
                delete [] m_threads;
                throw std::exception();
            }

            //创建成功的话，设置线程脱离
            if(pthread_detach(m_threads[i])){
                delete [] m_threads;
                throw std::exception();
            }
        }
    }


template<class T>
threadpool<T>::~threadpool(){
    //释放线程池的空间
    delete [] m_threads;
    ///不用释放请求队列吗？还是说只需要释放指针

    //关闭服务
    m_stop = true;
}

template<class T>
bool threadpool<T>::append(T* request, int state){
    //首先上锁
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_requests){
        m_queuelocker.unlock();
        return false;
    }

    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuesem.post();
    return true;
}


template<class T>
bool threadpool<T>::append_p(T* request){
    //在添加之前，需要对请求队列上锁，因为他是所有线程都可以访问的
    m_queuelocker.lock();
    //然后判断请求队列是否是满了
    if(m_workqueue.size() >= m_max_requests){
        //解锁，然后返回false
        m_queuelocker.unlock();
        return false;
    }
    //说明可以放进请求队列
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuesem.post();
    return true;
}


template<class T>
void * threadpool<T>::worker(void *arg){
    // 该函数调用 run 函数
    threadpool * pool = (threadpool *) arg;
    pool->run();
    return pool;
}

template<class T>
void threadpool<T>::run(){
    //不停的处理，从请求队列中拿出请求
    while(!m_stop){
        //首先看能不能取到
        m_queuesem.wait();
        m_queuelocker.lock();
        //如果请求队列是空的，但是不可能吧，我觉得甚至要先加锁（似乎不行，先加锁会死锁吧）
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        //从队头取出
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        //如果取出来的请求是错误的
        if(!request){
            continue;
        }

        //取到请求之后，需要根据判断来进行处理

        //如果是 reactor 模式
        if(m_actor_model == 1){
            //那么就查看当前的状态，如果是读数据，那就读数据
            if(request->m_state == 0){
                if(request->read()){
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else{
                    //如果数据读取失败了
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else{
                //如果是写数据
                if(request->write()){
                    request->improv = 1;
                }
                else{
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        //如果是 proactor 模式， 请求去执行解析任务
        else{
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
        
    }
}

#endif