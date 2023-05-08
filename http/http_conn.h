#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
class http_conn
{
public:
    // 设置读取名称为m_real_file的文件的大小
    static const int FILENAME_LEN = 200;
    // 设置读缓冲区 m_real_buf的大小
    static const int READ_BUFFER_SIZE = 2048;
    // 设置写缓冲区m_write_buf的大小
    static const int WRITE_BUFFER_SIZE = 1024;
    // 报文的请求方法，本项目只有 GET 和 POST
    enum METHOD
    {
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
    // 主状态机的状态
    enum CHECK_STATE
    {
        // 解析请求行
        CHECK_STATE_REQUESTLINE = 0,
        // 解析请求头
        CHECK_STATE_HEADER,
        // 解析消息体，仅用于解析POST请求
        CHECK_STATE_CONTENT
    };
    // 报文的解析结果
    enum HTTP_CODE
    {
        // 获取的请求不完整，需要继续读取请求报文数据
        // 跳转主线程继续监测读事件
        NO_REQUEST,
        // 获得了完整的HTTP请求
        // 调用do_request完成请求资源映射
        GET_REQUEST,
        // HTTP请求报文有语法错误
        // 跳转process_write完成响应报文
        BAD_REQUEST,
        NO_RESOURCE,  // 请求资源不存在，跳转process_write完成响应报文
        FORBIDDEN_REQUEST, // 请求资源禁止访问，没有读取权限，跳转process_write完成响应报文
        FILE_REQUEST,  // 请求资源可以正常访问，跳转process_write完成响应报文
        INTERNAL_ERROR,  // 服务器内部错误，该结果在主状态机逻辑switch的default下，一般不会触发
        CLOSED_CONNECTION
    };
    // 从状态机的状态
    enum LINE_STATUS
    {
        // 完整读取一行
        LINE_OK = 0,
        // 报文语法有误
        LINE_BAD,
        // 读取的行不完整
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    // 初始化这个http连接，里面有一个socket文件描述符，并把socket注册进epoll
    void init(int sockfd, const sockaddr_in &addr);
    // 关闭 http 连接，从 epoll中移除，并将文件描述符置为-1
    void close_conn(bool real_close = true);
    // request 处理函数，里面调用 process_read process_write
    void process();
    // 读取浏览器端发来的全部数据
    bool read_once();
    // 响应报文写入函数
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    // 同步线程初始化数据库读取表,不知道干啥的
    void initmysql_result(connection_pool *connPool);

private:
    // 成员变量初始化
    void init();
    // 从m_read_buf缓冲区中读取，并处理请求报文
    HTTP_CODE process_read();
    // 向m_write_buf缓冲区写入响应报文数据
    bool process_write(HTTP_CODE ret);
    // 主状态机解析报文中的请求行数据
    HTTP_CODE parse_request_line(char *text);
    // 主状态机解析报文中的请求头数据
    HTTP_CODE parse_headers(char *text);
    // 主状态机解析报文中的请求内容
    HTTP_CODE parse_content(char *text);
    // 生成响应报文
    HTTP_CODE do_request();
    // m_start_line是已经解析的字符
    // get_line用于将指针向后偏移，指向未处理的字符
    // init 时 m_start_line 就是 0
    char *get_line() { return m_read_buf + m_start_line; };
    // 从状态机读取一行，分析是请求报文的哪一部分
    LINE_STATUS parse_line();
    void unmap();
    // 根据响应报文格式，生成对应8个部分，以下函数均由do_request调用
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    // http 类也保管 epoll 文件描述符
    static int m_epollfd;
    // http 类 上开了多少个连接
    static int m_user_count;
    MYSQL *mysql;

private:
    // 这个 http 连接握着的socket文件描述符
    int m_sockfd;
    sockaddr_in m_address;
    // 读到的数据放到这个缓冲区
    char m_read_buf[READ_BUFFER_SIZE];
    // 读缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    int m_read_idx;
    // 读缓冲区m_read_buf当前读取到的位置m_checked_idx
    int m_checked_idx;
    // m_read_buf中已经解析的字符个数
    int m_start_line;
    // 写缓冲区，存储发出的响应报文数据
    char m_write_buf[WRITE_BUFFER_SIZE];
    // 指示写进去的长度
    int m_write_idx;
    // 主状态机的状态
    CHECK_STATE m_check_state;
    // 请求方法，GET or POST
    METHOD m_method;
    // 以下为解析请求报文中对应的6个变量
    // 存储读取文件的名称
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;
    char *m_file_address;  // 读取服务器上的文件地址
    struct stat m_file_stat;
    struct iovec m_iv[2];  // io向量机制iovec ？？
    int m_iv_count;
    int cgi;        //是否启用的POST
    char *m_string; //存储请求头数据
    int bytes_to_send;  // 剩余发送的字节数
    int bytes_have_send;  // 已发送的字节数
};

#endif
