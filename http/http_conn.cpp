#include "http_conn.h"
#include "../log/log.h"
#include <map>
#include <mysql/mysql.h>
#include <fstream>

//#define connfdET //边缘触发非阻塞
#define connfdLT //水平触发阻塞

//#define listenfdET //边缘触发非阻塞
#define listenfdLT //水平触发阻塞

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

//当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
const char *doc_root = "/home/ysl/ysl-TinyWebServer-raw_version/root";

//将表中的用户名和密码放入map：
map<string, string> users;
locker m_lock;

void http_conn::initmysql_result(connection_pool *connPool)
{
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

//对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;  
    // 搞一个 epoll_event 然后把文件描述符给 event.data.fd
    /*
    struct epoll_event {
    uint32_t events;    // 事件类型
    epoll_data_t data;  // 用户数据
    };

    typedef union epoll_data {
    void        *ptr;
    int          fd;
    uint32_t     u32;
    uint64_t     u64;
    } epoll_data_t;

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP; 
    ev.data.fd = sockfd;   // 将socket fd存入用户数据
    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);

    - events:事件类型,可以是上面提到的EPOLLIN、EPOLLOUT等等。
    - data:用户数据,这里我们通常会存入socket fd,用于确定就绪的socket
    */ 

#ifdef connfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef connfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

#ifdef listenfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef listenfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;

#ifdef connfdET
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
#endif

#ifdef connfdLT
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
#endif

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

//初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr)
{
    m_sockfd = sockfd;
    m_address = addr;
    //int reuse=1;
    //setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    addfd(m_epollfd, sockfd, true);
    m_user_count++;
    init();
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

//从状态机，用于分析出一行内容
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
// 在HTTP报文中，每一行的数据由\r\n作为结束字符，空行则是仅仅是字符\r\n。
// 因此，可以通过查找\r\n将报文拆解成单独的行进行解析
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r')
        {
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
// TODO 不懂，LT ET 需要对着有双学一学
bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

#ifdef connfdLT  // 水平触发阻塞模式

    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);

    if (bytes_read <= 0)
    {
        return false;
    }

    m_read_idx += bytes_read;

    return true;

#endif

#ifdef connfdET
    while (true)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            // 这个判断实现了在无数据可读时"跳出循环,等待epoll通知",这是非阻塞IO编程的精髓。
            return false;
        }
        else if (bytes_read == 0)
        {
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
#endif
}

//解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
// - strpbrk: 查找字符串中第一个匹配给定字符集中的任意字符的位置。用于查找字符串中的分隔符等。
//- strcasecmp: 字符串比较,不区分大小写。用于忽略大小写比较两个字符串。
//- strspn: 计算字符串中第一个不在给定字符集中的字符的位置。用于跳过字符串开头的某些字符。
//- strchr: 查找字符串中某个字符第一次出现的位置。
//- strlen: 获取字符串的长度。
//- strcat: 字符串连接,将一个字符串连接到另一个字符串的末尾。
{
    m_url = strpbrk(text, " \t");  // \t 是 tab，\t 前面还有空格
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
    // strcasecmp()实际上是逐个字符比较两个字符串,直到发现不相等的字符或遇到字符串结束符('\0')为止。
    //在这个示例中,method字符串的前3个字符与"GET"相同,但第4个字符是一个字符串结束符'\0'。
    // 此时,strcasecmp()会认为"GET\0"已经是method字符串的全部内容,并且与比较字符串"GET"相等,所以返回0。
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else
        return BAD_REQUEST;
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0)
    // 忽略大小写比较两个字符串的前n个字符
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    //当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析http请求的一个头部信息
// text 在 while + get_line() 中已经指向请求头了
// 这在while中不断循环处理，一次转换一行，直到 text[0] == '\0'
// 如果 text[0] == '\0' 即遇到空行，要是 m_content_length 还是默认值 0
// 说明是 GET 请求，就结束了，可以做响应了
// 如果 text[0] == '\0' 即遇到空行，但是 m_content_length 不为 0
// 说明是 POST 请求，还要继续处理请求数据
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);  // atol()函数用于将字符串转换为长整型数(long)。
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        //printf("oop!unknow header: %s\n",text);
        LOG_INFO("oop!unknow header: %s", text);
        Log::get_instance()->flush();
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';  // 加结束标识，前面是 请求数据
        //POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
//    LOG_INFO("test checked %d", m_checked_idx);

    // m_check_state 初始状态是 CHECK_STATE_REQUESTLINE，line_status 初始状态是 LINE_OK
    // 使用while循环,可以每读取一部分数据就立即判断状态并处理,不断重复直到解析完成，比如 ret = NO_REQUEST
    // 则跳出 switch 在 while中转
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
//      LOG_INFO("test checked end %d", m_checked_idx);
        // m_start_line是行在buffer中的起始位置，将该位置后面的数据赋给text，
        // 初始时值为0，后面m_start_line = m_checked_idx; 改变了值
        // 此时从状态机已提前将一行的末尾字符\r\n变为\0\0，
        // 所以text可以直接取出完整的行进行解析
        // LOG_INFO("%s", text); 默认遇到 \0 判断为字符串结束，停止打印
        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        Log::get_instance()->flush();
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            // 解析请求行
//            LOG_INFO("test how to loop");
//            LOG_INFO("%s", text);
            ret = parse_request_line(text);
            // 得到解析这一行的结果，同时请求行解析成功，主状态机状态变为 CHECK_STATE_HEADER
            // 但是 ret = NO_REQUEST 跳出 switch 去 while 那里
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;  // 用于退出 switch，并不会退出while
        }
        case CHECK_STATE_HEADER:
        {
            // 解析请求头
            ret = parse_headers(text);  // 如果 header 有内容，text自己加
            // 得到解析这一行的结果
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            // 完整解析GET请求后，跳转到报文响应函数do_request()
            else if (ret == GET_REQUEST)
            {
                return do_request();  // 只是while进不去了，才函数返回
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            // 解析消息体
            ret = parse_content(text);
            // 完整解析POST请求后，跳转到报文响应函数
            if (ret == GET_REQUEST)
                return do_request();
            // 解析完消息体即完成报文解析，避免再次进入循环，所以更新line_status
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}
// TODO 具体还需再看
http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);  // The strcpy() functions copy the string src to dst
    int len = strlen(doc_root);
    //printf("m_url:%s\n", m_url);
    const char *p = strrchr(m_url, '/');
    // strrchr()函数用于在字符串中查找某个字符最后一次出现的位置。
    // 在HTTP解析中,我们经常使用它在URL中查找最后一个'/'字符,以将URL分成路径和文件名两个部分:

    //处理cgi
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {

        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        // 这个函数会从src字符串拷贝前n个字符到dest字符串
        // dst 起始位置是 m_real_file + len，所以相当于追加
        free(m_url_real);

        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        //同步线程登录校验
        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end())
            {

                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    // 尝试获取m_real_file的文件信息,如果失败(返回-1),则表示资源文件不存在,返回NO_RESOURCE状态码;
    //如果成功(返回0),则文件信息存储在m_file_stat结构体中,用于后续判断文件权限和类型。
    //举个例子,如果m_real_file为:
    ///home/wwwroot/index.html
    //则成功执行stat()后,m_file_stat中可能包含如下信息:
    //- st_mode: 0100644  # 文件,可读可写可执行
    //- st_size: 1024     # 文件大小1KB
    //- st_mtime: 1609782000   # 最后修改时间
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    // 判断文件的权限，是否可读，不可读则返回FORBIDDEN_REQUEST状态
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    // 判断文件类型，如果是目录，则返回BAD_REQUEST，表示请求报文有误
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    // 1. 以只读方式获取文件描述符
    // O_RDONLY表示只读方式，文件描述符是打开文件后的一个数字,
    // 用于标识该文件,我们可以通过它对文件进行各种系统调用。
    int fd = open(m_real_file, O_RDONLY);
    // 2. 通过mmap()使用fd将m_real_file映射到内存,并获取映射区的起始地址m_file_address
    //- addr一般设置为0,表示由系统分配地址
    //- length是要映射的字节数,这里为文件大小
    //- prot是内存页保护标志,这里是PROT_READ表示可读
    //- flags一般为MAP_PRIVATE,表示映射的内存页是私有的
    //- fd是open()返回的文件描述符
    //- offset一般为0,表示从文件起始位置开始映射
    // 通过这种方式,我们可以直接操作内存(m_file_address)来读取文件内容,无需真正读取文件。
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    // 3. 关闭fd,避免资源泄露
    close(fd);
    return FILE_REQUEST;
}
// 解除文件映射，释放该映射区域占用的资源
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

// 服务器子线程调用process_write完成响应报文，随后注册epollout事件。
// 服务器主线程检测写事件，并调用http_conn::write函数将响应报文发送给浏览器端。
bool http_conn::write()
{
    int temp = 0;

    // 若要发送的数据长度为0
    // 表示响应报文为空，一般不会出现这种情况
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        // writev()会先将各内存区域的数据拷贝到一个连续的内部缓冲区,再通过一次系统调用将其发送出去。
        // 这样可以减少系统调用次数,也避免多次的数据拷贝,达到较高效率。
        // end()一次只能发送一个连续内存区域的数据,writev()可以一次发送多个非连续内存区域的数据。
        // writev()调用内部会使用send()等系统调用,但在此基础上实现I/O向量和零拷贝等机制,提供较高层的接口。

        if (temp < 0)  // 发送失败
        {
            if (errno == EAGAIN)  // 判断缓冲区是否满了，这里指 writev 构建的内部缓冲区
            // EAGAIN 错误表示当前缓冲区已满，无法继续发送数据，需要等待一段时间后重试。
            // EAGAIN 错误通常与非阻塞套接字一起使用，用于提示应用程序在发送数据时需要等待一段时间，
            // 直到缓冲区有足够的空间。
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        if (bytes_have_send >= m_iv[0].iov_len)  // 0 全部发出去了
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else  // 0 没有全部发出去
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)  // 0 1 都发完了
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

bool http_conn::add_response(const char *format, ...)
// 函数声明中的 const char format 表示第一个参数是一个 const char 类型的指针，
// 该指针指向一个格式化字符串，用于定义后续可变参数的格式。而 ... 则表示后续参数的数量和类型是可变的，
// 可以是任何类型的数据。
// 格式化字符串是一种带有占位符的字符串，占位符用于指定后续参数的格式和位置。
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;  // va_list 是一个可变参数列表类型，通常用于定义函数接受可变数量的参数
    va_start(arg_list, format);  // 创建va_list, format 之后的东西都塞到 arg_list 里面
    // 使用 vsnprintf 函数进行字符串格式化，并将格式化后的字符串写入到 m_write_buf + m_write_idx 中。
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    LOG_INFO("request:%s", m_write_buf);
    Log::get_instance()->flush();
    return true;
}
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();  // 是否 keep-alive
    add_blank_line();
}
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            // 第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            // 第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
            m_iv[1].iov_base = m_file_address;  // mmap 映射到内存上的地址
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            // 如果请求的资源大小为0，则返回空白html文件
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}
