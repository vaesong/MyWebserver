#include "webserver.h"

WebServer::WebServer(){
    //创建客户端用户数组
    users = new http_conn[MAX_FD];

    //root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //存放带有用户数据和定时器的数组
    users_timer = new client_data[MAX_FD];
}


WebServer::~WebServer(){
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

//初始化函数，主要是保存用户自定义的一些数据
void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_password = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

//设置触发模式
void WebServer::trig_mode()
{
    //LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

//初始化日志
void WebServer::log_write()
{
    //如果使用日志
    if (0 == m_close_log)
    {
        //初始化日志
        if (1 == m_log_write)
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

//初始化数据库连接
void WebServer::sql_pool()
{
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_password, m_databaseName, 3306, m_sql_num, m_close_log);

    //初始化数据库读取表
    users->initmysql_result(m_connPool);
}

//创建线程池
void WebServer::thread_pool(){
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

//网络编程的基本步骤
void WebServer::eventListen(){

    //首先创建一个服务端的监听套接字
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    //优雅关闭连接
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    //设置端口复用
    int reuse = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    int ret = 0;
    //绑定
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    //设置地址复用
    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    
    //绑定地址
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    //设置监听的数量
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT);

    //设置epoll事件相关
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;

    //创建信号相关的事件
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    //设置写端是非阻塞的，防止信号无法到达
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    //添加信号
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    //设置定时事件
    alarm(TIMESLOT);

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

//该函数相当于对客户端的初始化操作，包括添加到用户数据以及创建定时器
void WebServer::timer(int connfd, struct sockaddr_in client_address){
    //添加到用户数组，并且初始化
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_password, m_databaseName);

    //初始化client_data 的数据
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;

    util_timer* timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 6 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

//如果客户端关闭了连接，那么就删除定时器
void WebServer::deal_timer(util_timer *timer, int sockfd){
    //调用定时器的回调函数，删除了客户端，其实就相当于 conn_close,但是这里没有把 sockfd 置为 -1
    timer->cb_func(&users_timer[sockfd]);
    //然后把定时器从容器（定时器列表）中删除
    if(timer){
        utils.m_timer_lst.del_timer(timer);
    }
    LOG_INFO("close fd %d by timer", users_timer[sockfd].sockfd);
}

//如果有新的数据到来或者发出，向后调整定时器的过期时间，并且调整容器上的定时器位置
void WebServer::adjust_timer(util_timer* timer){
    time_t cur = time(NULL);
    timer->expire = cur + 6 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "addjust timer once");
}

//处理新来的客户端请求
bool WebServer::dealclinetdata(){
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);

    //监听端使用 LT 触发模式
    if(m_LISTENTrigmode == 0){
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if(connfd < 0){
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        
        //如果用户的数量已经满了
        if(http_conn::m_user_count >= MAX_FD){
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Tnternal server busy");
            return false;
        }
        //把用户加到数组中，同时创建一个保存用户数据的定时器
        timer(connfd, client_address);
    }
    else{
        //采用 ET 模式触发的话，就一直循环监听
        //当接收到一个之后，再次调用accept会返回 -1，从而退出循环
        while(1){
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if(connfd < 0){
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if(http_conn::m_user_count >= MAX_FD){
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
            //这里不设置返回的吗,那怎么退出呢
        }
        //这里返回 false?
        return false;
    }
    return true;
}


//处理信号事件
bool WebServer::dealwithsignal(bool& timeout, bool& stop_server){
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);

    if(ret == -1)
        return false;
    else if(ret == 0)
        return false;
    else{
        //查看所有的信号
        for(int i = 0; i < ret; ++i){
            switch(signals[i]){
                case SIGALRM:{
                    timeout = true;
                    break;
                }
                case SIGTERM:{
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}

//处理客户端的读事件，
void WebServer::dealwithread(int sockfd){
    //获得该客户端的定时器
    util_timer *timer = users_timer[sockfd].timer;

    //reactor模式,检测到读事件，不读，直接交给工作线程去处理
    if(m_actormodel == 1){
        //首先调整一下定时器
        if(timer){
            adjust_timer(timer);
        }

        //然后并不处理，直接加入到线程池的消息队列
        m_pool->append(users + sockfd, 0);

        //这里是？
        while(true){
            if(users[sockfd].improv){
                if(users[sockfd].timer_flag){
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else{
        //使用proactor 模式，先读取数据，再插入到线程池的请求队列中
        if(users[sockfd].read()){
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //如果检测到读事件，把该事件放入到请求队列
            m_pool->append_p(users + sockfd);

            if(timer){
                adjust_timer(timer);
            }
        }
        //如果数据读取失败了
        else{
            deal_timer(timer, sockfd);
        }
    }

}


//处理客户端的 EPOLLOUT 事件
void WebServer::dealwithwrite(int sockfd){
    util_timer* timer = users_timer[sockfd].timer;

    //reactor模式
    if(m_actormodel == 1){
        if(timer)
            adjust_timer(timer);

        m_pool->append(users + sockfd, 1);

        while(true){
            if(users[sockfd].improv == 1){
                if(users[sockfd].timer_flag){
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else{
        //proactor模式
        if(users[sockfd].write()){
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if(timer)
                adjust_timer(timer);
        }
        else{
            deal_timer(timer, sockfd);
        }
    }
}



void WebServer::eventLoop(){
    //该变量是用来看是否到达 SIGALRM 设定的时间
    bool timeout = false;
    bool stop_server = false;

    while(!stop_server){
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        //如果没有事件并且出错了
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        //循环处理这些事件
        for(int i = 0; i < number; ++i){
            int sockfd = events[i].data.fd;

            //处理新连接的客户端
            if(sockfd == m_listenfd){
                bool flag = dealclinetdata();
                if(flag == false)
                    continue;
            }
            //处理客户端连接异常，可以直接关闭
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                //服务端关闭与客户端的连接，清除定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理信号时间，主要是定时事件
            else if((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)){
                bool flag = dealwithsignal(timeout, stop_server);
                if(flag == false)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            //处理客户端上的数据，如果是接收到了数据
            else if(events[i].events & EPOLLIN){
                dealwithread(sockfd);
            }
            //处理客户端的写数据
            else if(events[i].events & EPOLLOUT){
                dealwithwrite(sockfd);
            }
        }

        //如果定时时间到了
        if(timeout){
            utils.timer_handler();

            LOG_INFO("%s", "timer tick...");
            timeout = false;
        }

    }
}