#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./lock/locker.h"
#include "./threadpool/threadpool.h"
#include "./timer/lst_timer.h"
#include "./http/http_conn.h"
#include "./log/log.h"
#include "./CGImysql/sql_connection_pool.h"

#define MAX_FD 65536           //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //最大事件数
#define TIMESLOT 5             //最小超时单位

#define SYNLOG  //同步写日志
//#define ASYNLOG /异步日志

//#define listenfdET 
#define listenfdLT //电平触发阻塞

//这三个函数在http_conn.cpp中定义，改变链接属性
extern int addfd(int epollfd, int fd, bool one_shot);
extern int remove(int epollfd, int fd);
extern int setnonblocking(int fd);

//用于处理信号的管道，以实现统一事件源，后面称之为管道，实际上存放的是一对socket
static int pipefd[2];
//创建定时器的容器链表  它是一个升序、双向链表、且带有头结点和尾结点
static sort_timer_lst timer_lst;
//用于唯一标识内核事件表的文件描述符
static int epollfd = 0;

//信号处理函数
void sig_handler(int sig)
{    //如果一个函数能被多个线程同时调用且不发生竟态条件，则称之为可重入函数
    //为保证函数的可重入性，保留原来的errno，errno用于保存函数调用产生的错误代码
    //可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;
    //TCP数据读写函数中的send函数是往sockfd上写入数据，这里是往pipefd[1]的socket写入数据
    //第一个参数一般是sockfd，第二个和第三个参数一般是写缓冲区的位置和大小，最后一个参数是flag参数在这里没有使用
    //将信号值从管道写端写入，传输字符类型，而非整型
    send(pipefd[1], (char *)&msg, 1, 0);
    //将原来的errno赋值为当前的errno
    errno = save_errno;//保留原来的errno
}

//设置信号函数
//函数的作用是在程序中注册一个信号处理函数，用于处理特定信号的发生。
//第一个参数要处理的信号，第二个参数具体处理信号的函数指针的声明，void表明这个函数不返回值，handler是函数名称，(int)是这个函数要传入的参数类型也就是信号的类型，
// 第三个参数表示在信号处理函数执行时自动重启被中断的系统调用。
void addsig(int sig, void(handler)(int), bool restart = true)
{   
    //创建sigaction结构体变量
    struct sigaction sa;
    // 清零并初始化 sa 结构体  这行代码的作用是将结构体 sa 的所有字节都设置为零，起到初始化的作用
    memset(&sa, '\0', sizeof(sa));
    
    //信号处理函数中仅仅发送信号值，不做对应逻辑处理
    //sa_handler属性用来指定信号处理函数
    sa.sa_handler = handler;
    // 如果 restart 参数为 true，则设置 SA_RESTART 标志
    if (restart)
        sa.sa_flags |= SA_RESTART;//SA_RESTART重新调用被该信号终止的系统调用，这通常用于防止由于信号中断导致的系统调用失败
    //在信号集中设置所有信号  sa_mask是sigset_t类型，该类型指定一组信号
    sigfillset(&sa.sa_mask);//在信号集中设置所有信号
    // 使用 sigaction 函数注册信号处理函数
    //执行sigaction函数
    //sig参数指出要捕获的信号类型 sa指定新的信号处理方式  第三个参数如果不为null的话则表示先前的信号处理方式
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void timer_handler()
{
    //定时处理任务，实际上就是调用tick函数，这个函数用来处理非活动连接
    timer_lst.tick();
    /* 重新定时以不断触发SIGALRM信号 */
    alarm(TIMESLOT);
}

//定时器回调函数，删除非活动连接在socket上的注册事件，并关闭
//关闭该socket连接，并删除其对应的定时器del_timer。

//定时器回调函数，它剔除非活动连接socket上的注册事件，并关闭之
void cb_func(client_data *user_data)
{  
    //第一个参数是要操作的内核事件表，第二个参数是操作类型，这里表示删除事件表上的注册事件，第三个参数表示事件
    //这个函数删除在epollfd所标识的内核事件表上的非活动socket连接
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    //关闭文件描述符
    close(user_data->sockfd);
    //减少连接数
    http_conn::m_user_count--;
    //写入日志
    LOG_INFO("close fd %d", user_data->sockfd);
    //刷新
    Log::get_instance()->flush();
}

void show_error(int connfd, const char *info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char *argv[])
{
#ifdef ASYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 8); //异步日志模型
#endif

#ifdef SYNLOG  //get_instance单例模式
    Log::get_instance()->init("ServerLog", 2000, 800000, 0); //创建同步日志文件并初始化
#endif

    if (argc <= 1)//如果参数量小于等于一个那么会显示下面的代码
    {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        //basename 函数在标准 C 库中，你需要包含头文件 <libgen.h> 才能使用。，用来获取路径信息
        return 1;
    }
  
    int port = atoi(argv[1]);//将端口号从字符串型转变为整型,获取服务器要打开的端口号

   
    //SIGPIPE往读端被关闭的管道或者socket连接中写数据  SIG_IGN表示忽略所接受的信号
    //在接收到 SIGPIPE 信号时，程序的默认行为是结束进程，但我们不希望因为错误的写操作而导致程序退出，所以第二个参数设置的信号处理函数是要忽略此信号
    addsig(SIGPIPE, SIG_IGN);

    //创建一个指向数据库连接池对象的指针connPool
    connection_pool *connPool = connection_pool::GetInstance();//使用静态局部变量懒汉模式(第一次被使用时才进行初始化)实现单例模式
    //初始化连接池的各个属性，创建数据库连接池，一共有8条数据库连接，并且利用互斥锁防止多线程竞争
    connPool->init("localhost", "root", "4050km", "webserver", 3306, 8);

    //创建一个指向线程池模板类的指针pool，线程池模板类的数据类型为http_conn：http连接处理类
    threadpool<http_conn> *pool = NULL;
    try
    {
        //在堆上动态分配内存空间以创建一个 threadpool<http_conn> 对象，并将其地址赋给 pool 指针。构造函数的参数 connPool 是连接池对象，可能是在线程池中用于管理连接的资源
        //调用线程池构造函数，此函数应接受三个参数，第一个参数为指向连接池对象的指针，第二个参数为thread_number是线程池中线程的数量，默认设置为8，
        // 第三个参数是max_requests是请求队列中最多允许的、等待处理的请求的数量，默认设置为10000
        // 创建线程池并执行任务，但是一开始并没有任务，所以线程池的8条线程一直在请求队列上检测是否有http请求，这种状态也被形容为工作线程睡眠在请求队列上
        pool = new threadpool<http_conn>(connPool);//线程池中的线程可能需要在任务执行过程中使用连接池中的连接，以完成与数据库的交互
    }
    catch (...)//catch (...) 是 C++ 中的异常处理机制的通用异常处理块。在 try 块中发生任何类型的异常时，会被 catch (...) 捕获
    {
        return 1;
    }
    //创建存储MAX_FD个http请求处理对象的数组
    //并利用users指向数组的起始位置
    http_conn *users = new http_conn[MAX_FD];
    assert(users);//assert就是检查传进来的东西是否为非空，非空则正常运行，空则程序终止

    //初始化 MySQL 查询结果的函数，它从服务器的数据库中检索已经存在的用户名（username）和密码（passwd），并将结果存储到 users 这个 std::map user的哈希表中
    users->initmysql_result(connPool);

    //Web服务器端通过socket监听来自用户的请求。
    //1、创建socket
    // Linux中所有东西都是文件，socket也是文件，它是可读，可写，可控制，可关闭的文件描述符
    // 创建socket文件描述符  告诉系统使用PF_INETipv4协议族，第二个是sock流服务，第三个是在前两个的条件下再选择一个具体协议，几乎全部选0，此函数成功返回一个socket文件描述符
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);//listenfd为-1则失败，>=0则成功
    assert(listenfd >= 0);

    //struct linger tmp={1,0};
    //SO_LINGER若有数据待发送，延迟关闭
    //setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    int ret = 0;//用来看绑定是否成功的变量

    //2、创建sockaddr_in ipv4专用socket地址
    struct sockaddr_in address;//用于表示 IPv4 地址的结构
    bzero(&address, sizeof(address));//用于将地址结构清零，以确保结构中的所有字段都被初始化。
    //指定socket指定的地址族位ipv4
    address.sin_family = AF_INET;//设置地址族为 IPv4。
    //指定套接字（socket）的 IP 地址  INADDR_ANY 表示将套接字绑定到任意可用的本地 IP 地址。
    address.sin_addr.s_addr = htonl(INADDR_ANY);//htonl 是将主机字节序转换为网络字节序长整型long的函数。
    //指定socket的开放端口
    address.sin_port = htons(port);//htons是将主机字节序转换为网络字节序的函数转换为短整型short型16位，长整型，port 是你指定的端口号。

    int flag = 1;
    //3、setsockopt 是一个专门用于设置socket文件描述符的系统调用
    ///* SO_REUSEADDR 允许端口被重复使用 */
    //参数列表为文件描述符，通用socket选项，SO_REUSEADDR 允许端口被重复使用，
    //flag表示设置 SO_REUSEADDR 选项的值为1，表示启用SO_REUSEADDR
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    //将一个socket与socket地址绑定称为给socket命名，只有命名完之后，客户端才知道如何连接它
    //bind 函数的调用将指定的套接字与指定的 IP 地址和端口号进行关联，使得套接字监听指定地址。成功返回零
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    //4、监听socket
    //创建一个监听队列，存放待处理的客户连接
    //参数列表：指定listenfd是被监听的socket，5是提示内核监听队列的最大长度
    ret = listen(listenfd, 5);//listen 函数是一个系统调用
    assert(ret >= 0);

    //用于存储用户关心的文件描述符上的事件，内核事件表上的事件会存储到这数组
    epoll_event events[MAX_EVENT_NUMBER];
    //* 创建一个额外的文件描述符epollfd来唯一标识内核事件表 */ 5给内核提示需要多大的事件表
    epollfd = epoll_create(5); 
    assert(epollfd != -1);
    //将文件描述符listenfd上的事件注册到epollfd指示的内核事件表中，第三个参数表示是否开启ET（边沿触发）模式,,使得epoll可以监听listenfd发生的可读事件(实际上可能有三种事件)
    addfd(epollfd, listenfd, false);
 
    //将epollfd赋值给http类对象的m_epollfd属性，作用未知
    http_conn::m_epollfd = epollfd;

    //创建一对相互连接的socket
    //使用socketpair创建管道，注册pipefd[0]的可读事件，实际上管道两端都可读可写，只是在一端读的同时再写的话会阻塞，这里只是形象上的管道，实际上就是一个保存两个socket的数组pipefd
    //参数列表：Linux协议族，socket流服务，0 表示自动选择合适的协议，创建的socket对
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    
    assert(ret != -1);
    //设置pipefd[1]非阻塞
    setnonblocking(pipefd[1]);
    //将文件描述符pipefd[0]上的事件注册到epollfd指示的内核事件表中,使得epoll可以监听pipefd[0]发生的可读事件(实际上可能有三种事件)
    addfd(epollfd, pipefd[0], false);
    //如果捕捉到M由alarm或setitimer设置的超时闹钟引起的SIGALRM信号，则调用sig_handler信号处理函数，目前看来就是把信号写入pipefd[1]的socket
    addsig(SIGALRM, sig_handler, false);
    //SIGTERM终止进程，kill命令默认发送的信号就是SIGTERM
    //如果捕捉到SIGTERM信号，则调用sig_handler信号处理函数
    addsig(SIGTERM, sig_handler, false);


    //服务器是否停止的标志
    bool stop_server = false;
    //把users_timer指针指向用户数据数组的起始位置，client_data里的每个元素对应着一个用户的socket地址，socket文件描述符，还有一个定时器
    client_data *users_timer = new client_data[MAX_FD];
    //超时默认为False
    //超时标志
    bool timeout = false;
    //alarm函数每隔TIMESLOT时间就会触发SIGALRM信号，该信号的处理函数，利用管道通知主循环执行定时器链表上的定时任务，关闭非活动的连接
    alarm(TIMESLOT);

    while (!stop_server)//进入主循环
    {   
        //主线程调用epoll_wait在超时时间内等待一组文件描述符上的时间
        //参数列表：标识内核事件表的文件描述符，要存储事件的数组 如果检测到事件就将所有就绪的事件从epollfd标识的内核事件表中复制到events中， MAX_EVENT_NUMBER表示最多监听多少个事件，
        //超时时间 -1表示epoll调用将永远阻塞，知道某个事件发生 此函数成功则返回就绪的文件描述符的个数
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);

        //没有事件或错误时终止程序并写入日志
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        //然后我们遍历events以处理这些已经就绪的事件
        for (int i = 0; i < number; i++)
        {
            //获取就绪的socket文件描述符 fd表示事件所从属的文件描述符，这里的就绪事件从属的都是listenfd文件描述符
            int sockfd = events[i].data.fd;
            //如果就绪事件都从属于socket文件描述符listenfd，那么表示监听到了新的客户连接   处理新到的socket连接
            if (sockfd == listenfd)
            {
                //客户端socket地址初始化，后面用来接受客户端的socket地址
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
 //LT水平触发
#ifdef listenfdLT
                //5、从listen监听队列中接受一个连接
                //参数列表：执行过监听操作的socket，用来获取客户端socket地址，客户端socket地址的长度
                //该函数一个连接socket文件描述符connfd用来标识被接受的这个连接，失败则返回-1
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);
                //接受失败时写入日志
                if (connfd < 0)
                {
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    continue;
                }
                //如果用户数量大于等于所设置的用户数量则显示Internal server busy
                if (http_conn::m_user_count >= MAX_FD)
                {
                    show_error(connfd, "Internal server busy");
                    LOG_ERROR("%s", "Internal server busy");
                    continue;
                }


                //分析事件类型
                //users指的使存放http请求处理类的数组，存放的都是http_conn
                //令数组中索引为connfd的http_conn对象调用init函数，这个函数初始化用户连接的各种属性且在内核事件表上进行注册
                users[connfd].init(connfd, client_address);

                //users_timer数组存放client_data类型的用户数据
                //将user_timer数组上索引为connfd的元素的address和sockfd设置为用户的属性
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;
                //创建临时的定时器timer
                util_timer *timer = new util_timer;
                //设置定时器对应用户数据为上面的users_timer[connfd]的数据
                timer->user_data = &users_timer[connfd];
                //设置timer的回调函数为上面定义的处理非活动socket连接的回调函数
                timer->cb_func = cb_func;
                //获取当前系统事件
                time_t cur = time(NULL);  //NULL则表示不关心具体的返回值，直接返回当前时间
                //设置绝对超时时间
                timer->expire = cur + 3 * TIMESLOT;//15s
                //设置此用户连接的定时器为timer 这个定时器设定超过15s调用回调函数
                users_timer[connfd].timer = timer;
                //将该定时器添加到定时器容器即升序链表中，并根据时间插入到合适的位置
                timer_lst.add_timer(timer);
#endif
     //ET非阻塞边缘触发
#ifdef listenfdET
                //需要循环接收数据
                while (1)
                {
                    int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);
                    if (connfd < 0)
                    {
                        LOG_ERROR("%s:errno is:%d", "accept error", errno);
                        break;
                    }
                    if (http_conn::m_user_count >= MAX_FD)
                    {
                        show_error(connfd, "Internal server busy");
                        LOG_ERROR("%s", "Internal server busy");
                        break;
                    }
                    users[connfd].init(connfd, client_address);

                    //初始化client_data数据
                    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
                    users_timer[connfd].address = client_address;
                    users_timer[connfd].sockfd = connfd;
                    util_timer *timer = new util_timer;
                    timer->user_data = &users_timer[connfd];
                    timer->cb_func = cb_func;
                    time_t cur = time(NULL);
                    timer->expire = cur + 3 * TIMESLOT;
                    users_timer[connfd].timer = timer;
                    timer_lst.add_timer(timer);
                }
                continue;
#endif
            }
            //处理异常事件    通过与操作可以检测events里面是否出现了远端挂起事件，挂起事件，错误事件中的一个或几个，如果出现那么与操作的结果不会为零
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //创建指向util_timer的指针timer接受当前就绪事件的定时器
                
                util_timer *timer = users_timer[sockfd].timer;
                //调用这个定时器的回调函数，这个函数删除在epollfd所标识的内核事件表上的非活动socket连接
                timer->cb_func(&users_timer[sockfd]);

                //如果timer指针不为空，则删除定时器容器链表上的定时器
                if (timer)
                {
                    timer_lst.del_timer(timer);
                }
            }
            //如果fd从属于的文件描述符是pipiefd【0】的socket，且出现可读事件，则处理信号来自客户端的信号
            //管道读端对应文件描述符发生读事件       处理客户连接上接收到的数据
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN))
            //接收到SIGALRM信号，timeout设置为True
            {
                int sig;
                char signals[1024];
                //TCP读操作
                //从管道读端读出信号值，成功返回字节数，失败返回-1
                //正常情况下，这里的ret返回值总是1，只有14和15两个ASCII码对应的字符
                //参数列表：要操作的socket文件描述符，读缓冲区，缓冲区大小，最后一个参数提供额外控制这里没有使用，此函数可能返回0表明对方已经关闭连接，返回-1则说明读取失败
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                //读取失败继续主循环
                if (ret == -1)
                {
                    continue;
                }
                //对方关闭连接，继续主循环
                else if (ret == 0)
                {
                    continue;
                }
                //其他情况
                else
                {
                    ////处理信号值对应的逻辑，遍历读缓冲区的字符
                    for (int i = 0; i < ret; ++i)
                    {

                        switch (signals[i])
                        {
                        //如果信号是SIGALRM，此信号由alarm或者settimer设置的闹钟引起
                        case SIGALRM:
                        {
                            //将超时标记置为true，然后跳出这个遍历信号的循环
                            timeout = true;
                            break;
                        }
                        //如果信号是SIGTERM，此信号只有终止进程时才会出现
                        case SIGTERM:
                        {
                            //将终止服务器运行标志置为true，表示要终止服务器运行
                            stop_server = true;
                        }
                        }
                    }
                }
            }
            //如果就绪的socket文件描述符出现可读事件
            else if (events[i].events & EPOLLIN)
            {
                //创建timer指针指向此用户socket连接的定时器timer
                util_timer *timer = users_timer[sockfd].timer;

                //根据读的结果，决定是将任务添加到线程池，还是关闭连接
                if (users[sockfd].read_once())//读取成功返回true
                {
                    //写入日志
                    LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();
                    
                  
                    // users是users数组的起始指针 sockfd表示这个指针要加几位，从而使users指针指向一个特定的http请求处理类
                    //将这个http请求处理类添加到线程池，并提醒线程有http请求需要处理，此时线程就会开始处理工作队列中的http请求
                    pool->append(users + sockfd);

                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
                    //如果某个客户连接上有数据可读，我们要调整该连接对应的定时器，延迟该连接被关闭的时间
                    //增加该连接的定时器时间，调整此用户的定时器在定时器容器链表中的位置
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                }
                else//否则关闭连接
                {
                    //调用定时器回调函数，关闭socket连接
                    timer->cb_func(&users_timer[sockfd]);
                    //删除链表中的定时器
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
            //如果就绪的socket文件描述符出现可写事件
            else if (events[i].events & EPOLLOUT)
            {
                util_timer *timer = users_timer[sockfd].timer;
                //根据写的结果，决定是否关闭连接
                ///* 当这一sockfd上有可写事件时，epoll_wait通知主线程。主线程往socket上写入服务器处理客户请求的结果 */
                if (users[sockfd].write())//成功返回true
                {
                    LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();

                    //如果某个客户连接上有数据可写，我们要调整该连接对应的定时器，延迟该连接被关闭的时间
                    //增加该连接的定时器时间，调整此用户的定时器在定时器容器链表中的位置
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                }
                else//否则关闭socket连接并删除定时器
                {
                    
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
        }
        //处理定时器为非必须事件，收到信号并不是立马处理
        //完成读写事件后，再进行处理

        //最后处理定时事件，因为IO事件优先级更高，当然，这样做会导致定时任务不能精确地按照预定时间执行
        if (timeout)
        {
            timer_handler();
            timeout = false;
        }
    }
    close(epollfd);
    //7、关闭socket连接
    close(listenfd);//关闭socket连接
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete pool;
    return 0;
}
