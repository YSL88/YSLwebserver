// //
// // Created by YSL on 2023/4/21.
// //

// #include <sys/epoll.h>
// /* 将fd上的EPOLLIN和EPOLLET事件注册到epollfd指示的epoll内核事件中 */
// void addfd(int epollfd, int fd, bool one_shot) {
//     epoll_event event;
//     event.data.fd = fd;
//     event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
//     /* 针对connfd，开启EPOLLONESHOT，因为我们希望每个socket在任意时刻都只被一个线程处理 */
//     if(one_shot)
//         event.events |= EPOLLONESHOT;
//     epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
//     setnonblocking(fd);
// }
// /* 创建一个额外的文件描述符来唯一标识内核中的epoll事件表 */
// int epollfd = epoll_create(5);
// /* 用于存储epoll事件表中就绪事件的event数组 */
// epoll_event events[MAX_EVENT_NUMBER];
// /* 主线程往epoll内核事件表中注册监听socket事件，当listen到新的客户连接时，listenfd变为就绪事件 */
// addfd(epollfd, listenfd, false);
// /* 主线程调用epoll_wait等待一组文件描述符上的事件，并将当前所有就绪的epoll_event复制到events数组中 */
// int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
// /* 然后我们遍历这一数组以处理这些已经就绪的事件 */
// for(int i = 0; i < number; ++i) {
// int sockfd = events[i].data.fd;  // 事件表中就绪的socket文件描述符
// if(sockfd == listenfd) {  // 当listen到新的用户连接，listenfd上则产生就绪事件
// struct sockaddr_in client_address;
// socklen_t client_addrlength = sizeof(client_address);
// /* ET模式 */
// while(1) {
// /* accept()返回一个新的socket文件描述符用于send()和recv() */
// int connfd = accept(listenfd, (struct sockaddr *) &client_address, &client_addrlength);
// /* 并将connfd注册到内核事件表中 */
// users[connfd].init(connfd, client_address);
// /* ... */
// }
// }
// else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
// // 如有异常，则直接关闭客户连接，并删除该用户的timer
// /* ... */
// }
// else if(events[i].events & EPOLLIN) {
// /* 当这一sockfd上有可读事件时，epoll_wait通知主线程。*/
// if(users[sockfd].read()) { /* 主线程从这一sockfd循环读取数据, 直到没有更多数据可读 */
// pool->append(users + sockfd);  /* 然后将读取到的数据封装成一个请求对象并插入请求队列 */
// /* ... */
// }
// else
// /* ... */
// }
// else if(events[i].events & EPOLLOUT) {
// /* 当这一sockfd上有可写事件时，epoll_wait通知主线程。主线程往socket上写入服务器处理客户请求的结果 */
// if(users[sockfd].write()) {
// /* ... */
// }
// else
// /* ... */
// }
// }