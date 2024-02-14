#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
//同步I/O模拟proactor模式
//半同步 / 半反应堆
// 线程池
template <typename T>
//线程池类
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    //创建线程池并执行任务
    threadpool(connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    //销毁线程池
    ~threadpool();
    //往请求队列里添加任务
    bool append(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);//设置为静态成员函数，如果是类成员函数不是静态成员函数，会自带一个this指针在函数中，这样pthread_create(m_threads + i, NULL, worker, this)调用的时候
    //会有两个this指针传入worker(一个来自类成员函数自带，一个来自本函数)，就会报错，而静态成员函数因为不为对象所维护，没有this指针，则可以避免这个问题。
    //静态成员函数没有this指针。非静态数据成员为对象单独维护，但静态成员函数为共享函数，无法区分是哪个对象，因此不能直接访问普通变量成员，也没有this指针。
    //启动线程池
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //表示是否有任务需要处理的信号量m_queuestat，默认信号值为0
    bool m_stop;                //是否结束线程
    connection_pool *m_connPool;  //数据库连接池
};
template <typename T>
//构造函数接受三个参数：连接池指针 connPool、线程数量 thread_number 和最大请求数量 max_requests。
//使用初始化列表初始化成员变量 m_thread_number、m_max_requests、m_stop、m_threads 和 m_connPool。
//分配一个动态数组 m_threads 来存储线程标识符，如果分配失败则抛出异常。
//循环创建线程，每个线程调用 worker 函数，并将当前对象的指针传递给线程。如果创建线程或设置线程分离状态失败，则进行相应的资源清理和异常抛出。
threadpool<T>::threadpool( connection_pool *connPool, int thread_number, int max_requests) : m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)//如果线程数量或者最大请求数量小于等于零，则抛出异常
        throw std::exception();

    m_threads = new pthread_t[m_thread_number];//创建线程池数组，这个线程池其实就是一个数组，数组的元素类型是 pthread_t，m_threads是 指向 存储pthread_t 类型的数组起始位置的指针。
    if (!m_threads)//如果为null则分配失败抛出异常
        throw std::exception();
    for (int i = 0; i < thread_number; ++i)//创建指定数量的线程，并将工作线程按照要求运行
    {
        //printf("create the %dth thread\n",i);
        //创建线程
        //第一个参数指定新线程的标识符，第二个参数表示线程属性，null表示使用默认线程属性，第三个参数指定线程即将运行的函数，第四个参数则是要运行函数的参数
        //成功返回零，不成功返回错误码
        //创建完线程和线程要执行任务并无关系，只要创建成功，代码就会继续执行，而创建好的线程会自己执行worker函数，但这就与下面这行代码无关了。
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)//当为零时线程创建成功，否则抛出下面的异常，this为线程池对象，将其传入worker
        {
            delete[] m_threads;//
            throw std::exception();
        }
        if (pthread_detach(m_threads[i]))//pthread_detach(m_threads[i])：这行代码将第 i 个线程标识符所代表的线程设置为分离状态。
        //分离状态的线程在结束自己的任务时会自动释放资源，而不需要其他线程调用 pthread_join 来等待它的结束。如果设置成功，pthread_detach 返回0，条件不成立。
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}
template <typename T>
threadpool<T>::~threadpool()//线程池模板类 threadpool 的析构函数的实现
{
    delete[] m_threads;
    m_stop = true;
}
template <typename T>
bool threadpool<T>::append(T *request)
//向线程池的请求队列中添加任务。在添加任务之前，使用互斥锁保证对工作队列的访问是线程安全的。如果工作队列已满，拒绝新的任务；否则，成功添加任务到工作队列。
{
    //操作工作队列时一定要加锁，因为它被所有线程共享
    m_queuelocker.lock();//加锁
    if (m_workqueue.size() > m_max_requests)//检查请求队列是否已经达到或超过了线程池的最大请求数量 (m_max_requests)。如果是，表示工作队列已满，这时应该拒绝新的任务。
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);//请求队列新放一个新任务
    m_queuelocker.unlock();//解锁
    //信号量提醒线程有任务要处理
    m_queuestat.post();//信号加1
    return true;
}
template <typename T>
//void * 被称为「无类型指针」，它是一种通用指针类型，因为它可以容纳任何类型的指针，包括this指针，这里this指针指向线程池对象
void *threadpool<T>::worker(void *arg)//此线程的任务就是使用run函数
{
    //用一个指向线程池对象的指针pool接受传入的this线程池对象指针，
    threadpool *pool = (threadpool *)arg;//将参数转换为线程池对象指针
    pool->run();//调用线程池的 run 函数处理任务
    return pool;// 返回线程池对象指针

}
template <typename T>
void threadpool<T>::run()//这就是一条线程的工作
{
    while (!m_stop)//m_stop默认为flase
    {
        //m_queuestat是表示是否有任务需要处理的信号量，这个信号默认值为0，故一开始应该是阻塞的
        //wait以原子操作将信号量的值减1，如果为零则阻塞。// 等待信号量，当信号量大于零时才继续执行，
        m_queuestat.wait();
        //m_queuelocker是一个互斥锁，用来保护对工作队列的访问
        m_queuelocker.lock();// 加锁，保护对工作队列的访问
        //m_workqueue是一个请求队列，list<T *>，实际上就是一个存放T *类型数据的双向链表，这里存放的是http_conn即http请求处理类的指针
        if (m_workqueue.empty())//如果m_workqueue没有http请求处理类的指针      代码一开始就不会执行到后面了，会在112行-126行不停循环，不断地检测工作队列中是否有请求，有请求程序才会继续执行
        {
            //解锁
            m_queuelocker.unlock();
            //继续循环
            continue;
        }
        //获取工作队列的第一个http请求处理类的指针
        T *request = m_workqueue.front();
        m_workqueue.pop_front();// 移除工作队列的第一个http请求处理类的指针
        m_queuelocker.unlock();// 解锁，允许其他线程访问工作队列
        if (!request// 如果请求为空，继续下一次循环
            continue;
        //从连接池中取出一个数据库连接
        //调用connectionRAII的构造函数此函数接受一个指向指针的指针和一个连接池的指针作为参数&request->mysql实际的意思是&(request->mysql),
        //而request->mysql(而mysql指向MYSQL连接对象)是指针,外面再取地址所以是双重指针，符合函数connectionRAII构造函数的参数
        connectionRAII mysqlcon(&request->mysql, m_connPool);//利用RAII将http请求和数据库连接的声明周期绑定在一起
        //process(模板类中的方法, 这里是http类)进行处理
        
        //工作线程只负责处理业务逻辑
        request->process();// 处理http请求的主要逻辑
    }
}
#endif
