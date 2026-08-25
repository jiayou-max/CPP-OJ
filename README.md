# 仿 LeetCode OJ（cpp-oj-vibe-coding）

> 个人学习 / 技术练手项目：用 **C++ (cpp-httplib) + 原生 HTML/CSS/JS** 从零搭建一个
> 功能闭环的仿 LeetCode 在线评测系统。

![tech] C++17 · cpp-httplib · MySQL 8 · md4c · ace.js · g++ 判题

---

## 简介

本项目是一个**无 Docker、零构建链**的在线评测系统（OJ）：后端为单进程
cpp-httplib 服务端渲染（SSR），前端为原生三件套 + vendored ace.js 代码编辑器；
判题内核通过 `fork + setuid(nobody) + setrlimit` 实现进程级隔离，可正确处理
**AC / WA / TLE / MLE / RE / CE** 六种结果，且死循环代码不会卡死服务器。

面向目标：学习 C++ 后端、HTTP 服务、并发、进程管理与判题内核设计。

## 核心特性

| 模块 | 特性 |
|------|------|
| 用户系统 | 注册 / 登录 / 登出、会话 Cookie（HttpOnly）、密码加盐哈希、内置唯一管理员 |
| 题目浏览 | 题库列表、Markdown 题目详情、ace.js 编辑器（语法高亮 + Ctrl+Enter 提交） |
| 判题内核 | 信号量限流（并发 ≤ 2）、2s 超时 TLE、256MB 内存 MLE、逐用例比对、资源统计 |
| 管理后台 | 题目的增 / 改 / 删（二次确认 + 级联删用例），普通用户无管理入口 |
| 提交落库 | submissions 表持久化每次提交（状态 / 时间 / 内存 / 代码） |
| 安全 | 预编译 SQL 防注入、输出 HTML 转义防 XSS（含 Markdown raw HTML 转义） |

判题隔离：用户代码以低权限 `nobody` 运行 + 随机临时目录 + rlimit 资源限制，
**无需 Docker**，本机即可完整运行。

## 快速开始

```bash
# 1) 安装依赖（详见 dependence.md）
sudo apt install -y build-essential cmake pkg-config mysql-server \
  libmysqlclient-dev libmd4c-dev libmd4c-html0-dev libssl-dev

# 2) 构建
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 3) 初始化数据库（先建库建表，再创建内置管理员；幂等可重复执行）
mysql -uroot < db/schema.sql
./build/oj_init_admin admin '你的密码'

# 4) 启动（判题降权需要 root，建议 sudo）
sudo ./build/oj_server

# 5) 浏览器访问 http://localhost:8080/
```

> 数据库默认连接 `127.0.0.1:3307 / root / 免密`，可用 `OJ_DB_HOST / OJ_DB_PORT /
> OJ_DB_USER / OJ_DB_PASSWORD` 环境变量覆盖。完整部署说明见 **DEPLOY.md**。

## 文档导航

| 文档 | 内容 |
|------|------|
| [SPEC.md](./SPEC.md) | 需求 / 架构 / 数据模型 / 路由表 / 验收标准（A1~A15） |
| [DEPLOY.md](./DEPLOY.md) | **部署文档**：构建、数据库初始化、启动、判题降权、故障排查 |
| [dependence.md](./dependence.md) | 依赖清单与逐项安装验证 |
| [API.md](./API.md) | HTTP 接口说明 |
| [接口自动化测试文档.md](./接口自动化测试文档.md) | 接口层自动化测试用例与基线数据 |
| [web自动化测试文档.md](./web自动化测试文档.md) | Web 端到端自动化测试（`npx playwright test`） |
| [web自动化测试文档-playwright.md](./web自动化测试文档-playwright.md) | Playwright CLI 交互式实测流程 |

## 测试

```bash
ctest --test-dir build --output-on-failure     # 单元测试（gtest）
```

测试覆盖：Markdown 渲染、模板、路由、日志、DB、密码哈希、会话、中间件、
用户/题目服务、编译器封装、判题运行与编排。

## 项目结构

```
cpp-oj-vibe-coding/
├── CMakeLists.txt            # 构建脚本（oj_server / oj_init_admin / oj_db_reset / oj_tests）
├── SPEC.md / DEPLOY.md       # 规格说明 / 部署文档
├── src/                      # C++ 源码（main / routes / services / judge / views / ...）
│   └── judge/                # 判题内核：编译封装 + fork 运行封装 + 编排
├── third_party/httplib.h     # cpp-httplib v0.10.3（vendored，纯 header-only）
├── www/                      # SSR 模板 + 静态资源（css / vendored ace.js）
├── db/                       # 建表脚本 + 初始化工具（init_admin / reset_db）
├── docs/                     # 其他文档
└── tests/                    # 判题内核自测脚本（可选）
```

## 状态

全部核心功能已完成并经接口 + Web 端到端自动化测试验证（M1~M7 全绿，含并发安全）。
可扩展方向：多语言判题、异步判题队列、提交历史页、HTTPS 支持。

## 相关

- 规格与决策记录见 [SPEC.md](./SPEC.md) 第 9 节。
- 本项目为学习用途，非生产级产品；公网部署前请补充 HTTPS 与更严格的账号策略。
