# CPP OJ 接口交互文档 (API.md)

> 本文档基于 [SPEC.md](./SPEC.md) 第 4.5 节路由表，并结合当前代码中**已实现**的路由（`src/routes.cpp`）
> 整理而成。标注「规划中」的接口在 SPEC 中已定义但代码尚未实现（见文末「未实现接口」）。
>
> 版本对应代码提交：`eabb7a4`（当前 HEAD）。

---

## 1. 总览与通用约定

### 1.1 基本约定

- **传输协议**：HTTP/1.1，监听 `0.0.0.0:8080`（`src/config.h` `kServerPort`）。
- **渲染方式**：全站服务端渲染（SSR）。页面路由均返回完整 HTML 文档（`Content-Type: text/html; charset=utf-8`）。
- **表单编码**：所有提交接口均为 `application/x-www-form-urlencoded`（`POST` 体）。
- **认证方式**：Session/Cookie。登录/注册成功后服务端下发 `Set-Cookie: oj_session=<token>`；
  后续请求携带 `Cookie: oj_session=<token>`。Cookie 属性为 `HttpOnly; SameSite=Lax; Path=/`（无 `Secure`，NFR-5）。
- **会话有效期**：默认 7 天，滑动续期（每次合法访问刷新基准）；单进程内存存储，服务重启即失效。
- **通用数据状态**：`username`（表单字段）长度 ≤ 64；`code` 为任意长度源码文本。

### 1.2 通用响应状态

| 状态码 | 含义 | 说明 |
|--------|------|------|
| `200` | 成功 | 页面路由：返回 HTML 页面；错误校验失败时也返回 200 + 表单页内错误提示 |
| `302` | 重定向 | 登录/注册成功回 `/problems`；未登录提交回 `/login`；未登录访问 `/logout` 回 `/login` |
| `403` | 访问被拒绝 | 已登录但无权限访问受保护资源（如非 admin 访问 `/admin/*`，当前无此路由） |
| `404` | 资源不存在 | 非法 id、不存在的题目、未匹配路由 |
| `500` | 服务器内部错误 | MySQL 不可用、数据库查询失败、判题数据缺失等 |

> 错误页均为 SSR HTML（`views/error_pages.cpp`），路由内显式 set_content 的错误页保持原样，其余走统一错误处理器。

### 1.3 已实现路由一览

| 方法 | 路径 | 访问控制 | 功能 |
|------|------|----------|------|
| GET | `/` | 公开 | 落地页首页（大屏 hero + 统计 + 终端演示 + 特性区） |
| GET | `/problems` | 公开 | 题库列表页 |
| GET | `/problems/{id}` | 公开 | 题目详情 + ace.js 代码编辑器页 |
| POST | `/problems/{id}/submit` | **登录** | 提交代码同步判题，渲染结果页 |
| GET | `/login` | 公开（已登录则回题库） | 登录表单页 |
| POST | `/login` | 公开 | 处理登录 |
| GET | `/register` | 公开（已登录则回题库） | 注册表单页 |
| POST | `/register` | 公开 | 处理注册（成功自动登录） |
| POST | `/logout` | **登录** | 登出并清 Cookie |
| GET | `/admin/problems` | **admin** | 管理后台题目列表（含编辑/删除，删除二次确认） |
| GET | `/admin/problems/new` | **admin** | 新增题目表单页 |
| POST | `/admin/problems` | **admin** | 创建题目（含测试用例，事务） |
| GET | `/admin/problems/{id}/edit` | **admin** | 编辑题目表单页（预填现有字段与用例） |
| POST | `/admin/problems/{id}/edit` | **admin** | 保存题目修改（标题/描述/难度/用例，事务） |
| POST | `/admin/problems/{id}/delete` | **admin** | 物理删除题目（级联删用例） |
| GET | `/static/*` | 公开 | 静态资源（`www/` 挂载，如 `/static/css/style.css`、`/static/js/ace/ace.min.js`） |

---

## 2. 页面路由（GET，SSR）

### 2.1 `GET /`

落地页首页（阶段 5「大屏落地页」）：品牌大屏 hero + 实时统计（题目 / 用户 / 提交数）+ 终端演示 + 特性区。

- **参数**：无
- **响应**：`200` 落地页 HTML 页面。统计依赖数据库：不可用时渲染为 `--`，不返回 500。

---

### 2.2 `GET /problems`

题库列表页（FR-2.1），列出全部题目（编号、标题、难度），游客可访问。

- **参数**：无
- **响应**：
  - `200`：题库列表 HTML 页面（`题库 - CPP OJ`）。每行含题目 id、标题链接（`/problems/{id}`）、难度徽章。题库为空时渲染 `problems_empty.html` 占位页。
  - `500`：MySQL 不可用时返回 500 错误页（`Render500`）。

---

### 2.3 `GET /problems/{id}`

题目详情/做题页（FR-2.2）：Markdown 渲染的题目描述 + **ace.js 代码编辑器**（预填 C++ 模板，语法高亮，`Ctrl+Enter` 提交）+ 提交表单（`action="/problems/{id}/submit"`）。编辑器内容随表单经 `name="code"` 的隐藏 textarea 提交。

- **路径参数**：`{id}` 十进制非负整数（`http::ParseId` 校验）。
- **响应**：
  - `200`：题目详情页（标题 + 难度徽章 + Markdown 描述 + ace.js 编辑器）。
  - `404`：id 非法（非数字）或题目不存在 → 404 错误页「页面不存在」/「题目不存在」。
  - `500`：数据库查询失败（`kDbError`）。

---

### 2.4 `GET /login`

登录表单页（FR-1.2），用户名 / 密码输入框附使用提示。

- **参数**：无
- **响应**：
  - `200`：登录表单页（`action="/login" method="post"`，字段 `username` / `password`）。
  - `302`：**已登录**用户访问 → 重定向 `/problems`（避免重复登录）。

---

### 2.5 `GET /register`

注册表单页（FR-1.1），含用户名 / 密码（6~64 位）/ 确认密码三个输入框及输入提示。

- **参数**：无
- **响应**：
  - `200`：注册表单页（字段 `username` / `password` / `password2`）。
  - `302`：**已登录**用户访问 → 重定向 `/problems`。

---

### 2.6 `GET /static/*`

静态资源挂载：`set_mount_point("/static", "./www")`，找不到时回退 `../www`（build 目录运行）。样式表路径：`/static/css/style.css`。

- **响应**：`200` 对应静态文件。

---

## 3. 表单提交接口（POST）

### 3.1 `POST /register`

注册新用户（FR-1.1）。校验通过后**自动登录**（下发会话 Cookie + 重定向题库）。

- **请求体**（`application/x-www-form-urlencoded`）：

  | 字段 | 类型 | 必填 | 校验规则 |
  |------|------|------|----------|
  | `username` | string | 是 | 仅字母/数字/下划线，长度 1~64；不可为 `admin`（FR-1.3，由白名单 + 唯一键共同保证） |
  | `password` | string | 是 | 长度 6~64 |
  | `password2` | string | 是 | 确认密码，须与 `password` 完全一致 |

- **响应**：
  - `302` + `Set-Cookie: oj_session=<token>`：注册成功，重定向 `/problems`。
  - `200`：校验失败 → 注册表单页 + 错误提示
    - 两次密码不一致：「两次输入的密码不一致。」
    - 用户名格式非法：「用户名仅允许字母/数字/下划线（1~64 位），密码长度 6~64。」
    - 用户名已存在：「用户名已存在，请换一个。」（不落库）
  - `500`：数据库不可用 / 会话创建失败（`Render500`）。

---

### 3.2 `POST /login`

用户登录（FR-1.2）。

- **请求体**：

  | 字段 | 类型 | 必填 | 说明 |
  |------|------|------|------|
  | `username` | string | 是 | 用户名 |
  | `password` | string | 是 | 密码（校验通过后加盐哈希比对） |

- **响应**：
  - `302` + `Set-Cookie: oj_session=<token>`：登录成功，重定向 `/problems`。
  - `200`：登录失败 → 登录表单页 + 错误提示
    - 格式非法：「用户名或密码格式不正确。」
    - 账号不存在或密码错误：「用户名或密码错误。」（统一文案，防用户名枚举）
  - `500`：数据库不可用 / 会话创建失败。

---

### 3.3 `POST /logout`

登出（FR-1.1）：销毁会话 + 清除 Cookie。

- **参数**：无
- **响应**：
  - `302` + `Set-Cookie: oj_session=; Max-Age=0...`：登出成功，重定向 `/problems`。
  - `302`：未登录调用 → 重定向 `/login`（`RequireLogin` 兜底）。

---

### 3.4 `POST /problems/{id}/submit`

提交代码并**同步判题**（FR-2.2 / 判题内核），渲染判题结果页。结果页展示总体状态 + 逐用例状态 + 时间/内存统计；CE 时展示完整 g++ 编译日志（FR-3.3 / FR-3.4）。

- **访问控制**：必须登录（FR-1.5）。未登录 → `302` 重定向 `/login`（此校验先于题目存在性等一切校验）。
- **路径参数**：`{id}` 十进制非负整数。
- **请求体**：

  | 字段 | 类型 | 必填 | 说明 |
  |------|------|------|------|
  | `code` | string | 是 | C++ 源码全文 |

- **判题流程**（SPEC 4.4）：信号量限流（全局并发 ≤ 2，FR-3.5）→ g++ 编译（10s 超时兜底）→ 逐用例运行比对（2s wall / 256MB RLIMIT_AS，`nobody` 降权）→ 汇总 → 登录用户落库 `submissions`。
- **响应**：
  - `200`：判题结果页（`判题结果：{题目标题} - CPP OJ`）。包含：
    - 总体状态标签：`通过 (AC)` / `答案错误 (WA)` / `超时 (TLE)` / `内存超限 (MLE)` / `运行错误 (RE)` / `编译错误 (CE)`（对应 `SubmissionStatusLabel`）。
    - 统计：`用时 {time_ms} ms · 内存 {memory_kb} KB`（时间 = 各用例 wall 之和，内存 = 各用例峰值取最大）。
    - 逐用例表格：用例序号 + 状态 + 时间 + 内存。
    - CE 时附加「编译错误日志」/「编译超时」块，展示 HTML 转义后的完整 g++ stderr。
  - `302`：未登录 → `/login`。
  - `404`：id 非法（非数字）或题目不存在 → 404 错误页。
  - `500`：数据库不可用；或题目存在但**无测试用例** → 500「判题不可用」错误页。
  - 说明：落库失败仅记错误日志，不阻塞结果展示。

---

## 4. 通用错误处理器

未匹配路由 / 状态码 ≥ 400 且响应体为空时，统一渲染错误页（`set_error_handler`）：

| 状态码 | 页面标题 | 说明 |
|--------|----------|------|
| `404` | 页面不存在 | 未知路径（如 `/does-not-exist`） |
| `403` | 访问被拒绝 | 权限不足 |
| `>=500` | 服务器内部错误 | 未捕获异常等 |

错误页为 SSR HTML，带当前登录态导航。

---

## 5. 辅助信息

### 5.1 会话 Cookie

- 名称：`oj_session`（`src/config.h` `kSessionCookieName`）。
- 格式：`oj_session=<token>`；`<token>` 为随机生成的会话标识。
- 生命周期：默认 7 天滑动续期；登出销毁；服务重启后全部失效（内存存储，不落库）。

### 5.2 判题状态定义（返回结果页中展示）

| 状态 | 库中 ENUM | 触发条件 |
|------|-----------|----------|
| 通过 (AC) | `ac` | 全部用例退出码 0 且 stdout 与 expected 一致 |
| 答案错误 (WA) | `wa` | 退出码 0 但 stdout 与 expected 不一致（含输出超限，>8MB 按 WA） |
| 超时 (TLE) | `tle` | 单用例 wall time > 2s，父进程 SIGKILL 整个进程组 |
| 内存超限 (MLE) | `mle` | VmSize 轮询超过 256MB → SIGKILL |
| 运行错误 (RE) | `re` | 非零退出码 / 崩溃信号（除零、非法内存等） |
| 编译错误 (CE) | `ce` | 编译失败（附 g++ 日志）或编译超时（>10s 兜底） |

---

## 6. 管理后台接口（阶段 5，admin 专属）

> 对应中间件 `auth::RequireAdmin`（`src/auth/middleware.h`）：未登录 → 302 `/login`；已登录非 admin → 403。

### 6.1 `GET /admin/problems`

管理后台题目列表（FR-4.1）。含「新增题目」入口；每行带「编辑」链接与「删除」表单（前端 JS 二次确认后 POST）。

- **访问控制**：admin。
- **响应**：`200` 管理页 HTML（题目管理）。题库为空显示占位提示。

### 6.2 `GET /admin/problems/new`

新增题目表单页（FR-4.2）：标题 / 难度 / Markdown 描述 / 多组测试用例（动态增删）。

- **访问控制**：admin。
- **响应**：`200` 表单页 HTML。

### 6.3 `POST /admin/problems`

创建题目（FR-4.2）。事务写入题目 + 全部用例，任一步失败整体回滚。

- **请求体**（`application/x-www-form-urlencoded`）：

  | 字段 | 类型 | 说明 |
  |------|------|------|
  | `title` | string | 题目标题（非空，≤255） |
  | `description` | string | Markdown 描述（非空） |
  | `difficulty` | string | `easy` / `medium` / `hard` |
  | `input[]` | string（多值） | 第 i 组用例输入（与 `expected[]` 同下标成对） |
  | `expected[]` | string（多值） | 第 i 组用例期望输出 |

- **响应**：
  - `302` + `Location: /admin/problems`：创建成功。
  - `200`：校验失败（缺标题/描述/至少一组非空用例）→ 表单页错误提示，不落库。
  - `500`：数据库不可用 / 事务失败。

### 6.4 `POST /admin/problems/{id}/delete`

物理删除题目（FR-4.3）。`DELETE problems` 经外键 CASCADE 级联删除 testcases 与 submissions（SPEC 4.3.1）。

- **访问控制**：admin；前端二次确认（confirm）后提交。
- **响应**：`302` + `Location: /admin/problems`；id 非法 → `404`；数据库失败 → `500`。

### 6.5 `GET /admin/problems/{id}/edit`

编辑题目表单页（阶段 5 扩展）：预填现有标题 / 难度 / Markdown 描述 / 既有测试用例，可修改并增删用例。

- **访问控制**：admin。
- **响应**：`200` 表单页 HTML；id 非法或题目不存在 → `404`；数据库失败 → `500`。

### 6.6 `POST /admin/problems/{id}/edit`

保存题目修改（标题 / 描述 / 难度 / 测试用例整体替换）。事务：`UPDATE problems` + `DELETE` 旧用例 + `INSERT` 新用例，任一步失败整体回滚。

- **请求体**：字段与 `POST /admin/problems` 完全一致（`title` / `description` / `difficulty` / `input[]` / `expected[]`）。
- **响应**：
  - `302` + `Location: /admin/problems`：保存成功。
  - `200`：校验失败 → 表单页错误提示，并**回填用户已填写内容**，不落库。
  - `404`：id 非法或题目不存在。
  - `500`：数据库不可用 / 事务失败。

---

## 7. 附：已实现接口变更记录

| 接口 | 变更 |
|------|------|
| `GET /` | 由「302 重定向 `/problems`」改为「落地页首页」（阶段 5 大屏落地页） |
| 管理后台四接口 | 由「规划中（404）」改为「已实现」（阶段 5） |
| 管理后台编辑 | 新增 `GET/POST /admin/problems/{id}/edit`（编辑标题/描述/难度/用例） |
| 题目详情页编辑器 | 由 textarea 改为 ace.js（vendored，离线可用） |
