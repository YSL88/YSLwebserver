// //
// // Created by YSL on 2023/4/21.
// //

// #include <sys/socket.h>
// #include <netinet/in.h>
// /* 创建监听socket文件描述符 */
// int listenfd = socket(PF_INET, SOCK_STREAM, 0);
// /* 创建监听socket的TCP/IP的IPV4 socket地址 */
// struct sockaddr_in address;
// bzero(&address, sizeof(address));  // bzero()函数的作用是将address变量的所有字节都设置为0。
// address.sin_family = AF_INET;  // 表明 IPV4地址族
// address.sin_addr.s_addr = htonl(INADDR_ANY);  /* INADDR_ANY：将套接字绑定到所有可用的接口 */
// address.sin_port = htons(port);
// // htons()函数用于将一个16位无符号整数从主机字节序转为网络字节序。
// // 在网络传输中,不同的系统使用不同的字节序存储数据,
// // 为了在网络中正确解析数据,TCP/IP协议规定网络字节序为大端字节序。
// // 也就是说,在通过网络发送数据前,主机需要将自己的本地字节序转为网络字节序。


// int flag = 1;
// /* SO_REUSEADDR 允许端口被重复使用 */
// setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
// /* 绑定socket和它的地址 */
// ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
// /* 创建监听队列以存放待处理的客户连接，在这些客户连接被accept()之前 */
// ret = listen(listenfd, 5);  // backlog = 5 表示监听队列的最大长度，超过这个长度的新链接会被拒绝
