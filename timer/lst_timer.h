#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
#include "../log/log.h"
//������Դ�ṹ���Ա��Ҫ�õ���ʱ����
//��Ҫǰ������
class util_timer;
//������Դ
//�䱾����һ��epoll�¼��������洢�û�����
//�û����ݽṹ���ͻ���socket��ַ��socket�ļ�����������ʱ��
struct client_data
{
    //�ͻ���socket��ַ
    sockaddr_in address;
    //socket�ļ�������
    int sockfd;
    //��ʱ��
    util_timer *timer;
};
//��ʱ����
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    //����ĳ�ʱʱ�䣬����ʹ�þ���ʱ��
    time_t expire;
    //����ص�����
    void (*cb_func)(client_data *);
    //�ص���������Ŀͻ����ݣ��ж�ʱ����ִ���ߴ��ݸ��ص�����
    client_data *user_data;
    //ָ��ǰһ����ʱ��
    util_timer *prev;
    //ָ����һ����ʱ��
    util_timer *next;
};
//��ʱ����������һ������˫�������Ҵ���ͷ����β���
class sort_timer_lst
{
public:
    sort_timer_lst() : head(NULL), tail(NULL) {}
    //��������ʱ��ɾ���������еĶ�ʱ��
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
    //��Ŀ�궨ʱ��timer��ӵ�����
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
        //���Ŀ�궨ʱ���ĳ�ʱʱ��С�ڵ�ǰ���������ж�ʱ���ĳ�ʱʱ�䣬��Ѷ�ʱ����������ͷ������Ϊ�����µ�ͷ��㣬����������غ���addtimer�������뵽����λ�ã��Ա��������������
        if (timer->expire < head->expire)
        {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        
        add_timer(timer, head);
    }
    //��ĳ����ʱ�������仯ʱ��������Ӧ�Ķ�ʱ���������е�λ�ã��������ֻ���Ǳ������Ķ�ʱ���ĳ�ʱʱ���ӳ�����������ö�ʱ����Ҫ�������β���ƶ�
    void adjust_timer(util_timer *timer)
    {
        //�������Ķ�ʱ����ָ��Ϊnull�����õ���
        if (!timer)
        {
            return;
        }
        util_timer *tmp = timer->next;
        //�������Ķ�ʱ��������β�����߸ö�ʱ���µĳ�ʱֵ��ȻС����һ����ʱ���ĳ�ʱʱ�䣬���õ���
        if (!tmp || (timer->expire < tmp->expire))
        {
            return;
        }
        //���Ŀ�궨ʱ���������ͷ��㣬�򽫸ö�ʱ����������ȡ���������²�������
        if (timer == head)
        {
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;
            add_timer(timer, head);
        }
        //���Ŀ�궨ʱ�������������ͷ��㣬�򽫸ö�ʱ����������ȡ����Ȼ���ٵ���add_timer����
        else
        {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);
        }
    }
    //��Ŀ�궨ʱ����������ɾ��
    void del_timer(util_timer *timer)
    {
        if (!timer)
        {
            return;
        }
        //�����������������ʾ������ֻ��һ����ʱ������Ŀ�궨ʱ��
        if ((timer == head) && (timer == tail))
        {
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }
        //���������������������ʱ������Ŀ�궨ʱ���������ͷ��㣬�������ͷ�������Ϊԭͷ������һ����㣬Ȼ��ɾ��Ŀ�궨ʱ��
        if (timer == head)
        {
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }
        //���������������������ʱ������Ŀ�궨ʱ���������β��㣬�������β�������Ϊԭβ����ǰһ����㣬Ȼ��ɾ��Ŀ�궨ʱ��
        if (timer == tail)
        {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }
        //���Ŀ�궨ʱ��λ�������м䣬�����ǰ��Ķ�ʱ������������Ȼ��ɾ��Ŀ�궨ʱ��
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }
    //���������ϵ��ڵ�http����
    void tick()
    {
        if (!head)
        {
            return;
        }
        //��ȡ��ǰʱ��
        //printf( "timer tick\n" );
        LOG_INFO("%s", "timer tick");
        Log::get_instance()->flush();
        time_t cur = time(NULL);//��ȡϵͳ��ǰʱ��
        util_timer *tmp = head;
        //��ͷ��㿪ʼ���δ���ÿ����ʱ����ֱ������һ����δ���ڵĶ�ʱ��������Ƕ�ʱ���ĺ����߼�
        while (tmp)
        {
            //��������Ϊ��������
            
            //��Ϊÿ����ʱ�������þ���ʱ����Ϊ��ʱֵ���������ǿ��԰Ѷ�ʱ���ĳ�ʱֵ��ϵͳ��ǰʱ��Ƚϣ��Ӷ��ж϶�ʱ���Ƿ���
            if (cur < tmp->expire)
            {
                break;
            }
            //��ǰ��ʱ�����ڣ�����ûص��������ر�����
            tmp->cb_func(tmp->user_data);
            //ִ���궨ʱ���еĶ�ʱ����֮�󣬾ͽ�����������ɾ��������������ͷ���
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
    //�����г�Աadd_timer��adjust_time����
    //�ú�����ʾ��Ŀ�궨ʱ��timer��ӵ����lsthead֮��Ĳ���������
    void add_timer(util_timer *timer, util_timer *lst_head)
    {
        util_timer *prev = lst_head;
        util_timer *tmp = prev->next;
        //����lsthead���֮��Ĳ�������ֱ���ҵ�һ����ʱʱ�����Ŀ�궨ʱ���ĳ�ʱʱ��Ľ�㣬����Ŀ�궨ʱ�����뵽�ýڵ�֮ǰ
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
        //���������lsthead���֮��Ĳ���������δ�ҵ���ʱʱ�����Ŀ�궨ʱ���ĳ�ʱʱ��Ľ�㣬�򽫶�ʱ�����뵽����β������������Ϊ�����µ�β���
        if (!tmp)
        {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
    }

private:
    //ͷβ���
    util_timer *head;
    util_timer *tail;
};

#endif
