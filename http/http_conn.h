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
    // 常量定义
    //设置读取文件的名称m_real_file大小
    static const int FILENAME_LEN = 200;
    //设置读缓冲区m_read_buf大小
    static const int READ_BUFFER_SIZE = 2048;
    //设置写缓冲区m_write_buf大小
    static const int WRITE_BUFFER_SIZE = 1024;
    // HTTP请求方法枚举 //报文的请求方法，本项目只用到GET和POST
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
    // 检查HTTP请求状态枚举
    //主状态机的三种状态
    //第一行当前正在分析请求行，第二行当前正在分析头部字段，第三行当前正在分析内容
    //解析客户请求时，主状态机所处的状态
 
    //主状态机的状态
    enum CHECK_STATE
    {
        //分析请求行
        CHECK_STATE_REQUESTLINE = 0,
        //分析报文头部
        CHECK_STATE_HEADER,
        //分析消息体状态
        CHECK_STATE_CONTENT
    };
    // HTTP响应状态码枚举
    //服务器处理HTTP请求的结果，一共有下面的几种状态
    //报文解析的结果
    enum HTTP_CODE
    {
        NO_REQUEST,//表示无请求，即初始状态。
        GET_REQUEST,//表示收到了一个有效的GET请求。
        BAD_REQUEST,//表示收到了一个无效的请求。
        NO_RESOURCE,//表示请求的资源不存在。
        FORBIDDEN_REQUEST,//: 表示请求被拒绝访问。
        FILE_REQUEST,//表示请求的是一个文件。
        INTERNAL_ERROR,// 表示服务器内部错误。
        CLOSED_CONNECTION//表示连接已关闭。
    };
    // 解析HTTP请求行的行状态枚举
    //请求行一共有下面三种状态
    //从状态机的状态
    enum LINE_STATUS
    {
        LINE_OK = 0,//表示成功解析一行。
        LINE_BAD,//表示解析的行有误。
        LINE_OPEN，//表示行数据尚未完全接收，仍处于打开状态。
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    // 初始化连接//初始化套接字地址，函数内部会调用私有方法init
    //初始化接受新的连接
    void init(int sockfd, const sockaddr_in &addr);
    // 关闭连接//关闭http连接
    void close_conn(bool real_close = true);
    // 处理HTTP请求  即处理客户请求
    void process();
    //读取浏览器端发来的全部数据 非阻塞读
    bool read_once();
    //响应报文写入函数 非阻塞写
    bool write();
    // 获取连接地址信息
    sockaddr_in *get_address()
    {
        return &m_address;
    }
   
    //CGI使用线程池初始化数据库表
    void initmysql_result(connection_pool *connPool);

private:
    // 初始化连接
    void init();
    // 处理读取的HTTP请求数据//从m_read_buf读取，并处理请求报文 //解析http请求
    HTTP_CODE process_read();
    // 处理写入HTTP响应数据//向m_write_buf写入响应报文数据
    //填充http应答
    bool process_write(HTTP_CODE ret);
    //下面一组函数被process_read函数调用以分析http请求
    // 解析HTTP请求行//主状态机解析报文中的请求行数据
    HTTP_CODE parse_request_line(char *text);
    // 解析HTTP请求头//主状态机解析报文中的请求头数据
    HTTP_CODE parse_headers(char *text);
    // 解析HTTP请求内容//主状态机解析报文中的请求内容
    HTTP_CODE parse_content(char *text);
    // 处理HTTP请求//生成响应报文
    HTTP_CODE do_request();
    
    //m_start_line是行在buffer中的起始位置，将该位置后面的数据赋给text
    //此时从状态机已提前将一行的末尾字符\r\n变为\0\0，所以text可以直接取出完整的行进行解析
    char *get_line() { return m_read_buf + m_start_line; };
    // 解析一行HTTP请求数据 //从状态机读取一行，分析是请求报文的哪一部分
    //这是从状态机，用于解析出一行内容
    LINE_STATUS parse_line();

    //下面一组函数被process_write调用以填充http应答
    // 释放文件映射
    void unmap();
    //根据响应报文格式，生成对应8个部分，以下函数均由do_request调用
    // 添加HTTP响应内容
    bool add_response(const char *format, ...);
    // 添加HTTP响应内容主体
    bool add_content(const char *content);
    // 添加HTTP响应状态行
    bool add_status_line(int status, const char *title);
    // 添加HTTP响应头
    bool add_headers(int content_length);
    // 添加HTTP响应内容类型
    bool add_content_type();
    // 添加HTTP响应内容长度
    bool add_content_length(int content_length);
    // 添加HTTP响应内容长度
    bool add_linger();
    // 添加HTTP响应空行
    bool add_blank_line();

public:
    // 静态成员：Epoll文件描述符和用户计数
    //所有socket上的事件都被注册到同一个epoll内核事件表中，所以将epoll文件描述符设置为静态的
    static int m_epollfd;
    //统计用户数量
    static int m_user_count;
    // MySQL数据库连接对象
    MYSQL *mysql;

private:
    //gaihttp连接的socket和对方的socket地址
    int m_sockfd;// 套接字文件描述符
    sockaddr_in m_address; // 套接字地址信息


    //存储读取的请求报文数据  读缓冲区
    char m_read_buf[READ_BUFFER_SIZE];// 读缓冲区
    //标识缓冲中已经读入的客户端数据的最后一个字节的的下一个位置
    int m_read_idx;// 读缓冲区中已读取的位置
    //m_read_buf读取的位置m_checked_idx
    //当前正在分析的字符在读缓冲区中的位置
    int m_checked_idx; // 读缓冲区中已分析的位置
    //当前正在解析的行的起始位置
    int m_start_line;// 读缓冲区中一行的起始位置

    //存储发出的响应报文数据
    char m_write_buf[WRITE_BUFFER_SIZE];// 写缓冲区
    //指示buffer中的长度
    int m_write_idx;// 写缓冲区中待发送的位置


    //主状态机的状态
    CHECK_STATE m_check_state;// HTTP请求状态
    //请求方法
    METHOD m_method;// HTTP请求方法
    //以下为解析请求报文中对应的6个变量
     //存储读取文件的名称
    //客户请求的目标文件的完整路径，其内容等于doc_root+m_url,doc_root是网站根目录*/
    char m_real_file[FILENAME_LEN]; // 请求的文件路径
    //客户请求的目标文件的文件名
    char *m_url;// 请求的URL
    //http协议版本。只支持http/1.1
    char *m_version; // HTTP协议版本
    //主机名
    char *m_host; // 主机名
    //http请求的消息体的长度
    int m_content_length; // 请求内容长度
  
    bool m_linger;// /请求是否为长连接
    
    //客户请求的目标文件被mmap到内存中的起始位置
    char *m_file_address; // 文件映射地址/读取服务器上的文件地址
    //目标文件的状态，通过它可以判断文件是否存在，是否为目录，是否可读，并获取文件大小等信息
    struct stat m_file_stat;// 文件状态信息

    //采用writev执行写操作，
    struct iovec m_iv[2];// iovec结构体数组，表示一块内存
    int m_iv_count;// 表示iovcec结构体数组的数量
    int cgi;        //是否启用的POST
    char *m_string; //存储请求头数据
    int bytes_to_send; // 剩余发送字节数
    int bytes_have_send;// 已发送的字节数

  
};

#endif
