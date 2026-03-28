# 2026-03-27（周五）

## 任务背景与意义（这一步为什么非常重要）
今天做的不是“业务功能”，而是把 TinyIM 的数据库地基搭好。

在服务端开发里，数据库环境如果没有标准化，会出现很多问题：
- 新同学拉代码后跑不起来
- 每个人本地库结构不一致
- 后续开发登录、会话、消息落库时反复手工建表
- 线上/测试环境与本地差异太大

所以今天这一步的价值是：
- 把 MySQL 环境变成“可复现”的工程能力
- 把建库建表变成“一条命令/两步命令”的标准流程
- 为后续用户系统、会话系统、消息持久化打下基础

一句话总结：
> 今天完成的是数据库基础设施建设，它不是终点功能，但决定了后续功能开发是否顺畅、可协作、可交付。

---

## 本次到底完成了什么

### 1. 提供了可运行的 MySQL 环境（容器方案）
新增：`docker-compose.yml`
- 使用 `mysql:8.4`
- 容器名：`tinyim-mysql`
- 对外端口：`3306`
- 初始化参数：
  - `MYSQL_ROOT_PASSWORD=root123`
  - `MYSQL_DATABASE=tinyim`
  - `MYSQL_USER=tinyim`
  - `MYSQL_PASSWORD=tinyim123`
- 挂载数据卷：`mysql_data`（重启容器数据不丢）
- 健康检查：`mysqladmin ping`

### 2. 提供了统一建库建表脚本
新增：`sql/schema.sql`
- 创建数据库：`tinyim`
- 创建数据表：
  - `users`
  - `conversations`
  - `messages`
- 语句采用 `IF NOT EXISTS`，可重复执行

### 3. 提供了“一条命令初始化”入口（容器）
新增：`scripts/init_mysql.sh`
该脚本会自动执行：
1. 拉起 MySQL 容器
2. 等待 MySQL 就绪
3. 执行 `sql/schema.sql`
4. `SHOW TABLES` 验证建表结果

### 4. 提供了“本地 MySQL 两步方案”
新增：`scripts/init_mysql_local.sh`
- 适用于本地已安装并启动 MySQL 服务
- 执行 schema 并验表
- 支持环境变量覆盖连接参数

---

## 概念介绍（第一次接触建议先看）

### Docker 是什么
Docker 可以理解为“轻量级应用运行容器”。
- 你可以把 MySQL 放到容器里跑
- 不污染本机环境
- 同一套配置在不同机器行为更一致

### Docker Compose 是什么
`docker compose` 用于管理多个容器服务配置。
- 我们用它来定义并启动 `mysql` 服务
- 一条命令就能把服务拉起来

### MySQL 客户端（mysql 命令）是什么
`mysql` 命令是连接并执行 SQL 的工具。
- 本地模式要用它执行 `schema.sql`
- 容器模式里也是通过它做验表

### schema.sql 是什么
`schema.sql` 是“数据库结构脚本”。
- 记录数据库有哪些库/表/索引/外键
- 是项目数据库结构的统一来源（single source of truth）

### 为什么“一键初始化”重要
- 减少手工步骤和遗漏
- 降低新人接入成本
- 让“建库建表”可重复、可自动化（后续可接 CI）

---

## Ubuntu 24.04 安装步骤（本次实际使用）

### 1. 安装依赖
```bash
sudo apt update
sudo apt install -y docker.io docker-compose-v2 mysql-client
sudo systemctl enable --now docker
sudo usermod -aG docker $USER
```

### 2. 刷新当前终端组权限
```bash
newgrp docker
```

### 3. 验证安装是否成功
```bash
docker --version
docker compose version
mysql --version
docker ps
```

本次环境实际版本：
- Docker `28.2.2`
- Docker Compose `2.37.1`
- MySQL Client `8.0.45`

---

## 项目初始化与验收步骤（可直接复现）

### 方案 A：容器一条命令（推荐）
```bash
cd /home/like/TinyIM
./scripts/init_mysql.sh
```

预期输出关键点：
- `[INFO] waiting mysql to be ready...`
- `[INFO] applying schema: .../sql/schema.sql`
- `Tables_in_tinyim`
- `conversations`
- `messages`
- `users`
- `[OK] MySQL initialized successfully`

本次实际验收结果：通过。

### 方案 B：本地 MySQL 两步
```bash
# 第一步：先确保本地 MySQL 服务已经启动
# 第二步：执行初始化
cd /home/like/TinyIM
./scripts/init_mysql_local.sh
```

可选环境变量（覆盖默认连接参数）：
```bash
MYSQL_HOST=127.0.0.1 MYSQL_PORT=3306 MYSQL_USER=root MYSQL_PASSWORD=root123 ./scripts/init_mysql_local.sh
```

---

## 本次排障记录（真实踩坑）

### 问题
安装后 `docker ps` 仍报错：
- `permission denied while trying to connect to the Docker daemon socket`

### 原因
用户虽然已经加入 `docker` 组，但当前 shell 会话还没刷新组权限。

### 解决
执行：
```bash
newgrp docker
```
然后再次执行 `docker ps`，恢复正常。

---

## 交付清单
本次新增/更新文件：
- `docker-compose.yml`
- `sql/schema.sql`
- `scripts/init_mysql.sh`
- `scripts/init_mysql_local.sh`
- `README.md`（补充 MySQL 初始化说明）
- `docs/week2/day05.md`（本文）

---

## 对后续开发的直接影响
今天的成果会直接加速下面的开发任务：
- 用户注册/登录：`users` 表可直接落地
- 会话管理：`conversations` 表可直接扩展
- 消息持久化：`messages` 表已具备基础外键关系
- 本地联调：数据库初始化不再依赖手工操作
- 团队协作：同一命令可统一环境状态

这一步本质上是“把工程启动成本降到最低”，后面每一天都会因此更快。
