1. lock 里面已经清楚，就是RAII封装一些锁 信号量 和 条件变量
2. 线程池和数据库连接池，数据库连接池是线程池的一个成员变量，
   线程池中的某些工作线程会在run的时候从数据库连接池取出连接，访问数据库
   线程池的主要作用是：容纳工作线程
   主线程和线程池中的工作线程通过一个请求队列发生关联，相当于生产者消费者吧
   1. 生产者消费者详细看看
   2. 
3. 方法 类要写接口文档  写在方法头一行 可以被识别
4. 测试服务正式启动了（确保 mysql docker 已启动
   1. make server 
   2. ./server (端口9901写死了)
   3. telnet 192.168.1.11 9901
   4. webbench -c 1 -t 1 http://192.168.1.11:9901/
# http
1. 阻塞与非阻塞
   recv()系统调用是否立即返回取决于所用的I/O模式 - 阻塞模式或非阻塞模式。
   在阻塞模式下,recv()调用会立即返回以下情况:
   1. 数据接收完毕 - 接收到所请求的全部数据,立即返回接收的数据长度
   2. 对方连接关闭 - 对方关闭连接,recv()会立即返回0
   3. 发生错误 - 返回-1,错误原因存于errno
      否则,调用会一直阻塞,直到有数据可读或连接关闭为止。
   在非阻塞模式下,recv()调用会立即返回以下情况:
   1. 数据接收完毕 - 接收到所请求的全部数据,立即返回接收的数据长度
   2. 对方连接关闭 - 对方关闭连接,recv()会立即返回0
   3. 发生错误 - 返回-1,错误原因存于errno
   4. 暂时无数据可读 - 返回-1,并设置errno为EAGAIN或EWOULDBLOCK
   在第4种情况下,我们需要再次调用recv()读取数据,直到数据可读、连接关闭或错误发生。
   所以总结来说:
- 在阻塞模式下,recv()只在数据接收完毕、连接关闭或错误时立即返回,否则阻塞
- 在非阻塞模式下,recv()会在上述情况下立即返回,另外,当暂时无数据可读时也会立即返回EAGAIN错误
- 非阻塞模式下需要循环调用recv()读取数据,以处理暂时无数据可读的情况
  一个简单的示例:
  阻塞模式:
  char buf[1024];
  nread = recv(sockfd, buf, sizeof(buf), 0);
  if (nread == -1) { /* 处理错误 */ }
  else if (nread == 0) { /* 对方关闭连接 */ }
  else { /* 处理接收的数据 */ }
  非阻塞模式:
  char buf[1024];
  while (1) {
  nread = recv(sockfd, buf, sizeof(buf), 0);
  if (nread == -1 && errno == EAGAIN) continue;
  else if (nread == -1) { /* 处理其他错误 */ }
  else if (nread == 0) { /* 对方关闭连接 */ }
  else { /* 处理接收的数据 */ }
  break;
  }
对于network socket,可以在创建socket时指定SOCK_NONBLOCK,或者调用setsockopt()来设置SO_NONBLOCK选项:
c
// socket()时指定
int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

// setsockopt()设置
int sockfd = socket(AF_INET, SOCK_STREAM, 0);
int yes = 1;
setsockopt(sockfd, SOL_SOCKET, SO_NONBLOCK, &yes, sizeof(yes));
没有检索到 SO_NONBLOCK 那 recv 应该都是阻塞IO
2. 只读一次（read_once 函数），有数据就交给线程池来处理这个 socket ，这个是怎么保证
    将所有数据读入对应buffer的
3. \r\n 代表换行 
    - Windows系统中使用\r\n表示换行
   - Linux/Unix系统中使用\n表示换行
   - Mac系统早期使用\r表示换行,现已与Linux/Unix统一使用\n
     所以,如果要编写跨平台的程序,通常使用\r\n来进行换行显示,这样可以兼容各个操作系统。
4. iovec
   iovec 是一种用于描述 I/O 操作的数据结构，它由一个指针和一个长度组成，用于指定要读取或写入的数据缓冲区和缓冲区的长度。
   iovec 通常用于支持零拷贝技术，可以将多个缓冲区的数据合并成一个 I/O 请求，从而提高数据传输的效率。
   writev(fd, iov, 2); 使用 writev 函数将 iov 中的所有缓冲区的数据一次性写入到文件中
# 数据库连接池
1. 功能：利用连接池完成登录和注册的校验功能
2. 使用**单例模式和链表**创建数据库连接池，实现对数据库连接资源的复用。
3. 工作线程从数据库连接池取得一个连接，访问数据库中的数据，访问完毕后将连接交还连接池。


