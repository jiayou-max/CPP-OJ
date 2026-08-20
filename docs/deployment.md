# 部署说明（Deployment）

> 对应 SPEC NFR-5/NFR-6/NFR-7、附录 A 与阶段 1「数据层」。当前内容覆盖 MySQL
> 初始化与内置管理员创建；判题降权（nobody / setuid）等后续阶段补充。

## 1. 前置条件

- Ubuntu 22.04，已按 [dependence.md](../dependence.md) 安装全部依赖。
- MySQL 8 已启动：

  ```bash
  sudo service mysql start
  mysqladmin -uroot status   # 免密 root 可连（本仓库默认配置）
  ```

> 若 root 有密码或不在本机 3306，见下方「3. 连接参数覆盖」。

## 2. 初始化数据库

### 2.1 建库建表（幂等）

```bash
mysql -uroot < db/schema.sql
```

会创建业务库 `cpp_oj`（utf8mb4 / utf8mb4_unicode_ci）及四张表：

| 表 | 说明 |
|----|------|
| `users` | 注册用户 + 内置管理员；`username` 唯一 |
| `problems` | 题目（标题 / Markdown 描述 / 难度） |
| `testcases` | 每题多组输入/输出用例，随题目级联删除 |
| `submissions` | 提交记录（状态 / 时间 / 内存），随题目级联删除 |

脚本可重复执行（`IF NOT EXISTS`）。`src/config.h` 的 `kDbName = "cpp_oj"`
与脚本一致，`oj_server` 启动自检会连库并校验。

### 2.2 创建内置管理员（FR-1.3）

```bash
cmake --build build --target oj_init_admin   # 若尚未构建
./build/oj_init_admin admin '你的密码'
```

- 密码以「随机盐 + sha256」哈希落库（`sha256(salt + password)`），盐与哈希
  方案与后续注册/登录共用（见 `src/auth/password.*`）。
- 幂等：重复执行会把该用户名密码重置为本次输入（忘记密码时可重置）。
- 管理员不可通过注册接口创建同名（`users.username` 唯一 + 角色校验，阶段 2 实现）。

## 3. 连接参数覆盖

默认连接参数来自 `src/config.h`（localhost:3306 / root / 免密）。初始化工具与
服务端均可用环境变量覆盖：

| 变量 | 默认 | 说明 |
|------|------|------|
| `OJ_DB_HOST` | `localhost` | 填 `127.0.0.1` 走 TCP，`localhost` 走 unix socket |
| `OJ_DB_PORT` | `3306` | |
| `OJ_DB_USER` | `root` | |
| `OJ_DB_PASSWORD` | 空 | |

```bash
OJ_DB_HOST=127.0.0.1 OJ_DB_USER=root OJ_DB_PASSWORD='xxx' ./build/oj_init_admin admin 'pwd'
```

## 4. 启动服务

```bash
./build/oj_server
# 浏览器访问 http://localhost:8080/
```

启动自检项（对应阶段 6）：g++、`nobody` 用户、MySQL 连通性。任一失败会在
控制台给出明确错误并退出。

## 5. 安全提示（NFR-3 / NFR-5）

- 本部署面向本机 / 局域网信任环境，无 HTTPS。公网部署前需补 HTTPS 与更严格的
  账号策略。
- 判题进程以低权限用户运行（阶段 4 实现）：需要 root 启动或配置 setuid 才能
  fork 后降权到 `nobody`（UID 65534），届时在此补充说明。
