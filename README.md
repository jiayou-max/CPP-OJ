# 仿 LeetCode OJ (cpp-oj-vibe-coding)

个人学习项目：**C++ (cpp-httplib) + 原生 HTML/CSS/JS** 搭建的仿 LeetCode 在线评测系统。
完整需求/架构/验收标准见 [SPEC.md](./SPEC.md)，依赖安装与逐项验证见 [dependence.md](./dependence.md)。

## 依赖清单

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

### 安装命令

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
  libssl-dev
```

> 说明：按 SPEC 单文件集成要求，cpp-httplib 使用 vendored 的纯 header-only
> `third_party/httplib.h`，CMake 优先选择；apt 版 `libcpp-httplib-dev` 非纯
> header-only（需链接 `-lcpp-httplib`），仅作回退。md4c 的解析器与 HTML 渲染器
> 是独立库，两者都必须安装，且 `libmd4c-html0` 运行时包不含 `-l` 符号链接，
> 需一并安装 `libmd4c-html0-dev`。安装细节见 [dependence.md](./dependence.md)。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

产物：
- `build/oj_server` — HTTP 服务
- `build/oj_init_admin` — 内置管理员初始化工具（阶段 1）
- `build/oj_tests` — 单元测试（gtest，可用 `-DOJ_BUILD_TESTS=OFF` 关闭）

## 数据库初始化

```bash
# 1) 建库建表（幂等，可重复执行；业务库名 cpp_oj，见 src/config.h kDbName）
mysql -uroot < db/schema.sql

# 2) 创建/重置内置管理员（FR-1.3；密码加盐哈希落库）
./build/oj_init_admin admin '你的密码'
#    或 ./build/oj_init_admin '你的密码'（用户名默认 admin）
#    或 OJ_ADMIN_USER=admin OJ_ADMIN_PASSWORD='你的密码' ./build/oj_init_admin
```

> 非默认 MySQL（非 root 免密 / 非 localhost:3306）可用环境变量覆盖：
> `OJ_DB_HOST` / `OJ_DB_PORT` / `OJ_DB_USER` / `OJ_DB_PASSWORD`。
> 若 `kDbName` 指向的库尚未创建，`oj_server` 启动自检会失败——先执行上面第 1 步。
> 详见 [docs/deployment.md](./docs/deployment.md)。

## 运行

```bash
./build/oj_server
# 浏览器访问 http://localhost:8080/
```

## 测试

```bash
ctest --test-dir build --output-on-failure
```

## 项目结构

```
├── CMakeLists.txt            # 构建脚本
├── src/                      # 源码（main / routes / views / markdown / ...）
├── third_party/httplib.h     # cpp-httplib v0.10.3 (vendored, header-only)
├── public/                   # 静态资源（css）
├── db/                       # 建表脚本与初始化
├── docs/                     # 文档
└── tests/                    # 判题内核自测脚本（可选）
```
