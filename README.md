* 使用Socket实现浏览器和服务器之间的通信
* 使用状态机解析HTTP请求报文，支持解析GET和POST请求
* 事件处理模式采用模拟Proactor模式
* 使用线程池实现多线程
* 访问服务器mysql数据库实现web端用户注册、登录功能，可以请求服务器图片和视频文件
* 实现同步/异步日志系统，记录服务器运行状态
* 经Webbench压力测试可以实现上万的并发连接数据交换
基础测试
------------
* 服务器测试环境
	* Ubuntu版本16.04
	* MySQL版本5.7.29
* 浏览器测试环境
	* Windows、Linux均可
	* Chrome

* 测试前确认已安装MySQL数据库

* 修改main.c中的数据库初始化信息

    ```C++
    // root root修改为服务器mysql的登录名和密码，3306为mysql的端口
	// yourdb修改为服务器上mysql中所创建的数据库名称
    connPool->init("localhost", "root", "root", "yourdb", 3306, 8);
    ```

* 修改http_conn.cpp中的root路径

    ```C++
	// 修改为服务器中的root文件夹所在路径，这里是Linux下的路径
    const char* doc_root="/home/WebServer/root";
    ```

* 生成server

    ```C++
    make server
    ```

* 启动server

    ```C++
    ./server port
    ```

* 浏览器端

    ```C++
    ip:port
    ```

服务器压力测试
===============
Webbench是有名的网站压力测试工具，它是由[Lionbridge](http://www.lionbridge.com)公司开发。

> * 测试处在相同硬件上，不同服务的性能以及不同硬件上同一个服务的运行状况。
> * 展示服务器的两项内容：每秒钟响应请求数和每分钟传输数据量。


测试规则
------------
* 测试示例

    ```C++
	webbench -c 10000  -t  30   http://127.0.0.1/phpionfo.php
    ```
* 参数

> * `-c` 表示客户端数
> * `-t` 表示时间


测试结果
---------
Webbench对服务器进行压力测试，经压力测试可以实现上万的并发连接.
> * 并发连接总数：10000
> * 访问服务器时间：30s
> * 每分钟响应请求数：805112 pages/min
> * 每秒钟传输数据量：1502782 bytes/sec
> * 402556个请求成功，零个请求失败
<div align=center><img src="https://github.com/4050km/webserver/blob/main/root/testresult.png" height="201"/> </div>
