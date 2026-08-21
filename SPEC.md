# 仿 LeetCode OJ 项目规格说明 (SPEC)

> 版本: v1.2 · 状态: 已确认 · 访谈结论汇总于 `## 9. 决策记录`
>
> 变更记录:
> - v1.2 前端统一迁移至 `www/`（D16）：SSR 页面改为「`www/` 模板 + 后端占位符替换」（新增 `src/views/template.*`），
>   HTML 不再硬编码于 C++ 字符串；原 `public/` 并入 `www/`，静态挂载 `/static → www/`；README 与 4.6/A.6 目录结构同步更新。
> - v1.1 新增「LSP 编程辅助」（FR-2.4~2.7 / 4.7 / 阶段 4.5 / A16~A18 / D15），语言服务器定为 clangd。

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
| FR-2.2 | **题目详情/做题页**: Markdown 描述 + textarea 代码编辑器（预填 C++ 模板，带 LSP 编程辅助覆盖层）+ 提交按钮 + 结果展示区 |
| FR-2.3 | 题目不存在 → 404 页面 |
| FR-2.4 | **LSP 编程辅助**: 编辑器接入语言服务器协议（后端代理 clangd），提供 **自动补全**（completion）、**诊断波浪线**（diagnostics）、**悬停文档**（hover） |
| FR-2.5 | LSP 会话按登录用户隔离（每用户一个 clangd 会话，见 4.7）；未登录调用 `/lsp/*` 一律重定向登录页 |
| FR-2.6 | LSP 仅做代码分析，**不参与判题**；判题结果仍以 g++ 编译 + 运行为准（两者相互独立） |
| FR-2.7 | 语言服务器可插拔：语言配置（启动命令 / 启动参数 / 编译选项 / 文件扩展名）抽象为「语言配置」，MVP 仅 C++/clangd，预留多语言扩展点 |

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
| NFR-7 | 依赖 | MySQL 为唯一**必选**外部服务；允许引入轻量 C++ 库（如 md4c 做 Markdown 渲染）；LSP 需**可选**安装 clangd（未安装则编辑器退化为纯 textarea，见第 5 节） |
| NFR-8 | 成本 | 零云成本，仅本机资源 |

---

## 4. 架构设计

### 4.1 架构图

```
┌──────────────────────────────────────────────────────────────┐
│                          Browser                             │
│       原生 HTML/CSS/JS · 页面整页刷新 · textarea + LSP 覆盖层  │
│       （补全弹窗 / 诊断波浪线 / hover 提示）                   │
└──────────────┬─────────────────────────────┬────────────────┘
               │ HTTP (SSR: 后端输出 HTML)    │ SSE 推送诊断
               │ HTTP POST JSON-RPC (LSP)    │ (text/event-stream)
┌──────────────▼─────────────────────────────▼────────────────┐
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
│  ┌──────────────▼────────────────────────┐  │                 │
│  │  LSP 中继（语言服务器协议，FR-2.4）    │  │                 │
│  │  · 每登录用户一个会话 → clangd 子进程  │  │                 │
│  │  · JSON-RPC over stdio 双向转发        │  │                 │
│  │  · 响应按 id 匹配 / 诊断经 SSE 推送     │  │                 │
│  │  · 空闲 30min 回收 · 会话数限流        │  │                 │
│  └──────────────┬────────────────────────┘  │                 │
│                 │ JSON-RPC (Content-Length 帧)                │
│      ┌──────────▼──────────┐    ┌───────────▼──────────────┐  │
│      │   MySQL             │    │  g++ 编译 → 子进程运行     │  │
│      │  users/problems/    │    │  setrlimit(2s/256MB)      │  │
│      │  testcases/submissions│   │  随机临时目录 · 输出比对  │  │
│      └─────────────────────┘    └──────────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
                          │ JSON-RPC (stdio)
             ┌────────────▼─────────────┐
             │  clangd (C++ 语言服务器)  │
             │  --compile-commands-dir  │
             │  (compile_flags.txt)     │
             └──────────────────────────┘
```

### 4.2 技术栈
| 层 | 选型 | 理由 |
|----|------|------|
| 后端 | C++ + [cpp-httplib](https://github.com/yhirose/cpp-httplib) (header-only) | 硬性约束，单文件集成简单 |
| 前端 | 原生 HTML + CSS + JS；textarea 编辑器 + 轻量 LSP 覆盖层 | 硬性约束，零构建链；LSP 不引入 CodeMirror/Monaco |
| 渲染 | 服务端渲染 (SSR)，后端直接输出 HTML | 已确认；前端 JS 仅做表单/confirm/LSP 覆盖层等增强 |
| Markdown | md4c 等轻量 C++ 库，后端转 HTML | 已确认允许引入 |
| 数据库 | MySQL | 已确认；题目/用户/提交持久化 |
| 判题 | fork + setuid(nobody) + setrlimit + g++ | 进程级低权限隔离，无 Docker 依赖 |
| LSP 传输 | JSON-RPC 2.0：浏览器 ⇄ 后端用 **HTTP POST + SSE**；后端 ⇄ clangd 用 **stdio（Content-Length 帧）** | httplib v0.10.3 无 WebSocket，POST+SSE 零额外依赖实现双向（见 4.7） |
| LSP 服务器 | **clangd**（C++ 语言服务器） | LLVM 官方实现；仅做解析/分析，与 g++ 判题互不干扰（FR-2.6） |

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

### 4.3.1 数据库注意事项（阶段 1 落地要点）

**连接与配置**
- 业务库名统一为 `cpp_oj`：`src/config.h` 的 `kDbName`、`db/schema.sql`、
  `db/init_admin.cpp` 三处必须保持一致，改动任一处需同步其余两处。
- `oj_server` 启动自检会连接 `cpp_oj`，因此**必须先执行
  `mysql -uroot < db/schema.sql` 再启动服务**，否则自检失败。
- 连接参数默认：`localhost:3306 / root / 免密`。若真实环境不符（如 root 有密码、
  端口不同、走 TCP），用环境变量 `OJ_DB_HOST / OJ_DB_PORT / OJ_DB_USER /
  OJ_DB_PASSWORD` 覆盖（当前 `oj_init_admin` 已支持，服务端由后续阶段接入）。
  注意：`localhost` 走 unix socket，`127.0.0.1` 走 TCP；socket 目录权限不足时
  需改走 TCP（如 `OJ_DB_HOST=127.0.0.1`）。
- **多实例风险（已实测）**：同一机器上可能存在多个 MySQL 实例，端口各不相同
  （如系统自带的 3306 与临时开发实例 3307）。项目默认 `localhost:3306`，但该
  端口的实例未必允许 `root` 免密登录（即使其初始化日志出现过
  `root@localhost is created with an empty password`，授权表也可能已被修改，
  出现 `ERROR 1045 (28000): Access denied`）。因此**连接被拒时先排查端口**：
  ```bash
  ss -ltnp | grep -E ':3306|:3307'      # 看哪些端口有 mysqld 在监听
  mysql -h127.0.0.1 -P<port> -uroot -e "SHOW DATABASES;"
  ```
  确认可用的实例后用 `OJ_DB_HOST=127.0.0.1 OJ_DB_PORT=<port>` 指向它；本项目
  建表/初始化/启动都应以**同一个可用实例**为准，避免「schema 建在 A 实例、
  服务却连 B 实例」导致查不到表。

**建表脚本（db/schema.sql）**
- 幂等（`CREATE ... IF NOT EXISTS`），可重复执行；全库 utf8mb4 +
  utf8mb4_unicode_ci，与 `src/db/db.cpp` 连接时设置的字符集保持一致。
- `testcases.order` 为 MySQL 保留字，建表/查询时必须反引号。
- 外键删除规则（已实测）：
  - `testcases.problem_id` → `problems` **CASCADE**：删题级联删用例（FR-4.3）。
  - `submissions.problem_id` → `problems` **CASCADE**：保证 admin 物理删题不被
    历史提交阻塞（FR-4.3「物理删除，无回收站」）。
  - `submissions.user_id` → `users` **RESTRICT**：无删用户功能，保护提交记录。

**密码存储（全站统一约定，阶段 2 必须复用）**
- `salt` = 16 随机字节 → 32 位 hex（`users.salt`）；`hash` = sha256(salt + password)
  → 64 位 hex（`users.password`），校验即重算比对。
- 实现唯一入口为 `src/auth/password.*`，注册/登录不得另起哈希方案，否则与
  既有 admin 密码互不兼容。
- 内置 admin 由 `oj_init_admin` 创建/重置（`INSERT ... ON DUPLICATE KEY UPDATE`
  幂等 upsert）；**重复执行会重置该用户名密码**（可作忘记密码的恢复手段）。
  FR-1.3 由 `users.username` 唯一键 + 注册接口禁用 `admin` 名共同保证。

**初始化顺序（验收 A1 前置步骤）**
1. 确认可用的 MySQL 实例（见上文「多实例风险」），确定其端口与 root 凭据；
   如非默认 `localhost:3306 / root / 免密`，以下命令都需带上
   `OJ_DB_HOST / OJ_DB_PORT`（及必要的 `OJ_DB_USER / OJ_DB_PASSWORD`）环境变量。
2. `mysql -h127.0.0.1 -P<port> -uroot < db/schema.sql`（建库建表，幂等可重复执行）
3. `OJ_DB_HOST=127.0.0.1 OJ_DB_PORT=<port> ./build/oj_init_admin admin '<密码>'`
   （建内置管理员；重复执行会重置该用户名密码）
4. 校验：`mysql -h127.0.0.1 -P<port> -uroot cpp_oj -e "SHOW TABLES; SELECT id,username,role FROM users;"`

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

### 4.5 路由表（页面整页刷新 + LSP JSON/SSE 接口）

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
| POST | `/lsp/open` | **登录** | 建立/复用 LSP 会话并打开文档（didOpen），返回 `session_id` + 初始诊断 |
| POST | `/lsp/change` | **登录** | 同步代码全文（didChange），新诊断经 SSE 推送 |
| POST | `/lsp/completion` | **登录** | 补全候选（textDocument/completion），返回 JSON 列表 |
| POST | `/lsp/hover` | **登录** | 悬停文档（textDocument/hover），返回 JSON |
| POST | `/lsp/close` | **登录** | 关闭会话（didClose + 终止 clangd 子进程） |
| GET | `/lsp/events` | **登录** | SSE 长连接，推送 `textDocument/publishDiagnostics` |

> 页面路由均为整页刷新；`/lsp/*` 为编辑器内部 JSON/SSE 接口（非整页刷新），详细设计见 4.7。

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
│   ├── lsp/                          # LSP 中继（阶段 4.5，见 4.7）
│   │   ├── lsp_client.h|.cpp         # clangd 子进程管理 + JSON-RPC stdio 帧编解码
│   │   ├── lsp_session.h|.cpp        # 会话生命周期（initialize/didOpen/didChange/completion/hover/didClose）
│   │   └── lsp_routes.cpp            # /lsp/* HTTP 端点 + SSE 诊断推送
│   │
│   ├── markdown/
│   │   └── md.h|.cpp                 # md4c 封装（Markdown → HTML）
│   │
│   ├── views/                        # SSR 页面渲染（读 www/ 模板 + 填充数据）
│   │   ├── render.h|.cpp             # 页面骨架：读 layout.html，填 {{title}}/{{body}}
│   │   ├── template.h|.cpp           # www/ 模板加载 + {{key}} 占位符替换
│   │   ├── problem_pages.cpp         # /problems 列表 + 详情页
│   │   ├── auth_pages.cpp            # /login / /register
│   │   ├── admin_pages.cpp           # /admin/problems 管理页
│   │   ├── result_pages.cpp          # 判题结果页（总体 + 逐用例 + 统计）
│   │   └── error_pages.cpp           # 404 / 403 / 500
│   │
│   └── test/                         # 单元测试（gtest）：render_test / routes_test
│
├── third_party/
│   └── httplib.h                     # cpp-httplib v0.10.3 纯 header-only（vendored）
│
├── www/
│   ├── layout.html           # SSR 页面骨架模板（导航 + 页脚，{{title}}/{{body}} 插槽）
│   ├── problems.html         # 题库列表页模板（{{rows}} 插槽）
│   ├── problems_empty.html   # 题库为空时的占位模板
│   ├── problem_detail.html   # 题目详情 + 编辑器页模板
│   ├── error.html            # 404 / 403 / 500 错误页模板
│   ├── css/style.css         # 全局样式
│   └── js/lsp_editor.js      # textarea LSP 覆盖层（补全弹窗 / 诊断波浪线 / hover 提示）
│
├── db/
│   ├── schema.sql                    # 建表脚本（users/problems/testcases/submissions）
│   └── init_admin.cpp                # 初始化内置 admin 账号（随机盐 + sha256，CMake 目标 oj_init_admin）
│
├── docs/
│   └── deployment.md                 # 部署说明（nobody 用户、setuid、MySQL 初始化）
│
└── tests/                            # 可选：判题内核自测脚本（AC/WA/TLE/... 样例）
```

> 分层原则：`main` 只做装配与路由分发 → `services` 持有业务逻辑（含 SQL）→ `models` 仅承载数据行；`judge` 独立于 HTTP 层，便于后续异步化改造。

### 4.7 LSP 中继设计（语言服务器协议，FR-2.4 ~ FR-2.7）

> 定位：仅做**编程辅助**（补全 / 诊断 / hover），**不参与判题**（FR-2.6）。
> 浏览器 ⇄ 后端：**HTTP POST + SSE**（httplib v0.10.3 无 WebSocket）；
> 后端 ⇄ clangd：标准 **JSON-RPC 2.0 over stdio（Content-Length 帧）**。

#### 4.7.1 传输与帧格式

- 后端 ⇄ clangd：单条消息以 `Content-Length: <N>\r\n\r\n<JSON-RPC 体>` 定界（LSP 标准 stdio 传输）。
- 浏览器 → 后端：`POST /lsp/*`，请求体为简化的 JSON，后端补齐 `jsonrpc/id/method/params` 字段后转发 clangd。
- 后端 → 浏览器：`GET /lsp/events` 为 SSE 长连接，推送 `textDocument/publishDiagnostics` 等服务器主动通知。

#### 4.7.2 会话生命周期

```
登录用户首次打开做题页编辑器
  → POST /lsp/open（携带代码全文 + 题目 id）
      · 无会话则创建：spawn clangd --compile-commands-dir=<会话临时目录>
      · 会话临时目录内写 compile_flags.txt（-std=c++17）
      · 依次发 initialize → initialized → textDocument/didOpen
      · 返回 { session_id, diagnostics }（首次诊断）
  → 编辑器输入（防抖 300ms）→ POST /lsp/change（全量代码）
      · 转发 textDocument/didChange
      · clangd 异步推送 publishDiagnostics → 后端经 SSE 推给浏览器
  → 触发补全（Ctrl+Space / 键入 . :: 等）→ POST /lsp/completion
      · 转发 textDocument/completion（带 position）
      · clangd 返回 CompletionItem[] → 后端裁剪字段 → JSON 返回前端
  → 悬停标识符 → POST /lsp/hover → 返回 markdown 文档 → tooltip 展示
  → 关页 / 显式关闭 → POST /lsp/close（didClose + 终止 clangd 子进程）
```

#### 4.7.3 会话管理与约束

- **会话粒度**：按登录 `user_id` 一个会话（页面整页刷新 → 单编辑器）。会话存活期间持有
  clangd 子进程（stdin/stdout/stderr 管道 + 互斥锁串行读写）。
- **响应匹配**：后端为每个 JSON-RPC 请求生成自增 `id`，后台线程持续读 clangd stdout，
  按 `id` 匹配并完成请求回调；`publishDiagnostics` 等无 `id` 通知直接推送 SSE 订阅者。
- **限流**：全局并发会话数上限（如 8），超出返回明确错误提示（见第 5 节）。
- **回收**：会话记录最后活动时间，空闲 30min 由后台线程回收并 kill clangd。
- **降权说明**：clangd 仅解析代码、不执行用户代码，**无需** nobody 降权（NFR-4 只针对判题内核）；
  进程仍受会话数限流约束。

#### 4.7.4 前端覆盖层（保留 textarea，不引入 CodeMirror/Monaco）

- **补全弹窗**：键入停止后触发请求，用隐藏镜像 `div`（复刻 textarea 字体/宽度/滚动）计算光标
  坐标，在光标下方渲染 `<ul>` 候选列表，↑/↓ 选择、Enter/Tab 插入，Esc 关闭。
- **诊断波浪线**：SSE 收到的 `publishDiagnostics` 按 `[start, end]` 区间记录，textarea 无法内联
  画线 → 用绝对定位的 SVG 波浪线层叠加在编辑器上方，滚动/输入时同步。
- **hover 提示**：`mouseover` 命中标识符或诊断区间时请求 hover，渲染 tooltip。

#### 4.7.5 语言配置扩展点（FR-2.7）

`LanguageConfig { 启动命令, 启动参数, 编译选项, 源文件扩展名 }`（MVP：C++/clangd）。
新增语言只需追加配置，LSP 路由与会话逻辑保持不变。

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
| clangd 未安装 / 启动失败 | `/lsp/*` 返回明确错误；编辑器退化为纯 textarea，判题不受影响（FR-2.6） |
| LSP 会话数超限 / 空闲超时 | 拒绝新会话或自动回收；前端提示刷新重试 |
| 并发 LSP 请求 | 每会话串行转发（clangd 单线程语义）；SSE 订阅与请求互不阻塞 |
| 未登录调用 `/lsp/*` | 重定向登录页 |
| LSP 参数非法 / JSON 解析失败 | 参数校验，返回 400 + 错误日志 |

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
| clangd 资源占用（每登录用户一进程） | 会话数限流 + 空闲 30min 回收；clangd 仅解析不执行，无需 nobody 降权 |
| clangd 诊断与 g++ 判题结论不一致 | LSP 仅为编程辅助，判题结论以 g++ 编译运行为准（FR-2.6） |
| httplib v0.10.3 无 WebSocket | LSP 双向用 POST + SSE 实现，不升级依赖；后续需长连接再评估 |

---

## 7. TODO 清单（建议实现顺序）

### 阶段 0：工程骨架
- [X] 初始化仓库目录结构（`src/`, `www/`, `db/`, `docs/`）
- [X] 集成 cpp-httplib（header-only），跑通 Hello World SSR（vendored 于 `third_party/httplib.h`）
- [X] 集成 md4c（Markdown → HTML）
- [X] CMake 构建脚本 + 依赖清单（README）
- [X] 基础日志（控制台 + 错误文件）
- [X] MySQL 连接封装（连接管理 + 预编译语句封装）

### 阶段 1：数据层
- [X] `schema.sql`：users / problems / testcases / submissions 建表
- [X] 初始化脚本：创建内置 admin 账号（生成随机盐 + 哈希）

### 阶段 2：题目浏览
- [X] 页面：/problems 列表（标题 + 难度）
- [X] 页面：/problems/{id} 详情（Markdown 渲染 + textarea + 提交表单）
- [X] 404 / 403 / 500 错误页
- [X] 基础全局样式（CSS）

### 阶段 3：判题内核
- [X] 编译器封装：临时目录 + g++ 编译 + 编译超时兜底 + CE 日志捕获
- [ ] 运行封装：fork + setuid(nobody) + setrlimit(2s/256MB) + 逐用例比对
- [ ] 结果判定与状态机（AC/WA/TLE/MLE/RE/CE）
- [ ] getrusage 资源统计（time/内存峰值）
- [ ] 信号量限流（全局并发判题数）
- [ ] 提交落库 + 结果页渲染（总体 + 逐用例 + 统计）

### 阶段 4：用户系统
- [ ] 注册 / 登录 / 登出 + Session/Cookie 中间件
- [X] 密码加盐哈希（sha256 + salt）（`src/auth/password.*`，阶段 1 初始化 admin 时已落地）
- [ ] 页面：/login、/register（SSR 表单 + 错误提示）
- [ ] 访问控制辅助：`require_login` / `require_admin`

### 阶段 4.5：LSP 编程辅助（语言服务器协议）-- 可选
- [ ] clangd 子进程管理 + JSON-RPC over stdio 帧编解码（`src/lsp/lsp_client.*`）
- [ ] 会话生命周期：initialize / didOpen / didChange / completion / hover / didClose（`src/lsp/lsp_session.*`）
- [ ] `/lsp/*` HTTP 端点 + SSE 诊断推送（`src/lsp/lsp_routes.cpp`）
- [ ] 前端覆盖层：textarea 补全弹窗 / 诊断波浪线 / hover 提示（`www/js/lsp_editor.js`）
- [ ] 会话限流 + 空闲回收（30min）+ 启动自检检测 clangd（缺失时编辑器退化）
- [ ] 单元测试：`lsp_client` 帧编解码 / 会话状态机（`src/test/lsp_test.cpp`）

### 阶段 5：管理后台
- [ ] 页面：/admin/problems 列表 + 删除（二次确认）
- [ ] 页面：/admin/problems/new 表单（标题/描述/难度/多组用例）
- [ ] 后端：创建题目（事务：题目 + 用例）、物理删除（级联删用例）
- [ ] 权限校验（require_admin）

### 阶段 6：收尾
- [ ] 启动自检（g++、nobody、MySQL 可用性）
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
| A16 | LSP-补全 | 登录做题页输入 `std::` 触发补全，弹窗列出候选并可插入代码 |
| A17 | LSP-诊断 | 输入含语法错误的代码，编辑器出现诊断波浪线，修正后消失 |
| A18 | LSP-会话管理 | 未登录调用 `/lsp/*` 重定向登录页；空闲会话被回收；clangd 缺失时编辑器正常退化为纯 textarea |

---

## 9. 决策记录（访谈结论）

| # | 维度 | 决策 |
|---|------|------|
| D1 | 业务目标 | 个人学习/技术练手，非生产级产品 |
| D2 | 用户模型 | 自由注册 + 登录做题 + 游客浏览；内置唯一管理员 |
| D3 | 角色 | admin（管理题目）/ user（查看 + 做题） |
| D4 | 技术栈 | cpp-httplib + 原生三件套；SSR；MySQL；md4c；g++ 判题；LSP 用 clangd |
| D5 | 编辑器 | 纯原生 textarea + 轻量 LSP 覆盖层（补全/诊断/hover）；不做 CodeMirror/Monaco |
| D6 | 题目字段 | 基础字段：标题/描述(Markdown)/难度/多组 I/O 用例文本块；无每题的时空限制 |
| D7 | 判题 | 仅 C++（预留扩展）；多组用例比对 stdout；同步 + 信号量限流 |
| D8 | 判题限制 | wall time 2s → TLE；256MB RLIMIT_AS → MLE；编译 10s 兜底 → CE |
| D9 | 隔离 | 低权限用户(nobody) + 随机临时目录 + rlimit，无 Docker |
| D10 | 结果展示 | 总体 + 逐用例 + 实际时间/内存统计 |
| D11 | 持久化 | 题目/用户/提交记录落 MySQL；删除题目为二次确认后物理删除 |
| D12 | 部署 | 本机/局域网，无 HTTPS |
| D13 | 非功能 | 基础日志即可；Web 安全为基础档（哈希/预编译SQL/转义/校验） |
| D14 | 页面 | 四类页面：列表、做题详情、登录注册、管理后台；纯整页刷新 + URL 路由 |
| D15 | LSP | 做题页接入 LSP（clangd）做编程辅助；保留 textarea + 覆盖层；传输用 POST+SSE（httplib v0.10.3 无 WebSocket）；LSP 仅分析不判题（FR-2.6）；每登录用户一会话 + 限流 + 空闲回收 |
| D16 | 前端目录 | 前端页面统一放 `www/`：HTML 页面抽为 SSR 模板文件（`{{key}}` 占位符），后端 `views/` 读模板填充数据；静态资源（css/js）随模板一并入 `www/`，移除 `public/`。新增页面只需加模板文件，不改 C++ 字符串 |

---

## 附录 A：依赖清单与构建运行（对应 README.md）

> 依赖安装与逐项验证的详细说明见 [dependence.md](./dependence.md)。

### A.1 依赖清单

| 依赖 | 用途 | Ubuntu 安装包 |
|------|------|---------------|
| g++ (C++17) | 编译本项目 + 判题用户代码 | `build-essential` |
| CMake ≥ 3.16 | 构建（CMakeLists.txt） | `cmake` |
| pkg-config | 定位 md4c / mysqlclient 头文件与链接参数 | `pkg-config` |
| MySQL 8 | 数据持久化（唯一外部服务） | `mysql-server` |
| MySQL C++ 客户端 | 连接 / 预编译语句 | `libmysqlclient-dev` |
| cpp-httplib 0.10.3 | HTTP 服务 | vendored `third_party/httplib.h`（apt 仅回退） |
| md4c 0.4.8 | Markdown 解析器 (`md_parse`) | `libmd4c-dev` |
| md4c-html 0.4.8 | Markdown HTML 渲染器 (`md_html`) | `libmd4c-html0-dev` |
| OpenSSL | SHA256 密码加盐哈希 | `libssl-dev` |
| `nobody` 用户 (UID 65534) | 判题内核降权运行 | 系统内置 |
| clangd (阶段 4.5 可选) | C++ 语言服务器（LSP 编程辅助，FR-2.4） | `clangd` |

### A.2 安装命令

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  mysql-server \
  libmysqlclient-dev \
  libcpp-httplib-dev \
  libmd4c-dev \
  libmd4c-html0-dev \
  libssl-dev \
  clangd
```

> 说明：按 SPEC 单文件集成要求，cpp-httplib 使用 vendored 的纯 header-only
> `third_party/httplib.h`，CMake 优先选择；apt 版 `libcpp-httplib-dev` 非纯
> header-only（需链接 `-lcpp-httplib`），仅作回退。md4c 的解析器与 HTML 渲染器
> 是独立库，两者都必须安装，且 `libmd4c-html0` 运行时包不含 `-l` 符号链接，
> 需一并安装 `libmd4c-html0-dev`。`clangd` 仅为 LSP 编程辅助（阶段 4.5）所需，
> 缺失时服务可正常启动，编辑器退化为纯 textarea（FR-2.6 / 验收 A18）。

### A.3 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

产物：
- `build/oj_server` — HTTP 服务
- `build/oj_tests` — 单元测试（gtest，可用 `-DOJ_BUILD_TESTS=OFF` 关闭）

### A.4 运行

```bash
./build/oj_server
# 浏览器访问 http://localhost:8080/
```

### A.5 测试

```bash
ctest --test-dir build --output-on-failure
```

### A.6 项目结构

```
├── CMakeLists.txt            # 构建脚本
├── src/                      # 源码（main / routes / views / markdown / ...）
├── third_party/httplib.h     # cpp-httplib v0.10.3 (vendored, header-only)
├── www/                      # 前端模板 + 静态资源（SSR 模板、css）
├── db/                       # 建表脚本与初始化
├── docs/                     # 文档
└── tests/                    # 判题内核自测脚本（可选）
```
