#include"http_conn.h"

//定义 http 相应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";


//初始化静态变量
int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

// const char* doc_root = "/home/hrj/MyWebServer/resources";

//将数据库中的用户名和密码载入到服务器的map中来，map中的key为用户名，value为密码。
void http_conn::initmysql_result(connection_pool *connPool){
    //先从数据库池中取出一个连接
    MYSQL* mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在 user 表中检索 username，password数据，没有返回值是正确的
    if(mysql_query(mysql, "SELECT username,passwd FROM user")){
        LOG_ERROR("SELECT error:%s\n", mysql_errno(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_field(result);

    //从结果集中获取下一行，把对应的用户名和密码，存入到 map 中
    while(MYSQL_ROW row = mysql_fetch_row(result)){
        string temp1(row[0]);
        string temp2(row[1]);

        m_users[temp1] = temp2;
    }

    //总觉得这里应该释放这个连接
}


//设置文件描述符非阻塞
void setnonblocking(int fd){
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
}

//把文件描述符需要监听的事件注册到内核事件表中
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode){
    //创建事件
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    // event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;

    //看触发模式,如果是 ET 模式
    if(TRIGMode == 1)
        event.events |= EPOLLET;

    //如果使用 one_shot 模式
    if(one_shot){
        event.events |= EPOLLONESHOT;
    }

    //加入到内核事件表中
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

    //设置文件描述符非阻塞
    setnonblocking(fd);
}

//从内核事件表中删除注册的文件描述符及其事件
void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//对与工作线程来说，完成了 EPOLLIN 事件之后，需要修改, 把事件重新置为 EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    //如果是 ET 模式
    if(TRIGMode == 1)
        event.events |= EPOLLET;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//释放mmap创建的内存空间
void http_conn::unmap(){
    if(m_file_address){
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}


//初始化连接
void http_conn::init(int sockfd, const sockaddr_in & addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname){
    //首先让成员变量赋值
    m_sockfd = sockfd;
    m_address = addr;

    //添加端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    //添加到 epoll 中，监听读写事件
    addfd(m_epollfd, m_sockfd, true, TRIGMode);

    //用户的数量加一
    m_user_count++;

    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    //这里是？？？来个连接就copy数据库名字？
    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}


//初始化一些读取与解析的成员变量
void http_conn::init(){
    //主状态机初始化为解析请求行
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_start_line = 0;
    m_checked_index = 0;
    m_read_idx = 0;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_host = 0;
    m_linger = false;
    m_content_length = 0;

    m_write_idx = 0;

    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    mysql = NULL;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);

}


//关闭连接, 不用从 users 数组里面进行删除吗？还是说放空了一个文件描述符，新来的连接会自动初始化
void http_conn::close_conn(){
    if(m_sockfd != -1){
        //首先从 epoll 事件表中删除
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}


//读取数据,直到无数据可读或者对方关闭连接
//根据触发模式进行判断
bool http_conn::read(){
    LOG_INFO("A read event occurs, and all data is read at one time");

    //如果读缓冲区满了
    if(m_read_idx >= READ_BUFFER_SIZE)
        return false;

    //定义一个已经读取的数据量
    int bytes_read = 0;

    // LT 模式下的读取数据
    if(m_TRIGMode == 0){
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if(bytes_read <= 0)
            return false;

        return true;
    }
    //ET 模式下的读取数据
    else{
        while(true){
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            //如果出错了
            if(bytes_read == -1){
                //在出错的情况下，如果没有数据，就返回
                if(errno == EAGAIN || errno == EWOULDBLOCK){
                    break;
                }
                return false;
            }
            else if(bytes_read == 0){
                //如果客户端关闭了连接
                return false;
            }
            m_read_idx += bytes_read;
        }
        // std::cout << "读到了数据：" << m_read_buf << std::endl;
        return true;
    }
}

//写数据,相应报文已经准备好了，在 iv 处，然后开始写给 客户端 socket
bool http_conn::write(){
    // std::cout << "发生写事件，一次性写完所有数据" << std::endl;
    LOG_INFO("When a EPOLLOUT event occurs, all data is written at one time")
    int temp = 0;

    //已经发送的字节数
    int bytes_have_send = 0;
    //还要发送的字节数
    int bytes_to_send = m_write_idx;

    if(m_iv_count == 2)
        bytes_to_send += m_iv[1].iov_len;

    //如果将要发送的字节数是 0，结束发送
    if(bytes_to_send == 0){
        //这次响应结束，重新让 该 socket 注册 EPOLLIN 事件
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        //这里初始化，等待下一次的数据发送
        init();
        return true;
    }

    //否则就不停的 发送数据
    while(true){
        //分散写 //将响应报文的状态行、消息头、空行和响应正文发送给浏览器端
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp < 0){
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            //失败了就释放 mmap 创建的内存空间
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        //如果第一个 iovec 头部信息的数据已经发完
        if(bytes_have_send >= m_iv[0].iov_len){
            //不再继续发送头部信息
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else{
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        //如果数据已经全部发送完成
        if(bytes_to_send <= 0){
            unmap();
            //让该 socket 重新注册 EPOLLIN 事件
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
            //如果浏览器请求是长连接
            if(m_linger){
                //重新初始化 HTTP 对象
                init();
                return true;
            }
            else{
                //返回 false 就相当于出错？直接关闭连接（但是数据发完了）？
                return false;
            }
                
        }
    }
    return true;
}


//解析 HTTP 请求-------------------------------
http_conn::HTTP_CODE http_conn::process_read(){
    //利用主状态机的转换来推动解析
    //设置从状态机的状态
    LINE_STATUS line_status = LINE_OK;
    //设置解析完三大位置之后 得到的状态
    HTTP_CODE ret = NO_REQUEST;
    //设置需要解析的行文本
    char *text = 0;

    //当从状态机读完一行，没有发现错误，就继续循环读取，处理
    //在 POST 请求中，消息体的末尾没有任何字符（\r\n 吗?），所以不能仅使用从状态机的状态，还要判断是否是读取的消息体
    //消息体的内容不需要检查
    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)){
        text = get_line();
        //设置起始行为已经检查结束的行，（向后移动）
        m_start_line = m_checked_index;
        LOG_INFO("%s",  text);

        //然后根据主状态机的状态来判断该处理那个部分
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:{
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST)
                    return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADER:{
                ret = parse_headers(text);
                if(ret == BAD_REQUEST)
                    return BAD_REQUEST;
                else if(ret == GET_REQUEST)
                    return do_request();
                break;
            }
            case CHECK_STATE_CONTENT:{
                ret = parse_content(text);
                if(ret = GET_REQUEST)
                    return do_request();
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    //判断 parse_line() 的值是不是 LINE_BAD
    if(line_status == LINE_BAD)
        return BAD_REQUEST;
    return NO_REQUEST;
}

//从状态机对一行进行解析，判断该行是否是完整的
//m_checked_index 从 m_start_line 开始向后检查，检查到 '\r\n'的时候返回
//通过 getline() 获取 m_start_line 到 m_checked_index 的一行
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for(; m_checked_index < m_read_idx; ++m_checked_index){
        temp = m_read_buf[m_checked_index];
        if(temp == '\r'){
            //如果后面没有了，返回行不完整
            if((m_checked_index + 1) == m_read_idx)
                return LINE_OPEN;
            //如果遇到了 ‘\r\n’
            else if(m_read_buf[m_checked_index + 1] == '\n'){
                m_read_buf[m_checked_index++] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n'){
            //为什么会碰到这种情况？前面的 '\r' 没碰到
            if(m_checked_index > 1 && m_read_buf[m_checked_index - 1] == '\r'){
                m_read_buf[m_checked_index - 1] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//解析 http 请求行，获得请求方法，目标URL以及http版本号 ++
http_conn::HTTP_CODE http_conn::parse_request_line(char * text){
    //GET / HTTP/1.1\0\0
    //strpbrk是在源字符串（s1）中找出最先含有搜索字符串（s2）中任一字符的位置并返回，若找不到则返回空指针。
    //这里就是 空字符 或者 '\t',如果没有必定是错的
    m_url = strpbrk(text, " \t");
    if(!m_url)
        return BAD_REQUEST;
    
    *m_url++ = '\0';    // *m_url = '\0'; m_url++;
    char *method = text;

    //判断字符串是否相等的函数，忽略大小写, 返回值大于 s1 < s2 --> 小于0, s1 == s2 --> 等于0, s1 > s2 --> 大于0
    if(strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if(strcasecmp(method, "POST") == 0){
        m_method = POST;
        cgi = 1;
    }
    else
        return BAD_REQUEST;

    //该函数返回 str1 中第一个不在字符串 str2 中出现的字符下标。从str1第一个开始，前面的字符有几个在str2中。
    //相当于向后移动，消除空格和 \t
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if(!m_version)
        return BAD_REQUEST;
    
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if(strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    
    //用来比较参数s1和s2字符串前n个字符
    if(strncasecmp(m_url, "http://", 7) == 0){
        m_url += 7;
        //在字符串 s 中查找字符 c，返回字符 c 第一次在字符串 s 中出现的位置
        m_url = strchr(m_url, '/');
    }
    else if(strncasecmp(m_url, "https://", 8) == 0){
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    //如果是空，或者不是 '/'
    if(!m_url || m_url[0] != '/')
        return BAD_REQUEST;

    //当 URL 是 '/'的时候，显示判断页面
    if(strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    
    //修改状态
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析请求头
http_conn::HTTP_CODE http_conn::parse_headers(char * text){
    //判断是空行还是请求头
    if(text[0] == '\0'){
        //如果有内容信息
        if(m_content_length != 0){
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0){
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0){
            m_linger = true;
        }
    }
    else if(strncasecmp(text, "Content-length:", 15) == 0){
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if(strncasecmp(text, "Host:", 5) == 0){
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else{
        LOG_INFO("oop! unknow header: %s", text);
        // std::cout << "oop! unknow header: " << text << std::endl;
    }
    return NO_REQUEST;
}

//解析请求体，主状态机判断 http 请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char * text){
    //如果接收到的字节数的位置比（内容的长度 + 已经检查的位置）大于等于
    //判断buffer中是否读取了消息体
    if(m_read_idx >= (m_content_length + m_checked_index)){
        text[m_content_length] = '\0';
        //POST请求中最后是输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//已经完成了 http 请求的解析，对请求的资源进行判断，返回响应状态
http_conn::HTTP_CODE http_conn::do_request(){
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    // strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    // std::cout << m_real_file << std::endl;
    //找到 '/' 第一次出现的位置
    const char* p = strchr(m_url, '/');

    //注册和登录校验
    if(cgi == 1 && (*(p+1) == '2' || *(p+1) == '3')){
        //根据标志判断是登录检测还是注册检测,甚至用不上？而且不应该用 p+1 吗
        char flag = m_url[1];

        //这一段总是觉得很奇怪？？？？？
        // char *m_url_real = (char*)malloc(sizeof(char) * 200);
        // strcpy(m_url_real, "/");
        // //为什么是 + 2，不是 + 1？？
        // strcat(m_url_real, m_url + 2);
        // strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        // free(m_url_real);

        //把用户名和密码提取出来
        //user=123&password=123
        char name[100], password[100];
        int i;
        for(i = 5; m_string[i] != '&'; ++i)
            name[i-5] = m_string[i];
        name[i-5] = '\0';

        int j = 0;
        for(i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';
        
        //如果是注册校验
        if(*(p+1) == '3'){
            //先检查数据库中是否有重名的
            //如果没有重名的，就增加数据
            char* sql_insert = (char*)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            std::cout << sql_insert << std::endl;
            //判断map中能否找到重复的用户名
            if(m_users.find(name) == m_users.end()){
                //向数据库中插入数据时，需要通过锁来同步数据
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                //同时 map 中也保存一份
                m_users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                //不用释放内存吗？
                free(sql_insert);

                //校验成功，进入登录界面
                if(!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        //如果是登录，直接判断 map 中是否存在
        else if(*(p+1) == '2'){
            if(m_users.find(name) != m_users.end() && m_users[name] == password){
                strcpy(m_url, "/welcome.html");
            }
            else
                strcpy(m_url, "/logError.html");
        }
    }

    //如果是 0，那就跳转到注册界面  GET
    if(*(p+1) == '0'){
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        //所有这里的 m_real_file + len 是起始位置，复制 strlen(m_url_real) 个字符，也即是全部字符串
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //如果是 1，那就跳转到登录页面  GET
    else if(*(p+1) == '1'){
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //如果是 5，那就是显示图片界面  POST
    else if(*(p+1) == '5'){
        char *m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //如果是 6， 那就是视频界面     POST
    else if(*(p+1) == '6'){
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //如果是 7，那就是显示关注界面      POST
    else if(*(p+1) == '7'){
        char *m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //否则就直接把 m_url 添加到后面,这里其实就是已经校验好的 m_url
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    //获取请求文件的信息
    if(stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    
    //判断是否有访问权限
    if(!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    //判断是否是目录
    if(S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    //然后打开文件，用只读的方式
    int fd = open(m_real_file, O_RDONLY);
    //创建内存映射
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    close(fd);
    return FILE_REQUEST;
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret){
    switch(ret){
        case INTERNAL_ERROR:{
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form))
                return false;
            break;
        }
        case BAD_REQUEST:{
            add_status_line(404, error_400_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_400_form))
                return false;
            break;
        }
        case FORBIDDEN_REQUEST:{
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form))
                return false;
            break;
        }
        //准备文件到 iv，说明所有东西准备好了，包括 写缓冲区的响应报文 以及 相应的文件
        case FILE_REQUEST:{
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        }

        default:
            return false;        
    }

    //除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    // bytes_to_send = m_write_idx;
    return true;
}

//根据状态码和 title 拼凑出完整的 相应行
bool http_conn::add_status_line(int status, const char* title){
    return add_response("%s, %d, %s\r\n", "HTTP/1.1", status, title);
}

//拼凑响应头部信息，包括内容类型，内容大小，是否是长连接等等
bool http_conn::add_headers(int content_len){
    return add_content_type() && add_content_length(content_len) && add_linger() && add_blank_line();
}

//然后就是响应头的内容类型，调用 add_response 写入写缓冲区, 需要添加 charset=utf-8，不然显示乱码
bool http_conn::add_content_type(){
    return add_response("Content-Type:%s;charset=utf-8\r\n", "text/html");
}

//响应头的内容长度
bool http_conn::add_content_length(int content_length){
    return add_response("Content-Length:%d\r\n", content_length);
}

//响应头头的编码方式
bool http_conn::add_encoding(){
    return add_response("Content-Encoding:deflate\r\n");
}
//响应头的是否是长连接
bool http_conn::add_linger(){
    return add_response("Conection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

//添加相应空行
bool http_conn::add_blank_line(){
    return add_response("%s", "\r\n");
}

//添加内容
bool http_conn::add_content(const char* content){
    return add_response("%s", content);
}


//更新m_write_idx指针和缓冲区m_write_buf中的内容
bool http_conn::add_response(const char *format, ...){
    //如果写缓冲区满了
    if(m_write_idx >= WRITE_BUFFER_SIZE)
        return false;

    //定义可变参数列表
    va_list arg_list;
    //把变量 arg_list 初始化为传入的参数
    va_start(arg_list, format);

    //将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    int len=vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);

    //如果写入的数据长度超过缓冲区剩余空间，则报错
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }

    //更新m_write_idx位置
    m_write_idx += len;
    //清空可变参列表
    va_end(arg_list);
    return true;
}


void http_conn::process(){

    //解析 http 请求报文
    HTTP_CODE read_ret = process_read();
    //如果 ret 是NO,说明数据还没传输完成，重新创建 EPOLLIN 事件,
    //剩下的不管是成功还是失败，都交给生成响应报文那部分去处理
    if(read_ret == NO_REQUEST){
        //修改 socket 可以触发 EPOLLIN 事件
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    LOG_INFO("Client message parsing is complete and response message is being generated...");
    // std::cout << "客户端 " << m_sockfd << " 报文解析完成,结果是："<< (read_ret == NO_RESOURCE) << "... 正在生成响应报文..." << std::endl;
    //生成 http 响应报文
    bool write_ret = process_write(read_ret);
    // std::cout << "响应报文生成结果是 " << write_ret << " ..." << std::endl;
    //如果出错，关闭连接
    if(!write_ret)
        close_conn();
    //否则注册 EPOLLOUT 事件，modfd强制触发一次 EPOLLOUT
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}