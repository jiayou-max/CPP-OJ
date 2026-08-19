# 仿 LeetCode OJ 项目规格说明 (SPEC)

> 版本: v1.0 · 状态: 已确认 · 访谈结论汇总于 `## 9. 决策记录`

---

## 1. 业务目标与成功标准

### 1.1 定位
个人学习 / 技术练手项目。以 **C++ (cpp-httplib) + 原生 HTML/CSS/JS** 搭建一个功能闭环的仿 LeetCode 在线评测系统，重点在于学习 C++ 后端、HTTP 服务、并发、进程管理、数据库持久化与判题内核设计。

### 1.2 成功标准
- 本地可一键启动，功能闭环可用。
- 游客能浏览题目；注册登录后可编辑、提交、评测代码并看到细粒度结果。
- 管理员能在后台增删题目。
- 判题内核能正确处理 AC / WA / TLE / MLE / RE / CE，且死循环代码不会卡死服务器。

---

## 2. 功能需求 (FR)

### 2.1 用户系统
| 编号 | 需求 |
|------|------|
| FR-1.1 | 用户可自由注册 / 登录 / 登出（用户名 + 密码），密码加盐哈希后存储 |
| FR-1.2 | 会话基于 Session/Cookie；游客可浏览题目，**登录后才可提交代码** |
| FR-1.3 | 内置唯一管理员账号（不可通过注册接口创建同名，管理员由配置/初始化脚本创建） |
| FR-1.4 | 角色模型：`admin` 与 `user`。admin 可管理题目；user 仅查看 + 做题 |
| FR-1.5 | 未登录提交 → 重定向到登录页 |

### 2.2 题目浏览与做题
| 编号 | 需求 |
|------|------|
| FR-2.1 | **题库列表页**: 展示所有题目（标题、难度），支持基础导航 |
| FR-2.2 | **题目详情/做题页**: Markdown 描述 + 纯 textarea 代码编辑器（预填 C++ 模板）+ 提交按钮 + 结果展示区 |
| FR-2.3 | 题目不存在 → 404 页面 |

### 2.3 判题
| 编号 | 需求 |
|------|------|
| FR-3.1 | 语言: 仅 C++（g++ 编译），内核预留多语言扩展点 |
| FR-3.2 | 判定: 每道题多组输入/输出用例，运行用户程序后逐组比对标准输出 |
| FR-3.3 | 结果分类: **AC**（全部通过）/ **WA** / **TLE**（超时）/ **MLE**（内存超限）/ **RE**（运行错误）/ **CE**（编译错误，含详细编译日志） |
| FR-3.4 | 结果展示: 总体结果 + **逐用例状态** + **本次运行实际时间/内存峰值统计** |
| FR-3.5 | 同步判题 + **信号量限流**（全局同时判题数受限，防多路死循环提交占满线程池） |
| FR-3.6 | 全局限制: wall time ≤ 2s（判 TLE），内存 ≤ 256MB（RLIMIT_AS，判 MLE） |

### 2.4 管理后台
| 编号 | 需求 |
|------|------|
| FR-4.1 | 仅 admin 可访问 `/admin/*` 管理题目 |
| FR-4.2 | **新增题目**: 标题、描述（Markdown）、难度、多组输入/输出用例文本块 |
| FR-4.3 | **删除题目**: 前端二次确认（confirm）后**物理删除**，无回收站 |
| FR-4.4 | 普通用户无管理入口/接口 |

### 2.5 提交记录
| 编号 | 需求 |
|------|------|
| FR-5.1 | 提交记录持久化到 MySQL（submissions 表：用户、题目、代码、状态、时间/内存、时间戳） |
| FR-5.2 | MVP 页面不含独立“提交历史”页（数据已落库，后续可加 UI） |

---

## 3. 非功能需求 (NFR)

| 编号 | 需求 | 说明 |
|------|------|------|
| NFR-1 | 规模 | 单用户 / 低并发，个人本机 + 局域网部署 |
| NFR-2 | 性能 | 单进程 cpp-httplib 线程池足够；判题信号量限流避免打满线程 |
| NFR-3 | 安全 | 基础档: 密码加盐哈希、预编译 SQL 防注入、输出 HTML 转义防 XSS、参数校验 |
| NFR-4 | 判题隔离 | 用户代码以 **低权限用户 (nobody)** 运行 + 随机临时目录 + rlimit 资源限制 |
| NFR-5 | 部署 | 本机/局域网监听，无需 HTTPS/域名 |
| NFR-6 | 可运维 | 基础日志（控制台 + 关键错误文件日志），不要求备份 |
| NFR-7 | 依赖 | MySQL 为唯一外部服务；允许引入轻量 C++ 库（如 md4c 做 Markdown 渲染） |
| NFR-8 | 成本 | 零云成本，仅本机资源 |

---

## 4. 架构设计

### 4.1 架构图

```
┌──────────────────────────────────────────────────────────────┐
│                          Browser                             │
│        原生 HTML/CSS/JS · 纯整页刷新 · 无外部前端依赖          │
└───────────────────────────┬──────────────────────────────────┘
                            │ HTTP (SSR: 后端直接输出 HTML)
┌───────────────────────────▼──────────────────────────────────┐
│                   cpp-httplib (C++17)                        │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  路由层 (RESTful URL) · Session/Cookie 认证 · 参数校验   │  │
│  └──────────────┬───────────────────────────┬─────────────┘  │
│                 │                           │                │
│       ┌─────────▼───────┐        ┌──────────▼──────────┐     │
│       │  业务逻辑层      │        │  判题内核 (Judge)    │     │
│       │ 题目/用户/提交   │        │  信号量限流(全局并发) │     │
│       │ (预编译SQL)      │        └──────────┬──────────┘     │
│       └─────────┬───────┘                   │ fork+降权       │
│                 │                           │ (nobody)        │
│      ┌──────────▼──────────┐    ┌───────────▼──────────────┐  │
│      │   MySQL             │    │  g++ 编译 → 子进程运行     │  │
│      │  users/problems/    │    │  setrlimit(2s/256MB)      │  │
│      │  testcases/submissions│   │  随机临时目录 · 输出比对  │  │
│      └─────────────────────┘    └──────────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
```

### 4.2 技术栈
| 层 | 选型 | 理由 |
|----|------|------|
| 后端 | C++ + [cpp-httplib](https://github.com/yhirose/cpp-httplib) (header-only) | 硬性约束，单文件集成简单 |
| 前端 | 原生 HTML + CSS + JS，纯 textarea 编辑器 | 硬性约束，零构建链 |
| 渲染 | 服务端渲染 (SSR)，后端直接输出 HTML | 已确认；前端 JS 仅做表单/confirm 等增强 |
| Markdown | md4c 等轻量 C++ 库，后端转 HTML | 已确认允许引入 |
| 数据库 | MySQL | 已确认；题目/用户/提交持久化 |
| 判题 | fork + setuid(nobody) + setrlimit + g++ | 进程级低权限隔离，无 Docker 依赖 |

### 4.3 数据模型 (MySQL, utf8mb4)

```
users (
  id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  username    VARCHAR(64)  NOT NULL UNIQUE,
  password    VARCHAR(255) NOT NULL,        -- 加盐哈希
  salt        VARCHAR(32)  NOT NULL,
  role        ENUM('admin','user') NOT NULL DEFAULT 'user',
  created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP
)

problems (
  id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  title       VARCHAR(255) NOT NULL,
  description MEDIUMTEXT   NOT NULL,        -- Markdown 原文
  difficulty  ENUM('easy','medium','hard') NOT NULL DEFAULT 'easy',
  created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP
)

testcases (
  id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  problem_id  BIGINT UNSIGNED NOT NULL,     -- FK -> problems.id (级联删除)
  `order`     INT NOT NULL,                 -- 用例序号
  input       TEXT NOT NULL,
  expected    TEXT NOT NULL
)

submissions (
  id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  user_id     BIGINT UNSIGNED NOT NULL,     -- FK -> users.id
  problem_id  BIGINT UNSIGNED NOT NULL,     -- FK -> problems.id
  code        MEDIUMTEXT NOT NULL,
  status      ENUM('ac','wa','tle','mle','re','ce') NOT NULL,
  time_ms     INT NOT NULL DEFAULT 0,       -- 本次运行实际用时
  memory_kb   INT NOT NULL DEFAULT 0,       -- 内存峰值
  created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP
)
```

> 说明: `testcases` 独立成表便于逐用例管理；提交只存最终总体状态与资源统计，逐用例详情不落库（仅当次页面展示）。

### 4.4 判题内核流程 (Judge)

```
POST /problems/{id}/submit (登录后)
  → 信号量 P() 获取判题名额（全局并发 ≤ N，如 2）
  → 写用户代码到随机临时目录 main.cpp
  → g++ -O2 -o main main.cpp
       ├─ 编译失败            → CE（附完整 g++ stderr 日志）
       └─ 编译超时(>10s 兜底) → CE（标注“编译超时”）
  → 逐个测试用例:
       fork() → setuid(nobody) → chdir(临时目录)
                → 打开 input/expected 文件
                → setrlimit(RLIMIT_AS = 256MB)
                → exec ./main < input
      父进程 setitimer/wall-time 2s + waitpid:
       ├─ 超时 (wall > 2s)    → TLE（终止子进程）
       ├─ 内存超限            → MLE
       ├─ 非零退出/崩溃信号   → RE
       └─ 退出 0:
            ├─ stdout == expected → 该用例 AC
            └─ 不等             → 该用例 WA
  → getrusage 汇总: 实际 wall 时间 + 内存峰值
  → 汇总: 全部 AC → AC；否则按首个失败类型定最终状态
  → 落库 submissions
  → 信号量 V()
  → SSR 渲染结果页（总体 + 逐用例 + 时间/内存统计）
```

### 4.5 路由表 (纯整页刷新)

| 方法 | 路径 | 访问控制 | 功能 |
|------|------|----------|------|
| GET | `/` | 公开 | 重定向 `/problems` |
| GET | `/problems` | 公开 | 题库列表 |
| GET | `/problems/{id}` | 公开 | 题目详情 + 编辑器 |
| POST | `/problems/{id}/submit` | **登录** | 提交代码并同步判题，渲染结果页 |
| GET | `/login` / `/register` | 公开 | 登录/注册表单页 |
| POST | `/login` / `/register` | 公开 | 处理登录/注册 |
| POST | `/logout` | 登录 | 登出 |
| GET | `/admin/problems` | **admin** | 题目管理列表 |
| GET | `/admin/problems/new` | **admin** | 新增题目表单页 |
| POST | `/admin/problems` | **admin** | 创建题目（含测试用例） |
| POST | `/admin/problems/{id}/delete` | **admin** | 删除题目（前端二次确认，物理删除+级联删用例） |

### 4.6 项目目录结构（预期）

```
cpp-oj-vibe-coding/
├── CMakeLists.txt                    # 构建脚本（cpp-httplib 头文件 + md4c 源 + 源码）
├── README.md                         # 构建 / 初始化 / 启动 / 部署说明
├── SPEC.md                           # 本规格文档
├── .gitignore
│
├── src/
│   ├── main.cpp                      # 入口：启动自检、路由注册、HTTP 服务
│   ├── config.h                      # 全局配置：端口 / DB / 限流 / rlimit 参数
│   ├── logger.h|.cpp                 # 基础日志（控制台 + 错误文件）
│   ├── http_utils.h|.cpp             # 表单解析、HTML 转义、参数校验
│   │
│   ├── db/
│   │   ├── db.h|.cpp                 # MySQL 连接管理 / 连接池
│   │   └── db_query.h|.cpp           # 预编译语句（prepare/execute）封装
│   │
│   ├── auth/
│   │   ├── password.h|.cpp           # 加盐哈希（sha256 + salt）
│   │   ├── session.h|.cpp            # Session/Cookie 中间件
│   │   └── middleware.h|.cpp         # require_login / require_admin
│   │
│   ├── models/
│   │   ├── user.h|.cpp
│   │   ├── problem.h|.cpp
│   │   ├── testcase.h|.cpp
│   │   └── submission.h|.cpp
│   │
│   ├── services/
│   │   ├── user_service.h|.cpp       # 注册 / 登录 / 鉴权逻辑
│   │   ├── problem_service.h|.cpp    # 题目 CRUD（含用例事务、级联删除）
│   │   └── submission_service.h|.cpp # 提交落库 / 查询
│   │
│   ├── judge/
│   │   ├── judge.h|.cpp              # 判题编排：信号量限流、逐用例调度、汇总落库
│   │   ├── compiler.h|.cpp           # g++ 编译（临时目录 / 10s 兜底 / CE 日志）
│   │   └── runner.h|.cpp             # fork + setuid(nobody) + setrlimit + 逐用例比对
│   │
│   ├── markdown/
│   │   └── md.h|.cpp                 # md4c 封装（Markdown → HTML）
│   │
│   └── views/                        # SSR 页面渲染（后端拼 HTML 字符串）
│       ├── render.h|.cpp             # 页面骨架 / 布局拼装
│       ├── problem_pages.cpp         # /problems 列表 + 详情页
│       ├── auth_pages.cpp            # /login / /register
│       ├── admin_pages.cpp           # /admin/problems 管理页
│       ├── result_pages.cpp          # 判题结果页（总体 + 逐用例 + 统计）
│       └── error_pages.cpp           # 404 / 403 / 500
│
├── public/
│   └── css/style.css                 # 全局样式
│
├── db/
│   ├── schema.sql                    # 建表脚本（users/problems/testcases/submissions）
│   └── init_admin.sql|.cpp           # 初始化内置 admin 账号
│
├── docs/
│   └── deployment.md                 # 部署说明（nobody 用户、setuid、MySQL 初始化）
│
└── tests/                            # 可选：判题内核自测脚本（AC/WA/TLE/... 样例）
```

> 分层原则：`main` 只做装配与路由分发 → `services` 持有业务逻辑（含 SQL）→ `models` 仅承载数据行；`judge` 独立于 HTTP 层，便于后续异步化改造。

---

## 5. 边缘案例与异常处理

| 场景 | 处理 |
|------|------|
| 死循环代码 | wall time 2s 强制 kill → TLE，请求有界返回，不卡死线程 |
| 编译卡死 / 编译超时 | 编译阶段 10s 兜底 → CE（标注编译超时） |
| 运行崩溃 / 除零 / 非法内存 | 非零退出码或崩溃信号 → RE |
| 空白 / 语法错误代码 | 正常编译流程，按结果返回 CE 或相应错误 |
| 访问不存在题目 / 非法 id | 404 页面 |
| 游客提交代码 | 重定向登录页（带 return 提示） |
| 重复用户名注册 | 返回“用户名已存在”错误提示，不落库 |
| 注册/登录参数非法（空、超长、格式错） | 参数校验，返回表单错误提示 |
| 管理员访问 /admin 非登录 | 重定向登录页 |
| 普通用户访问 /admin | 403 页面 |
| 并发提交 | 信号量限流，超出则等待；判题使用随机临时目录避免互相干扰 |
| MySQL 不可用 | 返回 500 错误页 + 错误日志 |
| g++ 未安装 / nobody 用户缺失 | 启动自检输出明确错误提示 |

---

## 6. 风险与权衡

| 风险 | 权衡与缓解 |
|------|-----------|
| 同步判题阻塞 HTTP 请求 | 单用户场景可接受；信号量限流 + 2s 超时保证有界；后续可升级异步队列 |
| setuid(nobody) 需要提权 | 判题进程需以 root 启动以 fork 后降权，或部署时配置 setuid；文档注明 |
| 用户代码可穷尽 CPU（多实例） | 限流 + RLIMIT_CPU/AS 双重限制 |
| Markdown 引入第三方库 | md4c 为轻量 header 类库，单一依赖可接受 |
| MySQL 外部依赖 | 本机安装即可，数据模型预留扩展 |
| 物理删除不可恢复 | 二次 confirm 已确认，属用户接受的简化 |
| 仅 C++ 语言 | 内核抽象出“语言配置”（编译命令/扩展名），预留扩展点 |
| 无 HTTPS | 局域网信任环境可接受，文档标注公网部署需补 HTTPS |

---

## 7. TODO 清单（建议实现顺序）

### 阶段 0：工程骨架
- [ ] 初始化仓库目录结构（`src/`, `public/`, `db/`, `docs/`）
- [ ] 集成 cpp-httplib（header-only），跑通 Hello World SSR
- [ ] 集成 md4c（Markdown → HTML）
- [ ] CMake 构建脚本 + 依赖清单（README）
- [ ] MySQL 连接封装（连接管理 + 预编译语句封装）

### 阶段 1：数据层
- [ ] `schema.sql`：users / problems / testcases / submissions 建表
- [ ] 初始化脚本：创建内置 admin 账号（生成随机盐 + 哈希）

### 阶段 2：用户系统
- [ ] 注册 / 登录 / 登出 + Session/Cookie 中间件
- [ ] 密码加盐哈希（sha256 + salt）
- [ ] 页面：/login、/register（SSR 表单 + 错误提示）
- [ ] 访问控制辅助：`require_login` / `require_admin`

### 阶段 3：题目浏览
- [ ] 页面：/problems 列表（标题 + 难度）
- [ ] 页面：/problems/{id} 详情（Markdown 渲染 + textarea + 提交表单）
- [ ] 404 / 403 / 500 错误页
- [ ] 基础全局样式（CSS）

### 阶段 4：判题内核
- [ ] 编译器封装：临时目录 + g++ 编译 + 编译超时兜底 + CE 日志捕获
- [ ] 运行封装：fork + setuid(nobody) + setrlimit(2s/256MB) + 逐用例比对
- [ ] 结果判定与状态机（AC/WA/TLE/MLE/RE/CE）
- [ ] getrusage 资源统计（time/内存峰值）
- [ ] 信号量限流（全局并发判题数）
- [ ] 提交落库 + 结果页渲染（总体 + 逐用例 + 统计）

### 阶段 5：管理后台
- [ ] 页面：/admin/problems 列表 + 删除（二次确认）
- [ ] 页面：/admin/problems/new 表单（标题/描述/难度/多组用例）
- [ ] 后端：创建题目（事务：题目 + 用例）、物理删除（级联删用例）
- [ ] 权限校验（require_admin）

### 阶段 6：收尾
- [ ] 启动自检（g++、nobody、MySQL 可用性）
- [ ] 基础日志（控制台 + 错误文件）
- [ ] 端到端手动验收（对照第 8 节）
- [ ] README：构建、初始化、启动、部署说明

---

## 8. 验收标准（可测试）

| # | 验收项 | 通过标准 |
|---|--------|----------|
| A1 | 构建与启动 | 按 README 可编译、初始化 MySQL、启动服务 |
| A2 | 游客浏览 | 未登录可访问 `/problems` 与题目详情页 |
| A3 | 注册登录 | 注册新用户→登录成功；重复用户名被拒绝；未登录提交被重定向到登录页 |
| A4 | 管理后台 | admin 可新增题目（含多组用例）并在列表可见；删除有二次确认且题目的用例级联删除 |
| A5 | 权限 | 普通用户访问 `/admin/*` 返回 403；游客同理 |
| A6 | 判题-AC | 标准正确代码 → 总体 AC，逐用例全通过，显示时间/内存 |
| A7 | 判题-WA | 输出错误代码 → WA，且能看到是第几个用例失败 |
| A8 | 判题-TLE | 死循环代码 → 2s 内返回 TLE，服务器线程不被卡死 |
| A9 | 判题-CE | 语法错误代码 → CE 且展示 g++ 编译日志 |
| A10 | 判题-RE | 除零/崩溃代码 → RE |
| A11 | 判题-MLE | 大量内存分配代码 → MLE（256MB） |
| A12 | 资源统计 | 判题结果页显示实际运行时间与内存峰值 |
| A13 | 提交落库 | 提交后 submissions 表新增记录，字段正确 |
| A14 | 异常路由 | 不存在题目 id → 404；非法参数 → 表单错误提示；MySQL 不可用 → 500 页 |
| A15 | 并发安全 | 两个并发提交均能正确返回结果，无相互污染（随机目录隔离） |

---

## 9. 决策记录（访谈结论）

| # | 维度 | 决策 |
|---|------|------|
| D1 | 业务目标 | 个人学习/技术练手，非生产级产品 |
| D2 | 用户模型 | 自由注册 + 登录做题 + 游客浏览；内置唯一管理员 |
| D3 | 角色 | admin（管理题目）/ user（查看 + 做题） |
| D4 | 技术栈 | cpp-httplib + 原生三件套；SSR；MySQL；md4c；g++ 判题 |
| D5 | 编辑器 | 纯原生 textarea（不做 CodeMirror/Monaco） |
| D6 | 题目字段 | 基础字段：标题/描述(Markdown)/难度/多组 I/O 用例文本块；无每题的时空限制 |
| D7 | 判题 | 仅 C++（预留扩展）；多组用例比对 stdout；同步 + 信号量限流 |
| D8 | 判题限制 | wall time 2s → TLE；256MB RLIMIT_AS → MLE；编译 10s 兜底 → CE |
| D9 | 隔离 | 低权限用户(nobody) + 随机临时目录 + rlimit，无 Docker |
| D10 | 结果展示 | 总体 + 逐用例 + 实际时间/内存统计 |
| D11 | 持久化 | 题目/用户/提交记录落 MySQL；删除题目为二次确认后物理删除 |
| D12 | 部署 | 本机/局域网，无 HTTPS |
| D13 | 非功能 | 基础日志即可；Web 安全为基础档（哈希/预编译SQL/转义/校验） |
| D14 | 页面 | 四类页面：列表、做题详情、登录注册、管理后台；纯整页刷新 + URL 路由 |
