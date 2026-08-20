# 依赖清单与安装指南 (dependence.md)

> 适用环境: **空白 Ubuntu 22.04 (jammy)** · 对应 [SPEC.md](./SPEC.md) v1.0
>
> 所有命令均以 `sudo` 执行，或先 `sudo -i` 进入 root。

---

## 1. 依赖总览

| 依赖 | 版本 (Ubuntu 22.04) | 用途 | 来源 |
|------|--------------------|------|------|
| `build-essential` (gcc/g++/make) | 12.9 | 编译本项目 + 判题用户代码 (g++ C++17) | apt |
| `cmake` | 3.22.1 | 项目构建 (CMakeLists.txt) | apt |
| `pkg-config` | 0.29 | 定位 mysqlclient / md4c 头文件与链接参数 | apt |
| `mysql-server` | 8.0.46 | 唯一外部服务: 数据持久化 | apt |
| `libmysqlclient-dev` | 8.0.46 | C++ MySQL 客户端库 (连接/预编译语句) | apt |
| `libcpp-httplib-dev` | 0.10.3 | cpp-httplib 单头文件 HTTP 库 | apt |
| `libmd4c-dev` | 0.4.8 | Markdown → HTML 渲染 (md4c) | apt |
| `libssl-dev` | 3.0.2 | OpenSSL SHA256 (密码加盐哈希) | apt |
| `nobody` 用户 | 系统内置 | 判题内核降权运行用户 (UID 65534) | 系统自带 |

> 说明: Ubuntu 22.04 官方源已提供 `libcpp-httplib-dev` (0.10.3) 与 `libmd4c-dev` (0.4.8)，无需从源码编译或从 GitHub 拉取，可直接 apt 安装，并在 CMake 中通过 `find_package` 使用。

> ⚠️ cpp-httplib 特殊说明: apt 包 `libcpp-httplib-dev` 的 `httplib.h` 将实现拆分到 `libcpp-httplib.so`，**非纯 header-only**（直接包含会报未定义符号）。本项目按 SPEC "单文件集成" 要求，将上游 v0.10.3 的纯 header-only `httplib.h` vendored 到 `third_party/httplib.h`；CMake 优先使用 vendored 版本，apt 版仅作回退（此时需链接 `-lcpp-httplib`）。

---

## 2. 安装命令

### 2.1 一次性安装全部依赖

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
  libssl-dev
```

### 2.2 分步安装（按需）

**① 编译工具链**

```bash
sudo apt install -y build-essential cmake pkg-config
```

**② MySQL 服务端 + C++ 客户端开发库**

```bash
sudo apt install -y mysql-server libmysqlclient-dev
```

**③ HTTP 服务库 (cpp-httplib)**

```bash
sudo apt install -y libcpp-httplib-dev
```

**④ Markdown 渲染库 (md4c)**

```bash
sudo apt install -y libmd4c-dev
```

**⑤ 密码哈希 (OpenSSL)**

```bash
sudo apt install -y libssl-dev
```

**⑥ 判题内核所需的系统用户 (无需安装，默认存在)**

`nobody` 用户 (UID 65534) 为 Ubuntu 默认系统账户，用于判题内核 `setuid(nobody)` 降权运行用户代码：

```bash
id nobody        # 预期输出: uid=65534(nobody) gid=65534(nogroup)...
```

> ⚠️ 若输出异常/不存在（极少见），可手动创建：
> `sudo useradd -r -M -s /usr/sbin/nologin nobody`

---

## 3. 安装验证

### 3.1 编译工具链

```bash
g++ --version    # 期望 ≥ 11.x，支持 C++17
cmake --version  # 期望 ≥ 3.22
make --version
pkg-config --version
```

### 3.2 C++ 库头文件

```bash
# 各头文件存在即代表对应 dev 包安装成功
ls /usr/include/httplib.h
ls /usr/include/md4c.h
ls /usr/include/mysql/mysql.h
ls /usr/include/openssl/sha.h
```

### 3.3 链接可用性 (pkg-config)

```bash
pkg-config --cflags --libs mysqlclient   # 期望输出 -I/usr/include/mysql -lmysqlclient
pkg-config --cflags --libs md4c          # 期望输出 -lmd4c
```

### 3.4 MySQL 服务

```bash
sudo systemctl enable --now mysql
sudo systemctl status mysql --no-pager   # 期望 active (running)
mysql -uroot -e "SELECT VERSION();"      # 期望输出 8.0.x
```

---

## 4. 初始化 (首次启动项目前)

MySQL 安装后默认 `root` 无密码（Ubuntu 的 auth_socket 认证），按 SPEC 部署文档需创建项目专用数据库与账号：

```sql
-- 示例: 创建数据库与专用账号（生产请替换为强密码）
CREATE DATABASE IF NOT EXISTS oj_db CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS 'oj_user'@'localhost' IDENTIFIED BY 'oj_password';
GRANT ALL PRIVILEGES ON oj_db.* TO 'oj_user'@'localhost';
FLUSH PRIVILEGES;
```

> 建议把数据库名/账号/密码统一放入 `src/config.h`，后续执行 `db/schema.sql` 建表。

---

## 5. 备注

- **无需安装**：前端为原生 HTML/CSS/JS（无 npm/构建链）；判题隔离为 fork+setuid，**无 Docker 依赖**。
- **项目构建方式**：CMake 中通过 `find_package(OpenSSL REQUIRED)`、`find_package(PkgConfig)` + `pkg_check_modules` 链接 `mysqlclient` 与 `md4c`，`cpp-httplib` 通过 `find_path` 查找 `/usr/include/httplib.h` 后 `include_directories`。
- **g++ 与判题**：判题阶段直接用系统 `g++` 编译用户代码，随 `build-essential` 一并安装。
