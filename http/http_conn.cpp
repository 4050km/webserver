#include "http_conn.h"
#include "../log/log.h"
#include <map>
#include <mysql/mysql.h>
#include <fstream>

//#define connfdET 
#define connfdLT //边沿触发宏。

//#define listenfdET //
#define listenfdLT //边沿触发宏

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
//网站根目录，文件夹内存放请求的资源和跳转的html文件
const char *doc_root = "/home/admin4050/TinyWebServer-raw_version/root";


map<string, string> users;
locker m_lock;
//初始化 MySQL 查询结果的函数，它从服务器的数据库中检索已经存在的用户名（username）和密码（passwd），并将结果存储到 users 这个 std::map user的哈希表中
void http_conn::initmysql_result(connection_pool *connPool)
{
    //先从数据库连接池中取一个数据库连接对象，获取一条数据库连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))//第一个参数，mysql连接对象，第二个参数mysql语句 成功返回0，0是false就不会执行日志记录
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集  用于处理 SELECT 查询的结果，该函数将查询结果集存储在一个 MYSQL_RES 结构中，并返回一个指向结果集的指针。
    MYSQL_RES *result = mysql_store_result(mysql);

    //用于获取结果集中字段的数量的函数。它返回一个整数
    int num_fields = mysql_num_fields(result);//num_fields 就是查询结果中的字段数量

    //用于获取结果集中字段的详细信息的函数。它返回一个指向 MYSQL_FIELD 结构数组的指针
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    //用于逐行获取查询结果集的函数。它返回一个指向当前行数据的指针，并将结果集的指针移动到下一行。
    while (MYSQL_ROW row = mysql_fetch_row(result))//mysql_fetch_row(result) 用于获取查询结果中的一行数据，返回一个指向数据行的指针（MYSQL_ROW）
    {
        //用户名临时变量
        string temp1(row[0]);
        //密码临时变量
        string temp2(row[1]);
        //user是map表，这里是将用户名和密码对应起来保存
        users[temp1] = temp2;
    }
}

//对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    //fcntl提供对文件描述符的各种控制操作
    //参数列表：要操作的文件描述符，指定操作类型，这里是获取和设置文件描述符的状态标志
    int old_option = fcntl(fd, F_GETFL);//p112
    // 设置非阻塞标志new_option//通过执行按位或运算，将 O_NONBLOCK 标志添加到 old_option 中，生成了新的文件描述符属性 new_option。这样做的目的是为了在设置文件描述符的属性时启用非阻塞模式。
    int new_option = old_option | O_NONBLOCK;
    // 将当前文件描述符的状态标志设置为非阻塞标志new_option
    fcntl(fd, F_SETFL, new_option);
    // 返回旧的文件描述符状态。以便日后恢复
    return old_option;
}

//将socket文件描述符注册到epollfd也就是内核事件表中的函数
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;//这个数据类型指定epoll事件类型和用户数据
    event.data.fd = fd;//指定事件属于什么文件描述符，这里表示事件输入listenfd文件描述符也就是要监听的socket
//那个宏定义了，就执行那个代码，选择epoll对文件描述符的操作方式：水平触发或边沿触发
#ifdef connfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef connfdLT//定义了这个宏 采用电平触发方式
    event.events = EPOLLIN | EPOLLRDHUP;///指定了事件类型。可读事件 (EPOLLIN) 和tcp连接被客户端关闭或者对方关闭了写操作  也可以称为远端挂起(EPOLLRDHUP) 事件的关注。
#endif

#ifdef listenfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef listenfdLT//定义了这个宏 采用电平触发方式，第一次调用这个函数是执行这里对socket文件描述符listenfd操作，这里指定了事件类型也是可读事件 (EPOLLIN)和远端挂起(EPOLLRDHUP) 事件
    event.events = EPOLLIN | EPOLLRDHUP;
#endif
    //开启EPOLLONESHOT，因为我们希望每个socket在任意时刻都只被一个线程处理 */
    if (one_shot)//第一次调用是false，false不启用
        event.events |= EPOLLONESHOT;//epolloneshot操作系统最多触发其上注册的一个可读，可写或者异常事件，且只触发一次
    //epoll_ctl用来操作内核事件表
    ///参数列表：要操作的文件描述符也就是唯一标识内核事件表的那个文件描述符，指定操作类型这里是往事件表中注册fd上的事件，fd就是socket的文件描述符，最后一个参数指定上面所选择的事件
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);//将文件描述符注册到内核事件表中，使得 epoll 可以监听该文件描述符上发生的特定事件。一旦文件描述符上的事件发生，将触发相应的操作。？？？
    setnonblocking(fd);//对socket设置非阻塞
}

//内核事件表删除事件
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//重置EPOLLONESHOT事件
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;

#ifdef connfdET
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
#endif

#ifdef connfdLT//添加oneshot事件
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
#endif
    // EPOLL_CTL_MOD修改fd上的注册事件，
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}
//最大用户数量
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;//关闭一个连接时，将客户总量减1
    }
}

//初始化用户连接，将socket连接在内核事件表上注册
void http_conn::init(int sockfd, const sockaddr_in &addr)
{
    m_sockfd = sockfd;
    m_address = addr;
    //注释的两行为了避免time_wait状态，仅用于调试
    //int reuse=1;
    //setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    //将socket在epollfd即内核事件表上注册，true表示开启epooloneshot事件，只能进行一次读写事件，且只能触发一次
    addfd(m_epollfd, sockfd, true);
    //用户数量+1
    m_user_count++;
    init();
}

//初始化新接受的socket连接
//初始化用户的各种属性
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    //设置主状态机的初始状态
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;//行在buffer中的起始位置
    m_checked_idx = 0;//当前已经分析完了多少字节的客户数据
    m_read_idx = 0;//当前已经读取了多少字节的客户数据
    m_write_idx = 0;
    cgi = 0;
    //用于设置一块内存区域的内容为特定的值
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}


//从状态机，用于分析出一行内容
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN

//m_read_idx指向缓冲区m_read_buf的数据末尾的下一个字节
//m_checked_idx指向从状态机当前正在分析的字节
// 
//从状态机从m_read_buf中逐字节读取，判断当前字节是否为\r或\n
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)//m_read_idx在read_once函数中定义
    {
        //m_read_buf是服务器的读缓冲区，m_checked_idx指向当前要分析的字节
        //temp为将要分析的字节
        temp = m_read_buf[m_checked_idx];
        //如果当前字节是\r即回车符，则说明可能读取到一个完整的行         //如果当前是\r字符，则有可能会读取到完整行
        if (temp == '\r')
        {
            //如果\r是目前buffer到的最后一个已经被读入的客户数据，那么这次分析没有读取到一个完整的行，返回lineopen表示还需要继续读取才能进一步分析 
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            //如果下一个字符是\n那么表明成功读取到了一个完整的行 并将\r\n改为\0\0       因为http请求行的末尾是\r\n
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            //否则存在http请求存在语法问题
            return LINE_BAD;
        }
        //如果当前字符是\n，即换行符，则说明有可能读取到一个完整的行。
        //如果当前字符是\n，也有可能读取到完整行
        //一般是上次读取到\r就到buffer末尾了，没有接收完整，再次接收时会出现这种情况
        else if (temp == '\n')
        {
            //如果前一个字符是\n那么表明成功读取到了一个完整的行 并将\r\n改为\0\0  
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            //并没有找到\r\n，需要继续接收
            return LINE_BAD;
        }
    }
    //如果分析完所有也没有遇到\r或者\n字符，则返回lineopen表示还需要更多数据才能分析
    return LINE_OPEN;
}

//循环读取客户数据，直到无数据可读或对方关闭连接
bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;//目前要读取的字节

#ifdef connfdLT
    //从socket接收数据，存储在m_read_buf缓冲区
    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
    //修改m_read_idx的读取字节数
    
    += bytes_read;

    if (bytes_read <= 0)
    {
        return false;
    }

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
{
    
    //在HTTP报文中，请求行用来说明请求类型,要访问的资源以及所使用的HTTP版本，其中各个部分之间通过\t或空格分隔。
    //请求行中最先含有空格和\t任一字符的位置并返回
    //strpbrk函数在text指针所指的字符串中查找第一个匹配给定字符集合中任一字符的字符 也就是在text查找空格或者\t即水平制表符
    m_url = strpbrk(text, " \t");
    //如果没有空格或\t，则报文格式有误
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    //将该位置改为\0，用于将前面数据取出
    *m_url++ = '\0';
    //取出数据，并通过与GET和POST比较，以确定请求方式
    char *method = text;
    //strcasecmp字符串比较函数，用于比较两个字符串是否相等，相等返回0
    //如果是get请求，将模式设置为GET
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    //如果是get请求，将模式设置为POST
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;//启用CGI
    }
    else
        return BAD_REQUEST;

    //m_url此时跳过了第一个空格或\t字符，但不知道之后是否还有
    //将m_url向后偏移，通过查找，继续跳过空格和\t字符，指向请求资源的第一个字符
    //strspn用于计算字符串中连续包含在指定字符集合中的字符的长度  +=用于跳到下一个位置
    m_url += strspn(m_url, " \t");
    //使用与判断请求方式的相同逻辑，判断HTTP版本号
    m_version = strpbrk(m_url, " \t");
    //m_version为空指针，那么请求有误
    if (!m_version)
        return BAD_REQUEST;
    //将该位置改为\0，用于将前面数据取出
    *m_version++ = '\0';
    //m_version跳到下一个位置，用来判断http协议
    m_version += strspn(m_version, " \t");
    //仅支持HTTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    //对请求资源前7个字符进行判断
    //这里主要是有些报文的请求资源中会带有http://，这里需要对这种情况进行单独处理
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        //用于在字符串中查找指定字符的第一次出现的位置
        m_url = strchr(m_url, '/');
    }
    //检查url是否合法
    //同样增加https情况
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    //一般的不会带有上述两种符号，直接是单独的/或/后面带访问资源
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
 
    //当url为/时，显示欢迎界面
    if (strlen(m_url) == 1)
        //用于将一个字符串追加到另一个字符串的末尾，这里是将judge.html追加到前面m_url的末尾
        strcat(m_url, "judge.html");
   
    //请求行处理完毕，将主状态机转移处理请求头
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}
//解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
  
    //text[0] == '\0'说明是空行，如果不是则下面的else if来处理请求头
    if (text[0] == '\0')
    {
        
        //若是空行还有content-length说明是post请求
        if (m_content_length != 0)
        {
            //POST需要跳转到消息体处理状态
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        //否则说明我们已经得到了完整的http的get请求
        return GET_REQUEST;
    }
    //strncasecmp(text, "Connection:", 11)比较text的前11个字符是不是Connection:
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        //跳过空格和\t字符
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            //如果是长连接，则将linger标志设置为true
            m_linger = true;
        }
    }
    
    //解析请求头部内容长度字段
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        //用于将字符串转换为长整型
        m_content_length = atol(text);
    }
    //解析请求头部HOST字段
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    //对未知信息的处理，写入日志
    else
    {
        //printf("oop!unknow header: %s\n",text);
        LOG_INFO("oop!unknow header: %s", text);
        Log::get_instance()->flush();
    }
    return NO_REQUEST;
}

//解析消息体
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    //判断buffer中是否读取了消息体
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        //POST请求中最后为输入的用户名和密码
        //将text即消息体放入m_string
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}
//处理http请求
//process_read通过while循环，将主从状态机进行封装，对报文的每一行进行循环处理
http_conn::HTTP_CODE http_conn::process_read()
{
    ////初始化从状态机状态、HTTP请求解析结果
    //初始化行状态为成功解析一行
    LINE_STATUS line_status = LINE_OK;
    //初始化http请求状态为没有请求
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    //m_check_state是主状态机的状态，line_status是从状态机的状态
    //(line_status = parse_line()) == LINE_OK表示请求行解析成功
    //(m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK)表示请求报文解析完成，但是此时主状态机的状态还是解析消息体状态，无法跳出循环
    // 为了跳出循环，在解析消息体的函数中将line_status变量更改为LINE_OPEN，这样在解析完成后由于LINE_OPEN不等于LINE_OK就能跳出while循环了，从而实现请求报文的解析
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        //调用get_line函数，通过m_start_line将从状态机读取数据间接赋给text
        text = get_line();
        //m_start_line是每一个数据行在m_read_buf中的起始位置
        //m_checked_idx表示从状态机在m_read_buf中读取的位置  m_checked_idx指向下一行的开头
        m_start_line = m_checked_idx;
        //日志操作
        LOG_INFO("%s", text);
        Log::get_instance()->flush();
        //m_check_state记录当前主状态机的状态
        ////主状态机的三种状态转移逻辑
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            //解析请求行
            ret = parse_request_line(text);//第一个状态，分析请求行
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER://第二个状态，分析头部字段
        {
            //解析请求头
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            //完整解析GET请求后，跳转到报文响应函数
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT://第三个状态，分析请求的正文部分
        {
            //解析消息体
            ret = parse_content(text);
            //完整解析POST请求后，跳转到报文响应函数
            if (ret == GET_REQUEST)
                return do_request();
            //解析完消息体后，报文的完整解析就完成了，但此时主状态机的状态还是CHECK_STATE_CONTENT，也就是说，符合循环入口条件，还会再次进入循环，这并不是我们所希望的。
            //为此，增加了该语句，并在完成消息体解析后，将line_status变量更改为LINE_OPEN，此时可以跳出循环，完成报文解析任务。
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}
//当得到一个完整的，正确的http请求时，我们分析目标文件的属性。如果目标文件存在，对所有用户可读，且不是目录，这而是用mmap将其映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request()
{
    //将初始化的m_real_file赋值为网站根目录
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    //printf("m_url:%s\n", m_url);
    
    //找到m_url中/的位置    此时m_url为解析请求行里面的状态
    const char *p = strrchr(m_url, '/');

    ////CGI多进程登录校验
    //判断是登录检测还是注册检测  2是登录检测，3是注册检测
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {

        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        //释放动态内存
        free(m_url_real);

        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        //以&为分隔符，前面的为用户名
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';
        //以&为分隔符，后面的是密码
        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        //CGI同步线程登录和注册
        //如果是注册检测
        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            //构建mysql插入语句
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            //判断map中能否找到重复的用户名，如果迭代器指向最后users.end()，说明没有找到重复的，进行用户注册
            if (users.find(name) == users.end())
            {
                //向数据库中插入数据时，需要通过锁来同步数据
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();
                //插入成功，跳转登录页面
                if (!res)
                    strcpy(m_url, "/log.html");
                //插入失败，跳转注册失败页面
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        //如果是登录检测，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            //如果找到了用户，而且密码正确     users在init_mysqlresult函数中已经放了服务器已有的账户和密码
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    //如果请求资源为/0，表示跳转注册界面
    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        //将网站目录和/register.html进行拼接，更新到m_real_file中
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //如果请求资源为/1，表示跳转登录界面
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        //将网站目录和/log.html进行拼接，更新到m_real_file中
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //图片页面
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //视频页面
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //关注页面
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
        //否则发送url实际请求的文件
        //如果以上均不符合，即不是登录和注册，直接将url与网站目录拼接
        //这里的情况是welcome界面，请求服务器上的一个图片
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    //通过stat获取请求资源文件信息，成功则将信息更新到m_file_stat结构体
     //失败返回NO_RESOURCE状态，表示资源不存在
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    //判断文件的权限，是否可读，不可读则返回FORBIDDEN_REQUEST状态
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    //判断文件类型，如果是目录，则返回BAD_REQUEST，表示请求报文有误
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    //以只读方式获取文件描述符，通过mmap将该文件映射到内存中
    int fd = open(m_real_file, O_RDONLY);
    //mmap函数用于申请一段内存空间，我们可以将这段内存作为进程间通信的共享内存，也可以将文件映射到里面
    //p107
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    //避免文件描述符的浪费和占用
    close(fd);
    //表示请求文件存在，且可以访问
    return FILE_REQUEST;
}
//对内存映射区执行munmap操作
void http_conn::unmap()
{
    if (m_file_address)
    {
        //第一个参数是映射区的起始地址，第二个参数是映射区的长度
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
//写http响应
bool http_conn::write()
{
    int temp = 0;

    //若要发送的数据长度为0
    //表示响应报文为空，一般不会出现这种情况
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        //将多块分散的m_iv上的内存数据一并写入当前socket连接
        //第一个参数为操作的文献描述符，第二个为iovec结构数组，该结构数组描述一块内存区，miv表示有多少块这样的内存
        //将响应报文的状态行、消息头、空行和响应正文发送给浏览器端
        //成功时返回写入的字节数，失败返回-1
        temp = writev(m_sockfd, m_iv, m_iv_count);
        //说明出现错误
        if (temp < 0)
        {
            //如果tcp写缓冲没有空间，则等待下一轮epollout事件，虽然在此期间，服务器无法立即接收到同一客户的下一个请求，但这可以保持连接完整性
            if (errno == EAGAIN)//表示操作暂时不能完成，但之后可能会成功
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            //将映射区取消
            unmap();
            return false;
        }
        //正常的话执行下面代码
        //更新已经发送的字节数
        bytes_have_send += temp;
        //更新准备发送的字节数
        bytes_to_send -= temp;
        //如果已经发送的字节数超过了第一块iov结构体的长度
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            //重置为零
            m_iv[0].iov_len = 0;
            //第二块iov结构体的起始地址为m_file_address + (bytes_have_send - m_write_idx)
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            //第二块iov结构的长度为待发送的字节数
            m_iv[1].iov_len = bytes_to_send;
        }
        else///如果已经发送的字节数没超过了第一块iov结构体的长度
        {
            //第一块iov结构体的起始位置更新
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            //第一块结构体的长度更新
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }
        //数据已全部发送完
        if (bytes_to_send <= 0)
        {
            //解除对内存映射区的映射关系
            unmap();
            //在epoll树上重置EPOLLONESHOT事件
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            //如果浏览器的请求为长连接
            if (m_linger)
            {
                //重新初始化HTTP对象
                init();
                return true;
            }
            else//否则返回false
            {
                return false;
            }
        }
    }
}
//往写缓冲区中写入待发送的数据
bool http_conn::add_response(const char *format, ...)
{
    //如果写入内容超出m_write_buf大小则报错
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    //定义可变参数列表
    va_list arg_list;
    //将变量arg_list初始化为传入参数
    va_start(arg_list, format);
    //将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
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
    LOG_INFO("request:%s", m_write_buf);
    Log::get_instance()->flush();
    return true;
}
//添加状态行
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
//添加消息报头，具体的添加文本长度、连接状态和空行
bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}
//添加Content-Length，表示响应报文的长度
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
//添加文本类型，这里是html
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
//添加连接状态，通知浏览器端是保持连接还是关闭
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
//添加空行
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
//添加文本content
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

//根据服务器处理http请求的结果，决定返回客户端的内容
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
        //内部错误，500
    case INTERNAL_ERROR:
    {
        //状态行
        add_status_line(500, error_500_title);
        //消息报头
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    //报文语法有误，404
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    //资源没有访问权限，403
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    //文件存在，200
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        //如果请求的资源存在
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            //第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            //第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            //发送的全部数据为响应报文头部信息和文件大小
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            //如果请求的资源大小为0，则返回空白html文件
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }
    //除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

//由线程池中的工作线程调用，这是处理http请求的入口函数
void http_conn::process()
{
    //处理http请求
    HTTP_CODE read_ret = process_read();
    //NO_REQUEST，表示请求不完整，需要继续接收请求数据  
    if (read_ret == NO_REQUEST)
    {
        
        ////重置EPOLLONESHOT事件，并注册读事件，让主线程读
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    //服务器子线程调用process_write完成响应报文，随后注册epollout事件。服务器主线程检测写事件，并调用http_conn::write函数将响应报文发送给浏览器端。
    //调用process_write完成报文响应  根据do_request的返回状态，服务器子线程调用process_write向m_write_buf中写入响应报文。
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    ////重置EPOLLONESHOT事件，并注册写事件，让主线程写
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}
