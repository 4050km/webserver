#ifndef _CONNECTION_POOL_ //防止同文件多次包含，
#define _CONNECTION_POOL_  //如果定义了此宏，那么下面的代码就不执行，没有定义则执行

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"

using namespace std;

class connection_pool
{
public:
	MYSQL *GetConnection();				 //获取数据库连接
	bool ReleaseConnection(MYSQL *conn); //释放连接
	int GetFreeConn();					 //获取连接
	void DestroyPool();					 //销毁所有连接

	//私有化它的构造函数，以防止外界创建单例类的对象；使用类的私有静态指针变量指向类的唯一实例(这里是返回的引用)，并用一个公有的静态方法获取该实例。
	//单例模式
	static connection_pool *GetInstance();

	void init(string url, string User, string PassWord, string DataBaseName, int Port, unsigned int MaxConn); 
private:
	connection_pool();
	~connection_pool();

private:
	unsigned int MaxConn;  //最大连接数
	unsigned int CurConn;  //当前已使用的连接数
	unsigned int FreeConn; //当前空闲的连接数

private:
	locker lock;//建立互斥锁对象
	list<MYSQL *> connList; //mysql连接池，list为双向链表
	sem reserve; //一个信号量对象，可能用于控制对连接池的并发访问。信号量通常用于同步和互斥，确保在某一时刻只有有限数量的线程能够访问某个资源。

private:
	string url;			 //主机地址
	string Port;		 //数据库端口号
	string User;		 //登陆数据库用户名
	string PassWord;	 //登陆数据库密码
	string DatabaseName; //使用数据库名
};

class connectionRAII{
//RAII（Resource Acquisition Is Initialization）是一种 C++ 中的设计模式，用于管理资源的获取和释放。在这种模式中，资源的获取和释放与对象的生命周期绑定，
// 资源的生命周期由对象的生命周期来控制。RAII 设计模式的核心思想是资源的初始化（获取）和资源的释放（清理）都在对象的构造函数和析构函数中完成。
public:
	// 构造函数，接受一个指向指针的指针和一个连接池的指针作为参数
	//双指针对MYSQL *con修改
	connectionRAII(MYSQL **con, connection_pool *connPool);
	// 析构函数，用于释放数据库连接
	~connectionRAII();
	
private:
	// 数据库连接指针
	MYSQL *conRAII;
	// 连接池指针
	connection_pool *poolRAII;
};

#endif
