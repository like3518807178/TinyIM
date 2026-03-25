# 2026-03-25（周三）

## 今日目标
- 完善 `Connection`：继续用单连接 `input_buffer` / `output_buffer` 承接收发状态
- 把服务端主链路收紧成“读事件驱动：收帧 -> 处理 -> 写回”
- 定好当前阶段协议字段：`cmd` / `request_id` / `body`
- 在动手前先遍历 TinyIM，补一轮上下文
- 补上今天的网络基础笔记：`epoll_ctl` / `epoll_wait` 参数与事件流

## 今日先遍历了哪些文件
- `include/TcpServer.h`
- `src/TcpServer.cpp`
- `include/EventLoop.h`
- `src/EventLoop.cpp`
- `src/main.cpp`
- `README.md`
- `docs/week2/day01.md`
- `docs/week2/day2.md`
- `docs/week1/day05.md`
- `docs/week1/decision_log.md`

这轮遍历主要确认三件事：
- 当前 `epoll` 已经接管到哪一步
- `Connection` 的状态边界是否已经站稳
- 旧协议如果要升级，最小改动点落在哪里

结论：
- Week 2 Day 2 已经把监听 fd 和连接 fd 都接进 `epoll`
- `Connection` 已经有 `input_buffer` / `output_buffer`，继续沿这个方向推进就对
- 主循环已经具备按 ready event 分发 `HandleRead()` / `HandleWrite()` 的骨架
- 当前协议还是“4 字节 body_len + payload”，没法直接表达 `cmd` 和 `request_id`
- 所以今天的关键不是重写 I/O 模型，而是把协议升级，并让“读事件驱动收帧、处理、写回”更完整

## 先补网络基础：`epoll_ctl` / `epoll_wait` 怎么看

### 1. `epoll_ctl` 的核心角色
`epoll_ctl()` 不是“等待事件”的函数，它负责维护“我到底想监听哪些 fd、监听哪些事件”这张表。

常见调用形式：

```c
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
```

参数理解：
- `epfd`：`epoll_create1()` 创建出来的 `epoll` 实例
- `op`：操作类型，最常见的是：
  - `EPOLL_CTL_ADD`
  - `EPOLL_CTL_MOD`
  - `EPOLL_CTL_DEL`
- `fd`：要被监听的目标 fd，比如监听 socket 或某个连接 socket
- `event`：告诉内核“我关心这个 fd 的哪些事件”，同时可以把业务侧标识塞进 `data`

对 TinyIM 来说，对应关系很清楚：
- 监听 fd：`ADD` 进去，通常关心 `EPOLLIN`
- 新接入的 `conn_fd`：`ADD` 进去，通常先关心 `EPOLLIN | EPOLLRDHUP`
- 当 `output_buffer` 非空：对这个 `conn_fd` 做 `MOD`，把 `EPOLLOUT` 打开
- 当 `output_buffer` 发空：再 `MOD` 回只监听读和对端关闭
- 连接关闭时：`DEL`

今天更清楚的一点是：
- `epoll_ctl()` 管的是“订阅关系”
- 它不负责告诉我们“现在有没有事件”
- 它是在给后续的 `epoll_wait()` 准备数据来源

### 2. `epoll_wait` 的核心角色
`epoll_wait()` 才是真正“拿就绪事件”的地方。

常见调用形式：

```c
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
```

参数理解：
- `epfd`：同一个 `epoll` 实例
- `events`：输出数组，内核把 ready event 填到这里
- `maxevents`：本次最多取回多少个 ready event
- `timeout`：
  - `-1`：一直等
  - `0`：立刻返回，不阻塞
  - `>0`：最多等这么多毫秒

返回值理解：
- `> 0`：这次拿到了多少个 ready event
- `== 0`：超时，没有事件
- `< 0`：失败；如果 `errno == EINTR`，通常是被信号打断，可以重试

TinyIM 当前封装成 `EventLoop::Wait(timeout_ms)` 后，对上层的意义是：
- 上层不用直接面对 `epoll_event` 数组
- 但仍然能拿到足够的信息：
  - 哪个 `fd`
  - 是否 `readable`
  - 是否 `writable`
  - 是否 `error`
  - 是否 `hangup`

### 3. 事件流到底是怎么串起来的
今天最值得记住的不是函数签名，而是这条链：

```text
先用 epoll_ctl 建立监听关系
-> 内核持续观察这些 fd
-> 某些 fd 状态变成 ready
-> epoll_wait 返回 ready event 列表
-> 应用按事件类型分发处理
-> 如果连接状态变化，再用 epoll_ctl(MOD/DEL) 更新监听关系
```

也就是说，`epoll_ctl()` 和 `epoll_wait()` 不是并列孤立的两个 API，而是一前一后的两段职责：
- `ctl` 负责“登记监听”
- `wait` 负责“收割结果”

## 今天的实现

### 1. 把协议从“只有 body”升级成“有字段的帧”
改动文件：
- `include/TcpServer.h`
- `src/TcpServer.cpp`

当前协议改成：

```text
4 bytes cmd
4 bytes request_id
4 bytes body_len
N bytes body
```

其中：
- `cmd`：表示消息类型，方便后续从 echo 过渡到真正命令分发
- `request_id`：请求唯一标识，响应原样带回，方便客户端对齐请求/响应
- `body_len`：正文长度
- `body`：业务载荷

这样做的直接价值是：
- 一条消息不再只是“字符串”
- 服务端已经能识别“这是什么请求”“这条响应该对应谁”
- 多连接并发场景下，至少协议语义层不会混成一团

### 2. `Connection` 继续承担单连接收发状态
今天没有推翻 `Connection`，而是继续沿已有设计收紧职责：
- `input_buffer`：累计接收到但还没完全消费的字节流
- `output_buffer`：累计待发送但还没完全发出的字节流

`HandleRead()` 里的链路现在是：

```text
recv() 读到字节
-> append 到 input_buffer
-> 循环 TryParseFrame()
-> 每解析出一条完整请求
-> ProcessMessage()
-> SendFrame() 追加到 output_buffer
```

`HandleWrite()` 里的链路是：

```text
只要 output_buffer 非空
-> send()
-> 成功多少就擦掉多少
-> EAGAIN 时保留剩余数据，等待下次可写
```

这就把“收帧 -> 处理 -> 写回”真正挂到了连接状态上。

### 3. 响应原样带回 `request_id`，保证并发时可对齐
当前处理逻辑还很简单，不是正式业务分发，而是先做一个带字段 echo：
- 响应保留原请求的 `cmd`
- 响应保留原请求的 `request_id`
- 响应 `body` 里带回 `cmd/request_id/body`

这样客户端即使并发发起多条请求，也可以用：
- 连接本身
- 响应里的 `request_id`

双重确认“这条响应对应哪条请求”。

### 4. 写事件仍然坚持动态开关
今天没有改动这个原则，反而更确认它的重要性：
- `output_buffer` 为空时，只监听 `EPOLLIN | EPOLLRDHUP`
- `output_buffer` 非空时，再把 `EPOLLOUT` 打开

原因还是那句老话：
- 大多数 socket 在多数时间都是“可写”的
- 如果常驻监听 `EPOLLOUT`，主循环会被大量无意义写事件唤醒

## 本地验收

### 1. 构建
执行：

```bash
cmake -S /home/like/TinyIM -B /home/like/TinyIM/build
cmake --build /home/like/TinyIM/build
```

构建通过。

### 2. 并发协议验收
今天额外补了脚本：
- `scripts/verify_concurrent_protocol.py`

它会做的事：
- 并发创建多个客户端连接
- 每个连接连续发送多条带 `cmd/request_id/body` 的请求
- 按同协议读取响应
- 校验：
  - 响应 `cmd` 是否等于请求 `cmd`
  - 响应 `request_id` 是否等于请求 `request_id`
  - 响应 `body` 是否包含对应请求内容

本地执行后，结果为全部通过，说明：
- 多连接并发发送请求可正常完成
- 响应没有串到别的请求上
- 新协议的编解码和缓冲处理链路是通的

今日验收：通过。

## 今天更清楚的理解
- `Connection` 的本质不是“保存 peer 地址”，而是单连接状态机
- `input_buffer` / `output_buffer` 是事件驱动读写模型的天然落点
- `epoll_ctl` 的重点是“订阅/修改监听关系”
- `epoll_wait` 的重点是“批量拿就绪结果”
- 真正的事件流是“订阅 -> 等待 -> 分发 -> 再订阅”，而不是只记 API 名字
- 协议里尽早引入 `cmd/request_id`，后续从 echo 过渡到真实 IM 命令会顺很多

## 当前主线图

```text
客户端连接
-> listen fd 就绪
-> epoll_wait() 返回监听事件
-> accept() 得到 conn_fd
-> conn_fd ADD 到 epoll

客户端发请求帧
-> conn_fd 收到 EPOLLIN
-> recv() 读入 input_buffer
-> TryParseFrame() 解析出 {cmd, request_id, body}
-> ProcessMessage() 生成响应
-> SendFrame() 追加到 output_buffer
-> MOD 打开 EPOLLOUT
-> send() 把 output_buffer 刷出去
-> 发空后 MOD 关闭 EPOLLOUT
```

## 下一步可以继续做什么
- 把 `ProcessMessage()` 从字符串拼接升级成真正的命令分发
- 给协议加状态码或版本字段
- 补自动化测试而不只是压测脚本
- 继续收口资源生命周期，比如把 `event_loop_` 也做成更稳妥的 RAII 管理
