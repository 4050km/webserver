#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
#include "../log/log.h"
//连接资源结构体成员需要用到定时器类
//需要前向声明
class util_timer;
//连接资源
//其本质是一个epoll事件，用来存储用户数据
//用户数据结构：客户段socket地址，socket文件描述符，定时器
struct client_data
{
    //客户端socket地址
    sockaddr_in address;
    //socket文件描述符
    int sockfd;
    //定时器
    util_timer *timer;
};
//定时器类
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    //任务的超时时间，这里使用绝对时间
    time_t expire;
    //任务回调函数
    void (*cb_func)(client_data *);
    //回调函数处理的客户数据，有定时器的执行者传递给回调函数
    client_data *user_data;
    //指向前一个定时器
    util_timer *prev;
    //指向下一个定时器
    util_timer *next;
};
//定时器链表，它是一个升序、双向链表、且带有头结点和尾结点
class sort_timer_lst
{
public:
    sort_timer_lst() : head(NULL), tail(NULL) {}
    //链表被销毁时，删除其中所有的定时器
    ~sort_timer_lst()
    {
        util_timer *tmp = head;
        while (tmp)
        {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }
    //将目标定时器timer添加到链表
    void add_timer(util_timer *timer)
    {
        if (!timer)
        {
            return;
        }
        if (!head)
        {
            head = tail = timer;
            return;
        }
        //如果目标定时器的超时时间小于当前链表中所有定时器的超时时间，则把定时器插入链表头部，作为链表新的头结点，否则调用重载函数addtimer把它插入到合适位置，以保持链表的升序性
        if (timer->expire < head->expire)
        {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        
        add_timer(timer, head);
    }
    //当某个定时任务发生变化时，调整对应的定时器在链表中的位置，这个函数只考虑北条政的定时器的超时时间延长的情况，即该定时器需要往链表的尾部移动
    void adjust_timer(util_timer *timer)
    {
        //被调整的定时器的指针为null，不用调整
        if (!timer)
        {
            return;
        }
        util_timer *tmp = timer->next;
        //被调整的定时器在链表尾部或者该定时器新的超时值仍然小于下一个定时器的超时时间，则不用调整
        if (!tmp || (timer->expire < tmp->expire))
        {
            return;
        }
        //如果目标定时器是链表的头结点，则将该定时器从链表中取出，并重新插入链表
        if (timer == head)
        {
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;
            add_timer(timer, head);
        }
        //如果目标定时器不是是链表的头结点，则将该定时器从链表中取出，然后再调用add_timer函数
        else
        {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);
        }
    }
    //将目标定时器从链表中删除
    void del_timer(util_timer *timer)
    {
        if (!timer)
        {
            return;
        }
        //下面这个条件成立表示链表中只有一个定时器，即目标定时器
        if ((timer == head) && (timer == tail))
        {
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }
        //如果链表中至少有两个定时器，且目标定时器是链表的头结点，则将链表的头结点重置为原头结点的下一个结点，然后删除目标定时器
        if (timer == head)
        {
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }
        //如果链表中至少有两个定时器，且目标定时器是链表的尾结点，则将链表的尾结点重置为原尾结点的前一个结点，然后删除目标定时器
        if (timer == tail)
        {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }
        //如果目标定时器位于链表中间，则把它前后的定时器串联起来，然后删除目标定时器
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }
    //处理链表上到期的http请求
    void tick()
    {
        if (!head)
        {
            return;
        }
        //获取当前时间
        //printf( "timer tick\n" );
        LOG_INFO("%s", "timer tick");
        Log::get_instance()->flush();
        time_t cur = time(NULL);//获取系统当前时间
        util_timer *tmp = head;
        //从头结点开始依次处理每个定时器，直到遇到一个尚未到期的定时器，这就是定时器的核心逻辑
        while (tmp)
        {
            //链表容器为升序排列
            
            //因为每个定时器都是用绝对时间作为超时值，所以我们可以把定时器的超时值和系统当前时间比较，从而判断定时器是否到期
            if (cur < tmp->expire)
            {
                break;
            }
            //当前定时器到期，则调用回调函数，关闭连接
            tmp->cb_func(tmp->user_data);
            //执行完定时器中的定时任务之后，就将它从链表中删除，并重置链表头结点
            head = tmp->next;
            if (head)
            {
                head->prev = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    //被公有成员add_timer和adjust_time调用
    //该函数表示将目标定时器timer添加到结点lsthead之后的部分链表中
    void add_timer(util_timer *timer, util_timer *lst_head)
    {
        util_timer *prev = lst_head;
        util_timer *tmp = prev->next;
        //遍历lsthead结点之后的部分链表，直到找到一个超时时间大于目标定时器的超时时间的结点，并将目标定时器插入到该节点之前
        while (tmp)
        {
            if (timer->expire < tmp->expire)
            {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        //如果遍历完lsthead结点之后的部分链表，仍未找到超时时间大于目标定时器的超时时间的结点，则将定时器插入到链表尾部，并把设置为链表新的尾结点
        if (!tmp)
        {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
    }

private:
    //头尾结点
    util_timer *head;
    util_timer *tail;
};

#endif
