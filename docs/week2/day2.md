# 2026-03-24（周二）

## 今日目标
- 加入 `Connection` 映射（`fd -> Connection`）
- 把 `conn_fd` 注册到 `epoll`
- 让服务端支持多客户端同时连入并完成 echo
- 在动手前，先从 TinyIM 现状出发，梳理阻塞 / 非阻塞与 I/O 多路复用的动机

## 动手前先遍历 TinyIM
今天先重新过了一遍这些文件：
- `README.md`
- `include/EventLoop.h`
- `src/EventLoop.cpp`
- `include/TcpServer.h`
- `src/TcpServer.cpp`
- `src/main.cpp`
- `docs/week2/day01.md`

这次遍历要回答的核心问题有两个：
- 现在 `epoll` 已经接管到了哪一步
- 连接状态管理应该挂在哪一层最顺手

遍历后的结论：
- Week 2 Day 1 已经把监听 fd 注册进 `epoll`
- `EventLoop` 目前只能返回 ready fd，还不支持 `MOD/DEL`
- `TcpServer` 里已经有 `connections_` 和 `Connection` 结构，`fd -> Connection` 的方向其实已经立住了
- 但新接入的 `conn_fd` 还没有注册进 `epoll`
- 主循环仍然会每轮遍历全部连接做 `HandleRead()` / `HandleWrite()`，本质上还是“手写轮询”

所以今天真正要补的，不是重写协议，也不是重写读写逻辑，而是把：

```text
监听 fd 走 epoll
已连接 fd 靠手写遍历
```

改成：

```text
监听 fd 和 conn_fd 都走 epoll
主循环只处理就绪事件
```

## 先补 OS 视角：为什么会有阻塞 / 非阻塞 / I/O 多路复用

### 1. 阻塞 I/O 的问题是什么
阻塞 socket 的语义很直接：
- `accept()` 没有新连接时会睡住
- `recv()` 没数据时会睡住
- `send()` 发不出去时也可能卡住

如果服务端只有一个线程，那么它一旦卡在某个连接上，就没法及时处理别的连接。

这在多客户端场景下的问题非常明显：
- A 客户端很慢
- 服务端线程却被 A 的一次阻塞读写卡住
- B、C、D 客户端即使已经准备好了，也得跟着等

所以阻塞 I/O 的主要矛盾不是“不能写程序”，而是：
- 单线程下并发能力差
- 线程被动睡眠，控制权不在应用手里
- 连接数一多，资源利用和响应性都会变差

### 2. 非阻塞解决了什么，还没解决什么
把 fd 设为非阻塞后：
- 没有新连接时，`accept()` 直接返回 `EAGAIN/EWOULDBLOCK`
- 没数据时，`recv()` 直接返回 `EAGAIN/EWOULDBLOCK`
- 暂时发不出去时，`send()` 也会返回 `EAGAIN/EWOULDBLOCK`

好处是：
- 应用线程不会被某一个 fd 强行睡死
- 线程重新拿回了“什么时候继续做别的事”的主动权

但非阻塞本身还不够，因为它带来另一个问题：
- 你虽然不会卡死
- 但你也不知道“到底哪个 fd 现在准备好了”
- 如果每轮都把所有连接扫一遍，就会变成空转轮询

也就是说，非阻塞只是把“被动等待”变成了“可以主动调度”，但还缺一个“谁就绪了就告诉我”的机制。

### 3. I/O 多路复用的动机
I/O 多路复用就是来解决这个问题的。

它做的事可以概括成一句话：
- 让内核帮我们盯住很多 fd
- 哪些 fd 就绪了，再一次性通知应用

这样就不需要：
- 一个连接配一个线程
- 也不需要应用自己把所有连接从头扫到尾

对 TinyIM 当前阶段来说，`epoll` 的价值主要是：
- 同时管理监听 fd 和多个连接 fd
- 只处理真正 ready 的 fd
- 为后续把读、写、拆包、发送缓冲都挂到事件驱动模型上打基础

## 今天的实现

### 1. `EventLoop` 从最小骨架补成可管理连接事件
改动文件：
- `include/EventLoop.h`
- `src/EventLoop.cpp`

新增能力：
- `Add(fd, events)`
- `Modify(fd, events)`
- `Remove(fd)`
- `Wait()` 返回 `ReadyEvent`

`ReadyEvent` 里带出的信息包括：
- `fd`
- 是否可读
- 是否可写
- 是否错误
- 是否挂断

这一步的关键意义是：
- `epoll_wait()` 不再只是告诉我们“某个 fd 来了”
- 而是开始把“来了什么事件”一起带出来

### 2. 真正把 `conn_fd` 注册到 `epoll`
改动文件：
- `src/TcpServer.cpp`

在 `AcceptNewConnections()` 里，现在每接收一个客户端，会做三件事：

```text
accept() 得到 conn_fd
-> 设为 non-blocking
-> 放入 connections_[conn_fd]
-> 注册到 epoll (EPOLLIN | EPOLLRDHUP)
```

这样 `connections_` 和 `epoll` 终于对齐了：
- `connections_` 负责保存连接状态
- `epoll` 负责告诉我们哪些连接现在值得处理

### 3. 主循环改成“只处理就绪连接”
之前的主循环是：
- 先看监听 fd 有没有事件
- 再遍历全部 `connections_`

今天改成：
- `event_loop_->Wait(1000)` 拿到 ready events
- 如果是监听 fd，可读就继续 `accept()`
- 如果是连接 fd，就按事件分发到 `HandleRead()` / `HandleWrite()`
- 如果出错或挂断，直接关连接
- 每次处理完，根据 `output_buffer` 是否为空，动态 `MOD` 为：
  - 只监听 `EPOLLIN | EPOLLRDHUP`
  - 或同时监听 `EPOLLOUT`

这一步是今天最核心的变化。

它意味着服务端终于不再每轮扫描所有连接，而是：
- 哪个连接 ready，就处理哪个连接
- 哪个连接暂时没事，就不碰它

### 4. 写事件为什么要动态开关
如果连接没有待发送数据，却一直监听 `EPOLLOUT`，通常会频繁收到“可写”通知，造成无意义唤醒。

所以这里采用的策略是：
- 默认只关心读事件和对端关闭事件
- 只有 `output_buffer` 不为空时，才把 `EPOLLOUT` 打开
- 缓冲区发空后，再把 `EPOLLOUT` 关掉

这是一种非常常见、也非常重要的事件驱动写法。

## 本地验收

### 构建
执行：

```bash
cmake -S /home/like/TinyIM -B /home/like/TinyIM/build
cmake --build /home/like/TinyIM/build
```

构建通过。

### 并发 echo 验收
本地启动服务端后，用 10 个并发客户端同时连接 `127.0.0.1:9999`，每个客户端都按当前 4 字节长度前缀协议发送一条消息，再读取服务端 echo。

验收结果：

```text
(0, True, 'server: client-0-hello')
(1, True, 'server: client-1-hello')
(2, True, 'server: client-2-hello')
(3, True, 'server: client-3-hello')
(4, True, 'server: client-4-hello')
(5, True, 'server: client-5-hello')
(6, True, 'server: client-6-hello')
(7, True, 'server: client-7-hello')
(8, True, 'server: client-8-hello')
(9, True, 'server: client-9-hello')
```

说明：
- 至少 10 个客户端可同时连接
- 每个客户端都能成功发送
- 每个客户端都收到了正确 echo

今日验收：通过。

## 今天新建立的理解
- `Connection` 映射不是为了“存一下信息”而已，它是单连接状态机的落点
- 非阻塞并不等于高并发，真正减少无效轮询的是 I/O 多路复用
- `epoll` 不是只管新连接，真正的价值在于把已连接 fd 也纳入统一事件分发
- 写事件不能常驻监听，应该和发送缓冲联动

## 当前代码里最值得回看的一条主线

```text
客户端连接
-> listen fd 在 epoll 中变为可读
-> accept() 拿到 conn_fd
-> connections_[conn_fd] = Connection
-> conn_fd 注册到 epoll
-> 客户端发消息
-> conn_fd 收到 EPOLLIN
-> HandleRead() 读入 input_buffer 并拆包
-> SendFrame() 写入 output_buffer
-> output_buffer 非空时打开 EPOLLOUT
-> HandleWrite() 发出数据
-> output_buffer 清空后关闭 EPOLLOUT
```

## 下一步可以继续做什么
- 把 `event_loop_` 从裸指针继续收口成更稳妥的 RAII 管理
- 给 `EventLoop` 增加更明确的事件掩码封装，减少业务层直接依赖 `epoll` 常量
- 补自动化回归测试，而不只是手工压测
