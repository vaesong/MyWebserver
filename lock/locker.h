/*
封装了信号量，互斥锁，以及条件变量
*/
#ifndef LOCKER_H
#define LOCKER_H

#include<exception>
#include<pthread.h>
#include<semaphore.h>

/*封装信号量的类*/
class sem{
public:
    //构造函数
    sem(){
        //初始化信号量
        if(sem_init(&m_sem, 0, 0) != 0)
            //构造函数没有返回值，可以通过抛出异常来报告错误
            throw std::exception();
    }

    //初始化多个信号量？
    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }

    //析构函数
    ~sem(){
        //销毁信号量
        sem_destroy(&m_sem);
    }

    //等待信号量
    bool wait(){
        return sem_wait(&m_sem) == 0;
    }

    //增加信号量
    bool post(){
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

/*封装互斥锁的类*/
class locker{
public:
    //构造函数，初始化互斥锁
    locker(){
        if(pthread_mutex_init(&m_mutex, NULL) != 0)
            throw std::exception();
    }

    //析构函数，销毁互斥锁
    ~locker(){
        pthread_mutex_destroy(&m_mutex);
    }

    //对资源上锁，获取互斥锁
    bool lock(){
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    //释放资源，释放互斥锁
    bool unlock(){
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    //获得互斥锁的地址
    pthread_mutex_t *get(){
        return &m_mutex;
    }

private:
    //定义一个互斥锁
    pthread_mutex_t m_mutex;
};

/*封装条件变量的类*/
class cond{
public:
    //构造函数，初始化条件变量
    cond(){
        if(pthread_mutex_init(&m_mutex, NULL) != 0)
            throw std::exception();
        
        if(pthread_cond_init(&m_cond, NULL) != 0){
            //构造函数中如果出现问题，就应该立即释放已经成功分配的资源
            pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }

    //析构函数，销毁条件变量
    ~cond(){
        pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_cond);
    }

    //等待条件变量
    bool wait(){
        int ret = 0;
        pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, &m_mutex);
        pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    
    //唤醒等待条件变量的线程
    bool signal(){
        return pthread_cond_signal(&m_cond) == 0;
    }

    //等待一定的时间
    bool timewait(struct timespec t){
        int ret = 0;
        pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, &m_mutex, &t);
        pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }

    //唤醒所有等待条件变量 COND 的线程
    bool broadcast(){
        return pthread_cond_broadcast(&m_cond)  == 0;
    }

private:
    //定义一个互斥锁和条件变量
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};
#endif
