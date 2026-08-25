# 部署文档（DEPLOY.md）

> 面向**本机 / 局域网**部署的仿 LeetCode OJ（cpp-oj-vibe-coding）完整部署指南。
> 依赖安装细节见 [dependence.md](./dependence.md)，需求与验收标准见 [SPEC.md](./SPEC.md)。
> 应用为「零构建链」原生 HTML/CSS/JS + C++ 后端，**唯一必选外部服务是 MySQL**（NFR-7）。

---

## 1. 部署架构概览

```
Browser ── HTTP(:8080) ──> oj_server (cpp-httplib, C++17)
                              │
              ┌───────────────┼─────────────────┐
              │               │                 │
          MySQL:3306/3307   g++ 编译           fork + setuid(nobody)
          (cpp_oj 库)       (用户代码)          + setrlimit(2s/256MB)
```

- 后端：单进程 `oj_server`，监听 `0.0.0.0:8080`，页面为服务端渲染（SSR）。
- 判题：`fork + setuid(nobody) + setrlimit` 进程级隔离，**无 Docker 依赖**。
- 前端：原生三件套 + vendored ace.js（离线可用），无 npm / 构建链。

---

## 2. 前置条件

| 项目 | 要求 |
|------|------|
| 系统 | Ubuntu 22.04（已按 [dependence.md](./dependence.md) 安装全部依赖） |
| 编译器 | g++ ≥ 11（C++17）、CMake ≥ 3.16、pkg-config |
| 数据库 | MySQL 8（唯一外部服务），root 或专用账号可连 |
| 用户 | 系统内置 `nobody`（UID 65534，判题降权用，默认存在） |

验证环境：

```bash
g++ --version            # ≥ 11.x
cmake --version          # ≥ 3.22
id nobody                # 预期 uid=65534(nobody) gid=65534(nogroup)...
mysql --version          # 8.0.x
```

---

## 3. 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

产物（均在 `build/`）：

| 产物 | 用途 |
|------|------|
| `oj_server` | HTTP 服务（主程序） |
| `oj_init_admin` | 创建/重置内置管理员（阶段 1，FR-1.3） |
| `oj_db_reset` | 数据库重置工具（清理测试残留 + 构造接口自动化基线数据） |
| `oj_tests` | 单元测试（gtest，可用 `-DOJ_BUILD_TESTS=OFF` 关闭） |

> 编译单元测试需本机安装 gtest 开发包；仅部署运行可加 `-DOJ_BUILD_TESTS=OFF`。

---

## 4. 数据库初始化

> **顺序不可颠倒**：必须先执行建表脚本，再启动服务；否则 `oj_server` 启动自检
> 无法连到 `cpp_oj` 库（连接被拒）。

### 4.1 建库建表（幂等）

```bash
mysql -uroot < db/schema.sql
```

- 创建业务库 `cpp_oj`（utf8mb4 / utf8mb4_unicode_ci）与四张表：
  `users` / `problems` / `testcases` / `submissions`。
- 脚本可重复执行（全部 `IF NOT EXISTS`），外键级联规则已内置
  （删题级联删用例与提交，FR-4.3）。

### 4.2 创建内置管理员（FR-1.3）

```bash
./build/oj_init_admin admin '你的密码'
# 或：./build/oj_init_admin '你的密码'          （用户名默认 admin）
# 或：OJ_ADMIN_USER=admin OJ_ADMIN_PASSWORD='你的密码' ./build/oj_init_admin
```

- 密码以「16 字节随机盐 + sha256」哈希落库，与注册/登录共用同一套哈希方案
  （`src/auth/password.*`）。
- 幂等：重复执行会把该用户名密码**重置**为本次输入（忘记密码时的恢复手段）。
- 注册接口不可创建 `admin` 同名账号（`username` 唯一键 + 接口校验）。

### 4.3 校验

```bash
mysql -uroot cpp_oj -e "SHOW TABLES; SELECT id, username, role FROM users;"
# 预期：4 张表；users 表含刚才创建的 admin（role=admin）
```

### 4.4 可选：重置到自动化测试基线（oj_db_reset）

接口 / Web 自动化测试依赖固定基线数据。需要时执行：

```bash
./build/oj_db_reset                       # 管理员重置为 admin/admin123
./build/oj_db_reset admin '你的密码'      # 指定管理员密码
./build/oj_db_reset --dry-run             # 仅预演，不实际执行
```

作用：清空 `submissions`、删除自动化测试残留用户/题目（`web_`/`webadmin` 等前缀），
并构造「题 5 两数之和（2 组用例）+ 题 6 最长回文子串（0 组用例）」基线数据。
**生产环境请勿使用**（会清库）。

---

## 5. 连接参数覆盖

默认连接参数在 `src/config.h`，初始化工具与服务端均可用环境变量覆盖：

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `OJ_DB_HOST` | `127.0.0.1` | `127.0.0.1` 走 TCP；`localhost` 走 unix socket |
| `OJ_DB_PORT` | `3307` | 本机实测实例端口（3306 若 root 需密码则改用 3307） |
| `OJ_DB_USER` | `root` | |
| `OJ_DB_PASSWORD` | 空（免密） | |
| `OJ_DB_NAME` | `cpp_oj` | 业务库名（改库名需同时改 `src/config.h` / `db/schema.sql` / `db/init_admin.cpp`） |

```bash
OJ_DB_HOST=127.0.0.1 OJ_DB_PORT=3306 OJ_DB_USER=root OJ_DB_PASSWORD='xxx' ./build/oj_init_admin admin 'pwd'
```

> **多实例风险（已实测）**：同一机器可能同时存在多个 MySQL 实例（如系统 3306 与
> 临时开发实例 3307），默认 `127.0.0.1:3307`。若连接被拒，先排查端口：
>
> ```bash
> ss -ltnp | grep -E ':3306|:3307'        # 哪些端口有 mysqld 监听
> mysql -h127.0.0.1 -P<port> -uroot -e "SHOW DATABASES;"
> ```
>
> 建表、初始化、启动服务都应指向**同一个可用实例**，避免「schema 建在 A 实例、
> 服务却连 B 实例」导致查不到表。socket 目录权限不足时改走 TCP（`OJ_DB_HOST=127.0.0.1`）。

---

## 6. 启动服务

### 6.1 普通启动（本机 / 局域网）

```bash
./build/oj_server
# 浏览器访问 http://localhost:8080/
```

- 监听 `0.0.0.0:8080`，局域网内其他机器可用 `http://<本机IP>:8080/` 访问
  （需防火墙放行 8080，如 `sudo ufw allow 8080/tcp`）。
- 启动自检：MySQL 连接池初始化时会预建连接做可达性自检；失败则控制台输出
  `MySQL 连接池初始化失败，题目页面将返回 500`，服务仍可启动、判题不受影响，
  但数据库相关页面返回 500（SPEC A14）。

### 6.2 以 root 启动（判题降权必需，推荐）

判题内核运行用户代码时会把子进程 `setuid(nobody)` 降权（NFR-4），这需要服务以
**root 启动**（fork 后才有权降权）：

```bash
sudo ./build/oj_server
```

以 root 启动的效果：

- 用户代码以低权限 `nobody` 隔离运行，防止恶意代码读写宿主文件；
- `nobody` 用户缺失时仅告警、不阻断判题（回退 UID 65534）。

> 非 root 启动为 best-effort：`setuid` 失败仅打告警日志，子进程以当前用户运行
> （本机开发/测试可用）。资源限制（RLIMIT_AS/CPU/FSIZE）、wall 2s 超时 kill、
> 内存监控（/proc VmSize 轮询 → MLE）与进程组隔离在非 root 下仍全部生效。

### 6.3 后台运行与日志

```bash
setsid nohup ./build/oj_server >> /tmp/oj_server.log 2>&1 < /dev/null &
```

- 访问日志输出到控制台/stdout（每请求一行：`方法 路径 -> 状态码`）。
- 关键错误追加写入 **`oj_error.log`**（相对进程工作目录，NFR-6）。
- 会话为进程内内存存储：**重启服务会使所有登录会话失效**。

---

## 7. 部署后验证（Smoke Test）

```bash
# ① 首页与题库
curl -s -o /dev/null -w "%{http_code}\n" http://localhost:8080/          # 200
curl -s -o /dev/null -w "%{http_code}\n" http://localhost:8080/problems  # 200

# ② 静态资源
curl -s -o /dev/null -w "%{http_code}\n" http://localhost:8080/static/css/style.css  # 200

# ③ 登录（Set-Cookie 应含 HttpOnly; SameSite=Lax）
curl -s -D - -o /dev/null -X POST http://localhost:8080/login \
  -d "username=admin&password=你的密码" | grep -i set-cookie

# ④ 判题可用性：访问题 5 详情页（存在 2 组用例）
curl -s -o /dev/null -w "%{http_code}\n" http://localhost:8080/problems/5  # 200
```

浏览器人工验收对照 [SPEC.md](./SPEC.md) 第 8 节（A1~A15）。

---

## 8. 常见问题（Troubleshooting）

| 现象 | 排查 |
|------|------|
| `oj_server` 启动报 MySQL 连接失败 | 未先执行 `mysql -uroot < db/schema.sql`；或端口/凭据不对（见 §5）；`ss -ltnp` 确认实例 |
| `ERROR 1045 (28000): Access denied` | root 有密码或授权被改；改走可用实例并用 `OJ_DB_HOST/OJ_DB_PORT/OJ_DB_USER/OJ_DB_PASSWORD` 覆盖 |
| 页面访问题详情 500 / 题目列表空 | MySQL 未启动或库未建；检查 `oj_error.log` |
| 判题结果 CE 且日志含 `无法执行 g++` | 未安装 `build-essential`（判题直接用系统 g++） |
| 判题 RE 且日志含降权失败告警 | 服务非 root 启动；建议 `sudo` 启动以启用 nobody 降权 |
| 局域网其他机器无法访问 | 确认监听 `0.0.0.0:8080`（`ss -tlnp | grep 8080`）并放行防火墙 |
| 登录后刷新即掉线 | 重启过 `oj_server`（会话为内存存储）；或浏览器禁用了 Cookie |
| `/tmp` 空间不足 / 判题临时目录失败 | 判题在 `/tmp/oj_judge_*` 随机目录工作，确认 `/tmp` 可写有空间 |

---

## 9. 安全与运维提示（NFR-3 / NFR-5 / NFR-6）

- 本部署面向**本机 / 局域网信任环境**，无 HTTPS。公网部署前必须补 HTTPS 与更严格
  的账号策略（如改 root、限制注册、加验证码）。
- 安全基础档已内置：密码加盐哈希、预编译 SQL 防注入、输出 HTML 转义防 XSS
  （含 Markdown raw HTML 转义，`MD_FLAG_NOHTML`）、参数校验、会话 Cookie
  `HttpOnly + SameSite=Lax`。
- 判题以低权限用户（nobody）运行 + 随机临时目录 + rlimit 资源限制，恶意代码
  无法逃逸；全局信号量限流（并发判题 ≤ 2）防多路死循环提交打满线程池。
- 无独立备份机制；如需备份导出：`mysqldump -uroot cpp_oj > cpp_oj_backup.sql`。
