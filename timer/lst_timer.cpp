#include "lst_timer.h"
#include "../http/http_conn.h"

//容器的初始化
sort_timer_lst::sort_timer_lst(){
    head == NULL;
    tail == NULL;
}

//容器的析构函数,删除所有的定时器
sort_timer_lst::~sort_timer_lst(){
    util_timer* tmp = head;
    while(tmp){
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

//容器的添加定时器
void sort_timer_lst::add_timer(util_timer* timer){
    //如果定时器是空的
    if(!timer)
        return;
    
    //如果容器是空的
    if(!head){
        head = tail = timer;
        return;
    }

    //如果当前的定时器比 头定时器还要小，那么就放在前面
    if(timer->expire < head->expire){
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}

//添加定时器到带有头结点的容器
void sort_timer_lst::add_timer(util_timer* timer, util_timer* head){
    util_timer* prev = head;
    util_timer* tmp = prev->next;

    while(tmp){
        if(timer->expire < tmp->expire){
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }

    if(!tmp){
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

//调整当前容器内的某个定时器的位置
void sort_timer_lst::adjust_timer(util_timer* timer){
    if(!timer)
        return;

    util_timer *tmp = timer->next;
    if(!tmp || (timer->expire < tmp->expire))
        return;

    if(timer == head){
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    else{
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

//删除指定的定时器
void sort_timer_lst::del_timer(util_timer* timer){
    if(!timer)
        return;
    
    if((timer == head) && (timer == tail)){
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    
    if(timer == head){
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }

    if(timer == tail){
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }

    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

//tick 函数
//SIGALRM信号每次被触发，主循环中调用一次定时任务处理函数，处理链表容器中到期的定时器。
/*遍历定时器升序链表容器，从头结点开始依次处理每个定时器，直到遇到尚未到期的定时器
若当前时间小于定时器超时时间，跳出循环，即未找到到期的定时器
若当前时间大于定时器超时时间，即找到了到期的定时器，执行回调函数，然后将它从链表中删除，然后继续遍历 */
void sort_timer_lst::tick(){
    if(!head)   
        return;

    time_t cur = time(NULL);
    util_timer* tmp = head;

    while(tmp){
        //链表容器为升序排列
        //当前时间小于定时器的超时时间，后面的定时器也没有到期
        if(cur < tmp->expire)
            break;
        
        tmp->cb_func(tmp->user_data);
        head = tmp->next;

        if(head)
            head->prev = NULL;
        
        delete tmp;
        tmp = head;
    }
}


void Utils::init(int timeslot){
    m_TIMESLOT = timeslot;
}

//对文件设置非阻塞
void Utils::setnonblocking(int fd){
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
}

//把事件注册到内核事件表中
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode){
    epoll_event event;
    event.data.fd = fd;

    if(TRIGMode == 1)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    //如果使用 onse_shot
    if(one_shot)
        event.events |= EPOLLONESHOT;

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart){
    struct sigaction sa;
    //这个没有清零，然后会报 Alarm Clock 错误，不用的话，大部分未初始化的变量struct sigaction
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//信号处理函数,把信号发送到主线程的读端，和 I/O 复用的信号事件统一
void Utils::sig_handler(int sig){
    //为了保证函数的可重入性，保留原来的 errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//定时处理任务，重新定时以不断触发 SIGALRM 信号
void Utils::timer_handler(){
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char* info){
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int* Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;

void cb_func(client_data *user_data)
{   
    if(user_data->sockfd != -1){
        epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
        assert(user_data);
        close(user_data->sockfd);
        http_conn::m_user_count--;
        std::cout << "定时器 关闭了 连接......" << std::endl;
    }
}
