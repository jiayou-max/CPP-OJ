# 基于 Playwright CLI 的 Web 自动化测试文档

> 本文档为 **UI 层（浏览器端到端）** 自动化测试设计，面向仿 LeetCode OJ（cpp-oj-vibe-coding）。
> 与 [接口自动化测试文档.md](./接口自动化测试文档.md)（curl 构造 HTTP 请求，验证接口层）互补：
> 本文档基于 **Playwright CLI**（`@playwright/test` + Chromium）驱动真实浏览器模拟用户操作，
> 覆盖页面渲染、JS 交互、ace.js 编辑器、管理后台 confirm 弹窗、判题结果页渲染等端到端场景。
>
> 测试依据：`SPEC.md`（功能需求 FR / 验收标准 A）、`API.md`（路由与表单约定）、
> `src/routes.cpp`（路由实现）、`www/` 模板与 `src/views/`（页面结构）。

---

## 1. 概述与适用范围

| 项目 | 说明 |
|------|------|
| 被测系统 | cpp-oj（C++ cpp-httplib 后端 + 原生 HTML/CSS/JS 前端，SSR + ace.js） |
| 测试层级 | E2E UI 自动化（Playwright CLI 驱动真实 Chromium） |
| 被测地址 | `http://127.0.0.1:8080`（服务监听 `0.0.0.0:8080`，见 `src/config.h` `kServerPort`） |
| 管理员账号 | `admin` / `admin123`（由 `oj_init_admin` 初始化，FR-1.3） |
| 覆盖功能点 | 首页/静态资源、题库浏览、注册登录登出、判题六状态与资源统计、管理后台增删改查与权限、异常路由与安全 |
| 覆盖验收项 | A1~A15 中可在浏览器层验证的全部条目 |

---

## 2. 测试环境与前置条件

### 2.1 服务端前置（沿用 [接口自动化测试文档.md](./接口自动化测试文档.md) 第 0 节）

1. **构建**：

   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

2. **初始化数据库**（幂等）：

   ```bash
   mysql -uroot < db/schema.sql
   ./build/oj_init_admin admin 'admin123'
   ```

   > 本机 MySQL 实例为 `127.0.0.1:3307`（见 `src/config.h` `kDbPort`）；若实际环境
   > 端口/凭据不同，用 `OJ_DB_HOST / OJ_DB_PORT / OJ_DB_USER / OJ_DB_PASSWORD` 覆盖，
   > 且 schema 初始化与服务启动必须指向**同一个实例**。

3. **启动服务**：

   ```bash
   cd build && setsid nohup ./oj_server >> /tmp/oj_server.log 2>&1 < /dev/null & disown
   sleep 2
   ss -tlnp | grep 8080          # 看到 LISTEN 即成功
   ```

4. **准备题目数据**：题库至少存在一道**含测试用例**的题目用于判题。本机现有：
   - 题 `5`「两数之和」（easy，2 组用例）→ 用于 AC/WA/TLE/MLE/RE/CE 判题用例；
   - 题 `6`「最长回文子串」（medium，**0 组用例**）→ 用于「判题不可用 500」场景。
   - 若环境缺数据，可参考接口测试文档第 0 节 3 的 SQL 补建；也可在自动化用例中
     `W-ADMIN-01` 通过管理后台 UI 创建。

### 2.2 自动化执行机前置（Playwright CLI）

| 依赖 | 说明 | 安装 |
|------|------|------|
| Node.js 16+ | Playwright 运行环境 | 系统安装 / `nvm` |
| `@playwright/test` | 测试框架 + `playwright` CLI | `npm init -y && npm i -D @playwright/test` |
| Chromium | 被测浏览器内核（由 Playwright 管理） | `npx playwright install chromium` |

Playwright CLI 常用命令（本文档自动化实现基于此）：

```bash
npx playwright test                    # 运行全部用例（默认 headless）
npx playwright test --headed           # 有头模式（可视化调试）
npx playwright test --ui               # UI 模式（浏览器内选择/调试/重跑）
npx playwright codegen 127.0.0.1:8080  # 录制操作 → 自动生成用例（快速辅助编写）
npx playwright show-report             # 查看 HTML 测试报告
```

> 浏览器内核选择不受限，本文档用例基于 Chromium 编写，选择器/断言逻辑与浏览器无关。

---

## 3. 通用约定

### 3.1 常量（`helpers.ts` / `playwright.config.ts`）

```ts
export const BASE_URL = 'http://127.0.0.1:8080';
export const ADMIN_USER = 'admin';
export const ADMIN_PASS = 'admin123';
export const JUDGE_PROBLEM_ID = 5;   // 有 2 组用例的判题用题（两数之和）
export const NO_TC_PROBLEM_ID = 6;   // 无测试用例的题（用于 500「判题不可用」）
```

### 3.2 页面元素选择器速查（对应 www/ 模板与 src/views 输出）

| 页面 | 目标 | 选择器（Playwright Locator） |
|------|------|------------------------------|
| 全部 | 页面标题 | `page.title()`（`页面 - CPP OJ`） |
| 导航栏 | 未登录 | `a[href="/login"]` / `a[href="/register"]` |
| 导航栏 | 已登录 | `.navbar-user`（用户名）、`form.navbar-logout button`（登出） |
| 导航栏 | admin 管理入口 | `a[href="/admin/problems"]`（仅 admin 可见，FR-1.4） |
| `/login` `/register` | 表单 | `form.auth-form`，`#username` `#password` `#password2`，`button[type=submit]` |
| `/problems` | 题目行 | `table.problem-table tbody tr`；标题链接 `a.problem-title[href="/problems/{id}"]`；难度徽章 `.badge.badge-{easy|medium|hard}` |
| `/problems/{id}` | 编辑器/提交 | `#code-editor`（ace 容器）、`textarea#code[name=code]`（隐藏）、`#submit-form`、`button`（运行提交） |
| 判题结果 | 总体状态 | `.result-status`（如 `通过 (AC)`） |
| 判题结果 | 统计 | `.result-stats .stat-chip`（`{time_ms} ms`、`{memory_kb} KB`） |
| 判题结果 | 逐用例 | `table.case-table tbody tr`（`td.case-status`、`td` 时间/内存） |
| 判题结果 | 编译日志 | `pre.compile-log`（仅 CE 渲染，FR-3.3） |
| `/admin/problems` | 新增入口 | `a[href="/admin/problems/new"]` |
| `/admin/problems` | 行操作 | `a[href="/admin/problems/{id}/edit"]`、`form.admin-delete[action*="/{id}/delete"] button` |
| 管理表单 | 字段 | `input#title`、`select#difficulty`、`textarea#description`、`.tc-group` 内 `textarea[name="input[]"]` / `textarea[name="expected[]"]` |
| 管理表单 | 用例增删 | `#add-tc`（添加用例）、`.tc-remove`（移除用例） |
| 管理表单 | 错误提示 | `.form-error`（校验失败回填，FR-4.2） |
| 错误页 | 状态码 | `h1.error-code`（`404`/`403`/`500`）、`h2`（页面标题） |

### 3.3 ace.js 编辑器自动化要点

页面初始化时：`ace.edit("code-editor")` 创建编辑器并预填模板（`textarea#code`），
表单提交事件监听器在提交时执行 `ta.value = editor.getValue()` 把编辑器内容同步到
隐藏 textarea（`www/problem_detail.html`）。

因此自动化设置代码的**可靠路径**是「用 `page.evaluate` 直接改 ace 会话 + 点击「运行提交」按钮」：

```ts
export async function setAceCode(page: Page, code: string) {
  await page.evaluate((c) => {
    const el = document.getElementById('code-editor') as any;
    el.env.editor.setValue(c, -1);
  }, code);
}
```

> 说明：点击 `type=submit` 按钮会触发表单 `submit` 事件 → 同步 textarea → POST，
> 是端到端最接近真实用户的提交方式。若直接调用 JS `form.submit()`（如 ace 的
> `Ctrl+Enter` 命令），按浏览器规范**不触发** `submit` 事件，隐藏 textarea 仍为
> 预填模板值——若观察到此现象，属于前端缺陷（见 §8 注意事项）。

### 3.4 等待策略

- Playwright 内置**自动等待**（元素出现/可交互再操作），多数断言无需显式等待。
- 判题为**同步阻塞**（FR-3.5），TLE 单用例 ≤ 2s，涉及判题结果的断言用
  `await expect(locator).toBeVisible({ timeout: 30_000 })` 或
  `locator.waitFor({ timeout: 30_000 })`。
- 表单提交为 302 整页刷新，重定向断言用 `page.waitForURL(/\/admin\/problems/)` 等。

---

## 4. 测试用例设计

### 4.1 M1 首页与静态资源（落地页首页）

| 用例ID | 标题 | 优先级 | 关联需求/验收 |
|--------|------|--------|----------------|
| W-HOME-01 | 游客访问首页成功渲染 | 高 | 阶段 5「大屏落地页」 |
| W-HOME-02 | 首页统计实时数字展示 | 高 | 落地页 hero-stats |
| W-HOME-03 | 首页导航与 CTA 跳转 | 中 | 落地页 |
| W-HOME-04 | 静态资源可加载 | 中 | 阶段 0 / NFR |

#### W-HOME-01 游客访问首页成功渲染
- **前置**：服务已启动。
- **步骤**：
  1. 访问 `GET /`。
- **预期**：
  1. 页面标题为 `CPP OJ - 在线评测系统`。
  2. 可见品牌 `CPP OJ`、hero 标题「让每一行代码都被严谨验证」。
  3. 页面含「开始刷题」按钮与「判题内核 / 细粒度结果 / 数据持久化 / 嵌入式编辑器」特性区。

#### W-HOME-02 首页统计实时数字展示
- **步骤**：访问 `GET /`，读取 `.hero-stats` 三个 `.stat-value`。
- **预期**：三个统计值均为**非负整数**，且与 `mysql ... cpp_oj -e "SELECT COUNT(*) FROM problems/users/submissions"` 一致（数据库不可用时渲染为 `--` 而不 500）。

#### W-HOME-03 首页导航与 CTA 跳转
- **步骤**：
  1. 访问 `/`，点击「开始刷题」。
- **预期**：URL 变为 `/problems`，进入题库页（标题 `题库 - CPP OJ`）。
- **步骤（续）**：未登录时点导航「登录」「注册」分别进入对应表单页；已登录时导航显示用户名与「登出」，admin 额外显示「管理」。

#### W-HOME-04 静态资源可加载
- **步骤**：通过 `request` fixture（或直接访问）：
  - `/static/css/style.css` → 200，`Content-Type: text/css`
  - `/static/js/ace/ace.min.js` → 200，`application/javascript`
  - `/static/js/ace/mode-c_cpp.min.js`、`/static/js/ace/theme-monokai.min.js` → 200
- **预期**：全部 200，返回非空内容。

---

### 4.2 M2 题库浏览与做题页

| 用例ID | 标题 | 优先级 | 关联需求/验收 |
|--------|------|--------|----------------|
| W-PROB-01 | 题库列表展示全部题目 | 高 | FR-2.1 / A2 |
| W-PROB-02 | 题目详情页结构完整 | 高 | FR-2.2 / A2 |
| W-PROB-03 | 详情页 Markdown 渲染 | 中 | FR-2.2 |
| W-PROB-04 | 不存在题目返回 404 | 高 | FR-2.3 / A14 |
| W-PROB-05 | 非法 id 返回 404 | 中 | FR-2.3 / A14 |

#### W-PROB-01 题库列表展示全部题目
- **前置**：题库非空。
- **步骤**：
  1. 访问 `GET /problems`。
  2. 遍历 `table.problem-table tbody tr`。
- **预期**：
  1. 页面标题 `题库 - CPP OJ`，头部「共 N 题」数字 > 0。
  2. 每题一行：编号、标题链接（指向 `/problems/{id}`）、难度徽章（`简单/中等/困难`）。
  3. 标题链接数量 = 数据库 `problems` 行数。

#### W-PROB-02 题目详情页结构完整
- **步骤**：
  1. 从题库列表点击题 `5` 标题链接进入详情页。
- **预期**：
  1. 页面标题 `两数之和 - CPP OJ`，题头含 `#5` 与难度徽章 `简单`。
  2. 描述区 `.markdown-body` 非空（Markdown 已转 HTML）。
  3. 右侧编辑器存在：`#code-editor`（ace 容器）、隐藏 `textarea#code[name=code]` 预填 C++ 模板、`#submit-form` 的 `action="/problems/5/submit"`、按钮文案「运行提交」。

#### W-PROB-03 详情页 Markdown 渲染
- **步骤**：访问题 `5` 详情页，检查 `.markdown-body` 内 `<h2>`/`<p>`/`<pre>` 等由 Markdown 生成的标签。
- **预期**：Markdown 标题、段落、代码块等被正确渲染为 HTML（而非原文 `#` 字符）。

#### W-PROB-04 不存在题目返回 404
- **步骤**：访问 `GET /problems/999`。
- **预期**：页面标题 `404 - CPP OJ`，`h1.error-code` 为 `404`，文案含「题目不存在」。

#### W-PROB-05 非法 id 返回 404
- **步骤**：访问 `GET /problems/abc`。
- **预期**：页面标题 `404 - CPP OJ`，文案含「页面不存在」。

---

### 4.3 M3 用户系统（注册 / 登录 / 登出 / 会话）

| 用例ID | 标题 | 优先级 | 关联需求/验收 |
|--------|------|--------|----------------|
| W-AUTH-01 | 注册成功并自动登录 | 高 | FR-1.1 / A3 |
| W-AUTH-02 | 重复用户名被拒绝 | 高 | FR-1.1 / A3 |
| W-AUTH-03 | 保留用户名 admin 被拒绝 | 高 | FR-1.3 / A3 |
| W-AUTH-04 | 两次密码不一致被拒绝 | 高 | FR-1.1 / 参数校验 |
| W-AUTH-05 | 非法用户名/密码被拒绝 | 中 | FR-1.1 / 参数校验 |
| W-AUTH-06 | 登录成功 | 高 | FR-1.2 / A3 |
| W-AUTH-07 | 登录失败统一文案（防枚举） | 中 | NFR-3 |
| W-AUTH-08 | 已登录访问登录/注册页重定向 | 中 | 路由约定 |
| W-AUTH-09 | 登出成功并清会话 | 高 | FR-1.1 |

> 每个用例的用户名使用 `web_<时间戳>` 动态生成，避免重复运行冲突。

#### W-AUTH-01 注册成功并自动登录
- **步骤**：
  1. 访问 `GET /register`，填入唯一用户名 `web_{ts}`、密码 `secret123`、确认密码 `secret123`，提交。
- **预期**：
  1. 302 重定向至 `/problems`（浏览器地址为 `/problems`）。
  2. 导航栏出现用户名 `web_{ts}` 与「登出」按钮（说明已自动登录）。
  3. 数据库 `users` 表新增该用户，`role='user'`。

#### W-AUTH-02 重复用户名被拒绝
- **步骤**：使用与 W-AUTH-01 相同的用户名再次注册。
- **预期**：返回 200 注册页，`.form-error` 提示「用户名已存在，请换一个。」；`users` 表无重复记录。

#### W-AUTH-03 保留用户名 admin 被拒绝
- **步骤**：用 `username=admin`、`password=admin123` 注册。
- **预期**：200 注册页，提示「用户名已存在，请换一个。」（FR-1.3：唯一键 + 白名单双重保证）。

#### W-AUTH-04 两次密码不一致被拒绝
- **步骤**：注册页填写 `password=secret123`、`password2=secret456`，提交。
- **预期**：200 注册页，提示「两次输入的密码不一致。」

#### W-AUTH-05 非法用户名/密码被拒绝
- **步骤**：注册页填写 `username=b@d!name`（含非法字符）提交。
- **预期**：200 注册页，提示「用户名仅允许字母/数字/下划线（1~64 位），密码长度 6~64。」；`users` 无新增。
- **另**：密码长度 < 6（如 `123`）同样被拒。

#### W-AUTH-06 登录成功
- **步骤**：
  1. 访问 `GET /login`，填入已注册用户与正确密码，提交。
- **预期**：302 → `/problems`；导航栏显示用户名与「登出」。

#### W-AUTH-07 登录失败统一文案（防枚举）
- **步骤**：
  1. 已存在用户 + 错误密码登录。
  2. 不存在用户 + 任意密码登录。
- **预期**：两种场景均 200 登录页，且提示文案一致「用户名或密码错误。」（不区分账号不存在/密码错误，防用户名枚举）。
  另：用户名/密码格式非法时提示「用户名或密码格式不正确。」。

#### W-AUTH-08 已登录访问登录/注册页重定向
- **步骤**：已登录状态下访问 `GET /login` 与 `GET /register`。
- **预期**：均 302 重定向至 `/problems`，不显示登录表单。

#### W-AUTH-09 登出成功并清会话
- **步骤**：
  1. 已登录状态下点击导航栏「登出」（`form.navbar-logout button`）。
- **预期**：
  1. POST `/logout` → 302 回 `/problems`；导航栏恢复「登录 / 注册」，不再显示用户名。
  2. **会话已失效**：登出后再访问受保护接口 `/admin/problems` 被 302 重定向至 `/login`（原 Cookie 已销毁）。
- **另**：未登录直接 POST `/logout` 亦 302 至 `/login`（RequireLogin 兜底）。

---

### 4.4 M4 判题内核（六状态 + 资源统计 + 落库）

| 用例ID | 标题 | 优先级 | 关联需求/验收 |
|--------|------|--------|----------------|
| W-JUDGE-01 | 游客提交重定向登录页 | 高 | FR-1.5 / A3 |
| W-JUDGE-02 | AC：正确代码全部通过 | 高 | FR-3.2/3.3/3.4 / A6 |
| W-JUDGE-03 | WA：答案错误可见失败用例 | 高 | FR-3.3 / A7 |
| W-JUDGE-04 | TLE：死循环有界返回 | 高 | FR-3.6 / A8 / SPEC 5 |
| W-JUDGE-05 | MLE：内存超限 | 高 | FR-3.6 / A11 |
| W-JUDGE-06 | RE：运行崩溃 | 高 | FR-3.3 / A10 |
| W-JUDGE-07 | CE：编译错误附日志 | 高 | FR-3.3 / A9 |
| W-JUDGE-08 | 结果页资源统计展示 | 高 | FR-3.4 / A12 |
| W-JUDGE-09 | 编辑器 Ctrl+Enter 提交 | 中 | 详情页交互 |
| W-JUDGE-10 | 提交落库校验 | 高 | FR-5.1 / A13 |
| W-JUDGE-11 | 题目无用例判题不可用 500 | 中 | SPEC 5 / A14 |

> 判题样本代码均已在本机实测命中对应状态（见 §5 判题样本）。登录身份统一用
> 普通测试用户 `judge_{ts}`（注册并自动登录）。

#### W-JUDGE-01 游客提交重定向登录页
- **前置**：浏览器未登录（Playwright 每个 test 默认新 context，天然未登录）。
- **步骤**：访问 `GET /problems/5`，点击「运行提交」。
- **预期**：302 → `/login`，浏览器停在登录页；不进入判题流程（FR-1.5：登录校验先于一切）。

#### W-JUDGE-02 AC：正确代码全部通过
- **步骤**：
  1. 登录后访问题 `5`，在 ace 编辑器填入两数之和正确解（§5 样本 ①），点「运行提交」。
- **预期**：
  1. 结果页标题 `判题结果：两数之和 - CPP OJ`。
  2. `.result-status` 为「通过 (AC)」；逐用例表 `case-table` 2 行全为 AC。
  3. 统计区显示 `{time_ms} ms` 与 `{memory_kb} KB`（均为非负）。

#### W-JUDGE-03 WA：答案错误可见失败用例
- **步骤**：登录后提交错误解（§5 样本 ②，恒输出 `0 0`）。
- **预期**：`.result-status` 为「答案错误 (WA)」；逐用例表至少一行状态为 WA，且能通过用例号 `#1`/`#2` 定位到首个失败用例（FR-3.4 / A7）。

#### W-JUDGE-04 TLE：死循环有界返回
- **步骤**：
  1. 登录后提交死循环代码（§5 样本 ③）。
  2. 记录提交到结果页出现耗时 T。
- **预期**：
  1. `.result-status` 为「运行超时 (TLE)」，逐用例对应行 TLE。
  2. **有界返回**：T ≤ 15s（编译 ≤ 10s + 每用例 2s + 开销），请求不挂死。
  3. **服务器未卡死**：随后访问 `/problems` 正常 200（对照 FR-3.5 / A8「线程不被卡死」）。

#### W-JUDGE-05 MLE：内存超限
- **步骤**：登录后提交持续分配 300MB 的代码（§5 样本 ④）。
- **预期**：`.result-status` 为「内存超限 (MLE)」（256MB RLIMIT_AS，FR-3.6 / A11）。

#### W-JUDGE-06 RE：运行崩溃
- **步骤**：登录后提交除零代码（§5 样本 ⑤）。
- **预期**：`.result-status` 为「运行错误 (RE)」（崩溃信号 → 非零退出，A10）。

#### W-JUDGE-07 CE：编译错误附日志
- **步骤**：登录后提交非法代码（§5 样本 ⑥）。
- **预期**：`.result-status` 为「编译错误 (CE)」；页面出现 `<h2>编译错误日志</h2>` 与 `pre.compile-log`，内含 g++ stderr（如 `error:` 行），日志为转义后的完整内容（FR-3.3 / A9）。

#### W-JUDGE-08 结果页资源统计展示
- **步骤**：任一判题结果页，读取 `.result-stats .stat-chip`。
- **预期**：显示 `{time_ms} ms`、`{memory_kb} KB` 两个数值；逐用例表每行亦有时间/内存列（A12）。

#### W-JUDGE-09 编辑器 Ctrl+Enter 提交
- **步骤**：
  1. 登录后打开题 `5`，ace 中填入正确解（§5 样本 ①）。
  2. 焦点置于编辑器，按 `Ctrl+Enter`（Mac 为 `Command+Enter`）。
- **预期**：表单提交并进入判题结果页，总体 AC。
  > 若结果提交的是**预填模板**而非编辑内容（即结果非 AC），说明前端存在缺陷：
  > ace 的 `Ctrl+Enter` 命令直接调用 `form.submit()` 未触发 `submit` 事件同步
  > 隐藏 textarea（见 §8）。

#### W-JUDGE-10 提交落库校验
- **步骤**：完成 W-JUDGE-02 后查库。
- **预期**：
  ```sql
  SELECT id,user_id,problem_id,status,time_ms,memory_kb,code
  FROM submissions ORDER BY id DESC LIMIT 1;
  ```
  `user_id` 为当前登录用户、`problem_id=5`、`status='ac'`、`time_ms/memory_kb>0`、`code` 与提交内容一致（FR-5.1 / A13）。

#### W-JUDGE-11 题目无用例判题不可用 500
- **前置**：存在无测试用例的题目（本机题 `6`，0 用例）。
- **步骤**：登录后对题 `6` 提交任意代码。
- **预期**：500 错误页：`h1.error-code` = `500`，标题「判题不可用」，文案「该题目暂无测试用例，请联系管理员。」（SPEC 5）。

---

### 4.5 M5 管理后台（权限 / 增删改查）

| 用例ID | 标题 | 优先级 | 关联需求/验收 |
|--------|------|--------|----------------|
| W-ADMIN-01 | 游客访问管理后台重定向登录 | 高 | FR-4.1 / A5 |
| W-ADMIN-02 | 普通用户访问管理后台 403 | 高 | FR-4.1/4.4 / A5 |
| W-ADMIN-03 | 普通用户无管理入口 | 中 | FR-4.4 |
| W-ADMIN-04 | admin 进入管理后台列表 | 高 | FR-4.1 / A4 |
| W-ADMIN-05 | 新增题目（含多组用例） | 高 | FR-4.2 / A4 |
| W-ADMIN-06 | 新增题目校验失败不落库 | 高 | FR-4.2 / 参数校验 |
| W-ADMIN-07 | 编辑题目预填与保存 | 高 | FR-4.2 |
| W-ADMIN-08 | 删除题目二次确认（取消） | 高 | FR-4.3 |
| W-ADMIN-09 | 删除题目物理删除（确认） | 高 | FR-4.3 / A4 |
| W-ADMIN-10 | 新增/编辑表单用例动态增删 | 中 | FR-4.2 |

> 管理用例统一以 `admin` / `admin123` 登录。测试创建的题目标题使用
> `webadmin_{ts}` 前缀，删除类用例自建自删，避免污染题库。

#### W-ADMIN-01 游客访问管理后台重定向登录
- **步骤**：未登录直接访问 `GET /admin/problems`。
- **预期**：302 → `/login`（RequireAdmin：未登录先回登录页，FR-4.1）。

#### W-ADMIN-02 普通用户访问管理后台 403
- **步骤**：
  1. 注册/登录普通用户 `webuser_{ts}`。
  2. 访问 `GET /admin/problems` 及 `GET /admin/problems/new`。
- **预期**：403 页面：`h1.error-code` = `403`，标题「访问被拒绝」，文案「需要管理员权限。」（A5）。

#### W-ADMIN-03 普通用户无管理入口
- **步骤**：以普通用户浏览任意页，检查导航栏。
- **预期**：导航栏无 `a[href="/admin/problems"]` 管理链接，仅用户名 + 登出（FR-4.4）。

#### W-ADMIN-04 admin 进入管理后台列表
- **步骤**：以 admin 登录，点击导航「管理」或直接访问 `/admin/problems`。
- **预期**：200，页面标题 `题目管理 - CPP OJ`，`h1` 为「题目管理」，含「新增题目」按钮；导航栏有「管理」入口。

#### W-ADMIN-05 新增题目（含多组用例）
- **步骤**：
  1. 进入 `/admin/problems/new`。
  2. 填标题 `webadmin_{ts}`、难度 `中等`、Markdown 描述、2 组用例（`input[]`/`expected[]` 各 2 组）。
  3. 提交。
- **预期**：
  1. 302 → `/admin/problems`；列表中可见新标题与其难度徽章「中等」。
  2. 题库页 `/problems` 也可见该题；点击进入详情页可见描述与用例（判题可命中）。
  3. 数据库 `problems`/`testcases` 各新增对应记录（事务落库，A4）。

#### W-ADMIN-06 新增题目校验失败不落库
- **步骤**：在新增表单提交空标题（或空描述 / 全空用例）。
- **预期**：200 表单页，`.form-error` 提示「请填写标题、Markdown 描述，并至少提供一组「输入/期望输出」均非空的测试用例。」；`problems` 无新增（FR-4.2 参数校验）。

#### W-ADMIN-07 编辑题目预填与保存
- **步骤**：
  1. 先创建题目 `webadmin_{ts}`，进入其 `/admin/problems/{id}/edit`。
  2. 断言表单预填：`#title` 值为原标题、难度选中、`#description` 回填描述、用例区含既有 `input[]`/`expected[]` 内容。
  3. 修改标题为 `webadmin_{ts}_edited`、难度 `困难`、替换用例为 1 组，保存。
- **预期**：
  1. 302 → `/admin/problems`；列表标题更新为 `webadmin_{ts}_edited`，难度徽章为「困难」。
  2. 重新进入编辑页，字段与用例为修改后的值（用例整体替换）。
  3. `testcases` 旧用例已删、新用例已插（编辑为事务替换，FR-4.2）。

#### W-ADMIN-08 删除题目二次确认（取消）
- **步骤**：
  1. 创建题目 `webadmin_{ts}_del` 后进入管理列表。
  2. 点击其行内「删除」按钮，在弹出的 `confirm` 弹窗点「取消」。
- **预期**：弹窗关闭，未发生 POST 删除；列表仍含该题，`/problems/{id}` 仍 200（FR-4.3 前端二次确认）。

#### W-ADMIN-09 删除题目物理删除（确认）
- **步骤**：
  1. 承接 W-ADMIN-08（或新创建），在管理列表点「删除」，弹窗点「确定」。
- **预期**：
  1. POST `/admin/problems/{id}/delete` → 302 回 `/admin/problems`；列表不再含该题。
  2. `/problems/{id}` → 404「题目不存在」。
  3. 数据库该题与其 `testcases` 已物理删除（CASCADE，FR-4.3「物理删除，无回收站」）。

#### W-ADMIN-10 新增/编辑表单用例动态增删
- **步骤**：在新增表单点击「+ 添加用例」，再点「移除」。
- **预期**：`.tc-group` 数量随点击 +1/-1；仅剩 1 组时「移除」按钮隐藏（模板 JS `syncRemoveButtons`）。

---

### 4.6 M6 异常路径与安全

| 用例ID | 标题 | 优先级 | 关联需求/验收 |
|--------|------|--------|----------------|
| W-ERR-01 | 未匹配路由 404 | 高 | A14 / 统一错误处理器 |
| W-ERR-02 | 输入 HTML 转义防 XSS | 高 | NFR-3 |
| W-ERR-03 | 会话 Cookie HttpOnly | 中 | NFR-3 / API 5.1 |
| W-ERR-04 | MySQL 不可用时 500 | 中 | SPEC 5 / A14 |

#### W-ERR-01 未匹配路由 404
- **步骤**：访问 `GET /does-not-exist`。
- **预期**：404 页：`h1.error-code` = `404`，标题「页面不存在」（`set_error_handler` 统一渲染）。

#### W-ERR-02 输入 HTML 转义防 XSS
- **步骤**：
  1. admin 新增题目，标题填 `<script>alert('xss')</script>`，描述填含 `<img onerror=...>` 的 Markdown。
  2. 查看管理列表、题库列表、题目详情页。
- **预期**：页面中以**文本**显示原样尖括号，`.problem-title`/标题处无 `script` 元素被执行；浏览器无 `alert` 弹出（NFR-3 输出转义）。检查后删除该题。

#### W-ERR-03 会话 Cookie HttpOnly
- **步骤**：登录成功后，用 `page.evaluate(() => document.cookie)` 读取 Cookie。
- **预期**：`oj_session` 不出现在 `document.cookie`（HttpOnly）；响应头 `Set-Cookie` 含 `HttpOnly; SameSite=Lax; Path=/`（无 Secure，NFR-5）。

#### W-ERR-04 MySQL 不可用时 500
- **步骤**：停止 MySQL 后访问 `/problems`、提交判题（或注入 DB 故障）。
- **预期**：对应页面返回 500 错误页（`h1.error-code` = `500`，标题「服务器内部错误」）；恢复 MySQL 后服务自动恢复。恢复后**重启 `oj_server`**（会话内存存储，重启即失效）。
  > 此用例影响面大，建议放到最后单独执行，并确认 MySQL 已恢复。

---

### 4.7 M7 并发安全（可选）

| 用例ID | 标题 | 优先级 | 关联需求/验收 |
|--------|------|--------|----------------|
| W-CONC-01 | 双并发提交结果互不污染 | 中 | FR-3.5 / A15 |

#### W-CONC-01 双并发提交结果互不污染
- **步骤**：
  1. 用 `browser.newContext()` 开两个隔离的浏览器上下文，分别登录两个不同用户。
  2. 同时（差 < 0.5s，用 `Promise.all`）提交：窗口 A 提交 AC 代码（样本 ①），窗口 B 提交 CE 代码（样本 ⑥）。
- **预期**：
  1. 两请求均**有界返回**（信号量限流 ≤ 2，并发排队不挂死）。
  2. 窗口 A 结果 AC、窗口 B 结果 CE，结果与各自代码对应，无交叉污染（随机临时目录隔离，A15）。

---

## 5. 判题样本代码（已实测命中）

| 样本 | 期望 | 代码 |
|------|------|------|
| ① AC | 通过 (AC) | `#include <iostream>`<br>`#include <vector>`<br>`using namespace std;`<br>`int main(){int n;cin>>n;vector<int>a(n);for(int i=0;i<n;i++)cin>>a[i];int t;cin>>t;for(int i=0;i<n;i++)for(int j=i+1;j<n;j++)if(a[i]+a[j]==t){cout<<i<<" "<<j<<"\n";return 0;}return 0;}` |
| ② WA | 答案错误 (WA) | `#include <iostream>`<br>`using namespace std; int main(){int n;cin>>n;int x;for(int i=0;i<n;i++)cin>>x;cout<<"0 0\n";return 0;}` |
| ③ TLE | 运行超时 (TLE) | `#include <iostream>`<br>`int main(){ while(1){} return 0; }` |
| ④ MLE | 内存超限 (MLE) | `#include <iostream>`<br>`int main(){ size_t n=300*1024*1024; char* p=new char[n]; for(size_t i=0;i<n;i+=4096) p[i]=1; int x; std::cin>>x; delete[] p; return 0; }` |
| ⑤ RE | 运行错误 (RE) | `#include <iostream>`<br>`int main(){ int x=1/0; std::cout<<x; return 0; }` |
| ⑥ CE | 编译错误 (CE) | `this is not valid c++` |

> ①② 基于题 `5`（两数之和：`n / 数组 / target` → 输出 `i j`，2 组用例）；若判题用题不同，
> 需按题目输入输出格式调整样本。

---

## 6. 自动化实现（TypeScript + @playwright/test）

> 以下为可运行实现，对应第 4 节用例。文件布局建议：

```
web_auto/
├── playwright.config.ts    # Playwright CLI 配置（baseURL/并行/报告）
├── helpers.ts              # 常量 + 登录/注册/提交/等待 封装
├── home.spec.ts            # M1
├── problems.spec.ts        # M2
├── auth.spec.ts            # M3
├── judge.spec.ts           # M4
├── admin.spec.ts           # M5
├── errors.spec.ts          # M6
└── concurrency.spec.ts     # M7
```

### 6.1 playwright.config.ts

```ts
import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: '.',
  timeout: 60_000,                       // 单个用例超时
  fullyParallel: true,                   // 多 worker 并行（注意判题信号量 ≤ 2，可调 workers: 1）
  workers: 2,
  reporter: [['html', { open: 'never' }], ['list']],
  use: {
    baseURL: 'http://127.0.0.1:8080',
    headless: true,
    viewport: { width: 1440, height: 900 },
    trace: 'retain-on-failure',          // 失败保留 trace，npx playwright show-trace 查看
    screenshot: 'only-on-failure',
  },
});
```

> 判题为全局信号量限流（并发 ≤ 2，FR-3.5），MLE/TLE 用例若与其它判题用例并行会排队，
> 全量执行建议 `workers: 1`（串行）或将判题模块单独 `--project` 串行运行。

### 6.2 helpers.ts

```ts
import { Page } from '@playwright/test';

export const BASE_URL = 'http://127.0.0.1:8080';
export const ADMIN_USER = 'admin';
export const ADMIN_PASS = 'admin123';
export const JUDGE_PROBLEM_ID = 5;   // 有测试用例的判题用题
export const NO_TC_PROBLEM_ID = 6;   // 无测试用例的题

export const ts = () => String(Math.floor(Date.now() / 1000));

export async function register(page: Page, username: string, password = 'secret123') {
  await page.goto('/register');
  await page.locator('#username').fill(username);
  await page.locator('#password').fill(password);
  await page.locator('#password2').fill(password);
  await page.locator('form button[type=submit]').click();
  await page.waitForURL(/\/problems/);
}

export async function login(page: Page, username: string, password: string) {
  await page.goto('/login');
  await page.locator('#username').fill(username);
  await page.locator('#password').fill(password);
  await page.locator('form button[type=submit]').click();
  await page.waitForURL(/\/problems/);
}

export async function logout(page: Page) {
  await page.locator('form.navbar-logout button').click();
  await page.waitForURL(/\/problems/);
}

// ace.js 编辑器：JS 直接改会话（详见 §3.3）
export async function setAceCode(page: Page, code: string) {
  await page.evaluate((c) => {
    const el = document.getElementById('code-editor') as any;
    el.env.editor.setValue(c, -1);
  }, code);
}

export async function submitProblem(page: Page, problemId: number, code: string,
                                    waitCss = '.result-status') {
  await page.goto(`/problems/${problemId}`);
  await setAceCode(page, code);
  await page.locator('#submit-form button[type=submit]').click();
  await page.locator(waitCss).waitFor({ timeout: 30_000 });
}

export async function resultStatus(page: Page) {
  return (await page.locator('.result-status').textContent())!.trim();
}
```

### 6.3 home.spec.ts（M1）

```ts
import { test, expect } from '@playwright/test';
import { BASE_URL } from './helpers';

test('W-HOME-01 游客访问首页成功渲染', async ({ page }) => {
  await page.goto('/');
  await expect(page).toHaveTitle('CPP OJ - 在线评测系统');
  await expect(page.locator('.hero-title')).toContainText('严谨验证');
  await expect(page.locator('a[href="/problems"]')).toContainText('开始刷题');
});

test('W-HOME-02 首页统计实时数字展示', async ({ page }) => {
  await page.goto('/');
  const vals = await page.locator('.hero-stats .stat-value').allTextContents();
  expect(vals).toHaveLength(3);
  expect(vals.every((v) => /^\d+$/.test(v.trim()))).toBe(true);
});

test('W-HOME-03 首页 CTA 跳转', async ({ page }) => {
  await page.goto('/');
  await page.locator('a.btn-primary[href="/problems"]').click();
  await expect(page).toHaveURL(/\/problems/);
});

test('W-HOME-04 静态资源可加载', async ({ request }) => {
  for (const p of ['/static/css/style.css', '/static/js/ace/ace.min.js',
                   '/static/js/ace/mode-c_cpp.min.js', '/static/js/ace/theme-monokai.min.js']) {
    const res = await request.get(BASE_URL + p);
    expect(res.status()).toBe(200);
    expect((await res.body()).length).toBeGreaterThan(0);
  }
});
```

### 6.4 problems.spec.ts（M2）

```ts
import { test, expect } from '@playwright/test';
import { JUDGE_PROBLEM_ID } from './helpers';

test('W-PROB-01 题库列表展示全部题目', async ({ page }) => {
  await page.goto('/problems');
  await expect(page).toHaveTitle('题库 - CPP OJ');
  await expect(page.locator('table.problem-table tbody tr').first()).toBeVisible();
  const rows = page.locator('table.problem-table tbody tr');
  expect(await rows.count()).toBeGreaterThanOrEqual(1);
});

test('W-PROB-02 题目详情页结构完整', async ({ page }) => {
  await page.goto(`/problems/${JUDGE_PROBLEM_ID}`);
  await expect(page).toHaveTitle(new RegExp('CPP OJ'));
  await expect(page.locator('#code-editor')).toBeVisible();
  await expect(page.locator('#submit-form')).toHaveAttribute(
    'action', `/problems/${JUDGE_PROBLEM_ID}/submit`);
  await expect(page.locator('textarea#code')).not.toHaveValue('');   // 预填模板
  await expect(page.locator('.markdown-body')).not.toBeEmpty();
});

test('W-PROB-03 详情页 Markdown 渲染', async ({ page }) => {
  await page.goto(`/problems/${JUDGE_PROBLEM_ID}`);
  await expect(page.locator('.markdown-body h1, .markdown-body h2').first()).toBeVisible();
});

test('W-PROB-04 不存在题目返回 404', async ({ page }) => {
  await page.goto('/problems/999');
  await expect(page).toHaveTitle('404 - CPP OJ');
  await expect(page.locator('h1.error-code')).toHaveText('404');
  await expect(page.locator('h2')).toContainText('题目不存在');
});

test('W-PROB-05 非法 id 返回 404', async ({ page }) => {
  await page.goto('/problems/abc');
  await expect(page).toHaveTitle('404 - CPP OJ');
  await expect(page.locator('h2')).toContainText('页面不存在');
});
```

### 6.5 auth.spec.ts（M3）

```ts
import { test, expect } from '@playwright/test';
import { register, login, logout, ts } from './helpers';

test('W-AUTH-01 注册成功并自动登录', async ({ page }) => {
  const u = `web_${ts()}`;
  await register(page, u);
  await expect(page.locator('.navbar-user')).toHaveText(u);
  await expect(page.locator('form.navbar-logout button')).toBeVisible();
});

test('W-AUTH-02 重复用户名被拒绝', async ({ page }) => {
  const u = `webdup_${ts()}`;
  await register(page, u);
  await logout(page);
  await page.goto('/register');
  await page.locator('#username').fill(u);
  await page.locator('#password').fill('secret123');
  await page.locator('#password2').fill('secret123');
  await page.locator('form button[type=submit]').click();
  await expect(page.locator('.form-error')).toContainText('用户名已存在');
});

test('W-AUTH-03 保留用户名 admin 被拒绝', async ({ page }) => {
  await page.goto('/register');
  await page.locator('#username').fill('admin');
  await page.locator('#password').fill('admin123');
  await page.locator('#password2').fill('admin123');
  await page.locator('form button[type=submit]').click();
  await expect(page.locator('.form-error')).toContainText('用户名已存在');
});

test('W-AUTH-04 两次密码不一致被拒绝', async ({ page }) => {
  await page.goto('/register');
  await page.locator('#username').fill(`mismatch_${ts()}`);
  await page.locator('#password').fill('secret123');
  await page.locator('#password2').fill('secret456');
  await page.locator('form button[type=submit]').click();
  await expect(page.locator('.form-error')).toContainText('两次输入的密码不一致');
});

test('W-AUTH-05 非法用户名被拒绝', async ({ page }) => {
  await page.goto('/register');
  await page.locator('#username').fill('b@d!name');
  await page.locator('#password').fill('secret123');
  await page.locator('#password2').fill('secret123');
  await page.locator('form button[type=submit]').click();
  await expect(page.locator('.form-error')).toContainText('用户名仅允许字母/数字/下划线');
});

test('W-AUTH-06 登录成功', async ({ page }) => {
  const u = `loginok_${ts()}`;
  await register(page, u);
  await logout(page);
  await login(page, u, 'secret123');
  await expect(page.locator('.navbar-user')).toHaveText(u);
});

test('W-AUTH-07 登录失败统一文案（防枚举）', async ({ page }) => {
  const u = `loginbad_${ts()}`;
  await register(page, u);
  await logout(page);
  await page.goto('/login');
  await page.locator('#username').fill(u);          // 已存在用户 + 错误密码
  await page.locator('#password').fill('wrongpass');
  await page.locator('form button[type=submit]').click();
  await expect(page.locator('.form-error')).toContainText('用户名或密码错误。');
  await page.locator('#username').fill(`nouser_${ts()}`);  // 不存在用户
  await page.locator('#password').fill('whatever1');
  await page.locator('form button[type=submit]').click();
  await expect(page.locator('.form-error')).toContainText('用户名或密码错误。');
});

test('W-AUTH-08 已登录访问登录/注册页重定向', async ({ page }) => {
  const u = `redi_${ts()}`;
  await register(page, u);
  await page.goto('/login');
  await page.waitForURL(/\/problems/);
  await page.goto('/register');
  await page.waitForURL(/\/problems/);
});

test('W-AUTH-09 登出成功并清会话', async ({ page }) => {
  const u = `logout_${ts()}`;
  await register(page, u);
  await logout(page);
  await expect(page.locator('nav a[href="/login"]')).toContainText('登录');
  await page.goto('/admin/problems');      // 会话已失效 → 302 登录页
  await page.waitForURL(/\/login/);
});
```

### 6.6 judge.spec.ts（M4）

```ts
import { test, expect, Page } from '@playwright/test';
import { register, submitProblem, resultStatus, setAceCode, ts,
         JUDGE_PROBLEM_ID, NO_TC_PROBLEM_ID } from './helpers';

const AC = `#include <iostream>
#include <vector>
using namespace std;
int main(){int n;cin>>n;vector<int>a(n);for(int i=0;i<n;i++)cin>>a[i];int t;cin>>t;for(int i=0;i<n;i++)for(int j=i+1;j<n;j++)if(a[i]+a[j]==t){cout<<i<<" "<<j<<"\\n";return 0;}return 0;}`;

const WA = `#include <iostream>
using namespace std; int main(){int n;cin>>n;int x;for(int i=0;i<n;i++)cin>>x;cout<<"0 0\\n";return 0;}`;

const TLE = `#include <iostream>
int main(){ while(1){} return 0; }`;

const MLE = `#include <iostream>
int main(){ size_t n=300*1024*1024; char* p=new char[n]; for(size_t i=0;i<n;i+=4096) p[i]=1; int x; std::cin>>x; delete[] p; return 0; }`;

const RE = `#include <iostream>
int main(){ int x=1/0; std::cout<<x; return 0; }`;

const CE = 'this is not valid c++';

async function loggedIn(page: Page) {
  await register(page, `judge_${ts()}`);
}

test('W-JUDGE-01 游客提交重定向登录页', async ({ page }) => {
  await page.goto(`/problems/${JUDGE_PROBLEM_ID}`);
  await setAceCode(page, AC);
  await page.locator('#submit-form button[type=submit]').click();
  await page.waitForURL(/\/login/);
});

test('W-JUDGE-02 AC：正确代码全部通过', async ({ page }) => {
  await loggedIn(page);
  await submitProblem(page, JUDGE_PROBLEM_ID, AC);
  await expect(page.locator('.result-status')).toHaveText('通过 (AC)');
  const rows = await page.locator('table.case-table tbody tr').allTextContents();
  expect(rows).toHaveLength(2);
  expect(rows.every((r) => r.includes('AC'))).toBe(true);
});

test('W-JUDGE-03 WA：可见失败用例', async ({ page }) => {
  await loggedIn(page);
  await submitProblem(page, JUDGE_PROBLEM_ID, WA);
  await expect(page.locator('.result-status')).toHaveText('答案错误 (WA)');
  const rows = await page.locator('table.case-table tbody tr').allTextContents();
  expect(rows.some((r) => r.includes('WA'))).toBe(true);
});

test('W-JUDGE-04 TLE：死循环有界返回', async ({ page }) => {
  await loggedIn(page);
  const t0 = Date.now();
  await submitProblem(page, JUDGE_PROBLEM_ID, TLE);
  await expect(page.locator('.result-status')).toHaveText('运行超时 (TLE)');
  expect(Date.now() - t0).toBeLessThan(15_000);          // 有界返回
  await page.goto('/problems');                          // 服务器未卡死
  await expect(page).toHaveTitle('题库 - CPP OJ');
});

test('W-JUDGE-05 MLE：内存超限', async ({ page }) => {
  await loggedIn(page);
  await submitProblem(page, JUDGE_PROBLEM_ID, MLE);
  await expect(page.locator('.result-status')).toHaveText('内存超限 (MLE)');
});

test('W-JUDGE-06 RE：运行崩溃', async ({ page }) => {
  await loggedIn(page);
  await submitProblem(page, JUDGE_PROBLEM_ID, RE);
  await expect(page.locator('.result-status')).toHaveText('运行错误 (RE)');
});

test('W-JUDGE-07 CE：编译错误附日志', async ({ page }) => {
  await loggedIn(page);
  await submitProblem(page, JUDGE_PROBLEM_ID, CE);
  await expect(page.locator('.result-status')).toHaveText('编译错误 (CE)');
  await expect(page.locator('pre.compile-log')).toBeVisible();
  const log = (await page.locator('pre.compile-log').textContent())!.toLowerCase();
  expect(log).toContain('error');
});

test('W-JUDGE-08 结果页资源统计展示', async ({ page }) => {
  await loggedIn(page);
  await submitProblem(page, JUDGE_PROBLEM_ID, AC);
  const chips = await page.locator('.result-stats .stat-chip').allTextContents();
  expect(chips).toHaveLength(2);
  expect(chips.some((c) => c.includes(' ms'))).toBe(true);
  expect(chips.some((c) => c.includes(' KB'))).toBe(true);
});

test('W-JUDGE-09 编辑器 Ctrl+Enter 提交', async ({ page }) => {
  await loggedIn(page);
  await page.goto(`/problems/${JUDGE_PROBLEM_ID}`);
  await setAceCode(page, AC);
  await page.locator('.ace_text-input').press('Control+Enter');
  await page.locator('.result-status').waitFor({ timeout: 30_000 });
  await expect(page.locator('.result-status')).toHaveText('通过 (AC)');
});

test('W-JUDGE-11 题目无用例判题不可用 500', async ({ page }) => {
  await loggedIn(page);
  await submitProblem(page, NO_TC_PROBLEM_ID, AC, 'h1.error-code');
  await expect(page.locator('h1.error-code')).toHaveText('500');
  await expect(page.locator('h2')).toContainText('判题不可用');
});
```

> `W-JUDGE-10`（提交落库）为 DB 校验，不在浏览器层断言；执行完 W-JUDGE-02 后手动或
> 用 `mysql` 命令行验证 `submissions` 表（见 §4.4 W-JUDGE-10 的 SQL）。

### 6.7 admin.spec.ts（M5）

```ts
import { test, expect, Page } from '@playwright/test';
import { register, login, ts, ADMIN_USER, ADMIN_PASS } from './helpers';

async function adminLogin(page: Page) {
  await login(page, ADMIN_USER, ADMIN_PASS);
}

async function createProblem(page: Page, title: string, difficulty = 'medium', nCases = 2) {
  await page.goto('/admin/problems/new');
  await page.locator('#title').fill(title);
  await page.locator('#difficulty').selectOption(difficulty);
  await page.locator('#description').fill('## 自动化测试题\n描述内容');
  const inputs = page.locator('textarea[name="input[]"]');
  const exps = page.locator('textarea[name="expected[]"]');
  for (let i = 0; i < Math.min(nCases, await inputs.count()); i++) {
    await inputs.nth(i).fill(`${i + 1}\n`);
    await exps.nth(i).fill(`out${i + 1}\n`);
  }
  await page.locator('form.admin-form button[type=submit]').click();
  await page.waitForURL(/\/admin\/problems/);
}

async function findIdByTitle(page: Page, title: string) {
  const row = page.locator('table.problem-table tbody tr', { hasText: title });
  return (await row.locator('.col-id').textContent())!.trim();
}

test('W-ADMIN-01 游客访问管理后台重定向登录', async ({ page }) => {
  await page.goto('/admin/problems');
  await page.waitForURL(/\/login/);
});

test('W-ADMIN-02 普通用户访问管理后台 403', async ({ page }) => {
  await register(page, `webuser_${ts()}`);
  for (const path of ['/admin/problems', '/admin/problems/new']) {
    await page.goto(path);
    await expect(page.locator('h1.error-code')).toHaveText('403');
    await expect(page.locator('h2')).toContainText('访问被拒绝');
  }
});

test('W-ADMIN-03 普通用户无管理入口', async ({ page }) => {
  await register(page, `nounlink_${ts()}`);
  await expect(page.locator('a[href="/admin/problems"]')).toHaveCount(0);
});

test('W-ADMIN-04 admin 进入管理后台列表', async ({ page }) => {
  await adminLogin(page);
  await page.goto('/admin/problems');
  await expect(page.locator('h1')).toContainText('题目管理');
  await expect(page.locator('a[href="/admin/problems/new"]')).toBeVisible();
});

test('W-ADMIN-05 新增题目（含多组用例）', async ({ page }) => {
  await adminLogin(page);
  const title = `webadmin_${ts()}`;
  await createProblem(page, title);
  const row = page.locator('table.problem-table tbody tr', { hasText: title });
  await expect(row).toHaveCount(1);
  await expect(row).toContainText('中等');
});

test('W-ADMIN-06 新增题目校验失败不落库', async ({ page }) => {
  await adminLogin(page);
  await page.goto('/admin/problems/new');
  await page.locator('form.admin-form button[type=submit]').click();
  await expect(page.locator('.form-error'))
    .toContainText('请填写标题、Markdown 描述，并至少提供一组');
});

test('W-ADMIN-07 编辑题目预填与保存', async ({ page }) => {
  await adminLogin(page);
  const title = `webadmin_${ts()}`;
  await createProblem(page, title);
  const pid = await findIdByTitle(page, title);
  await page.goto(`/admin/problems/${pid}/edit`);
  await expect(page.locator('#title')).toHaveValue(title);
  await expect(page.locator('#description')).toContainText('自动化测试题');
  await expect(page.locator('textarea[name="input[]"]')).toHaveCount(2);
  const newTitle = title + '_edited';
  await page.locator('#title').fill(newTitle);
  await page.locator('#difficulty').selectOption('hard');
  await page.locator('form.admin-form button[type=submit]').click();
  await page.waitForURL(/\/admin\/problems/);
  await expect(page.locator('table.problem-table tbody tr', { hasText: newTitle }))
    .toContainText('困难');
});

test('W-ADMIN-08 删除题目二次确认（取消）', async ({ page }) => {
  await adminLogin(page);
  const title = `webadmin_del_${ts()}`;
  await createProblem(page, title);
  const pid = await findIdByTitle(page, title);
  page.once('dialog', (d) => d.dismiss());
  await page.locator(`form.admin-delete[action*='/${pid}/delete'] button`).click();
  await expect(page.locator('table.problem-table tbody tr', { hasText: title }))
    .toHaveCount(1);
});

test('W-ADMIN-09 删除题目物理删除（确认）', async ({ page }) => {
  await adminLogin(page);
  const title = `webadmin_del2_${ts()}`;
  await createProblem(page, title);
  const pid = await findIdByTitle(page, title);
  page.once('dialog', (d) => d.accept());
  await page.locator(`form.admin-delete[action*='/${pid}/delete'] button`).click();
  await page.waitForURL(/\/admin\/problems/);
  await expect(page.locator('table.problem-table tbody tr', { hasText: title }))
    .toHaveCount(0);
  await page.goto(`/problems/${pid}`);
  await expect(page).toHaveTitle('404 - CPP OJ');
});

test('W-ADMIN-10 新增表单用例动态增删', async ({ page }) => {
  await adminLogin(page);
  await page.goto('/admin/problems/new');
  await expect(page.locator('.tc-group')).toHaveCount(2);
  await page.locator('#add-tc').click();
  await expect(page.locator('.tc-group')).toHaveCount(3);
  await page.locator('.tc-remove').first().click();
  await expect(page.locator('.tc-group')).toHaveCount(2);
});
```

### 6.8 errors.spec.ts（M6）

```ts
import { test, expect } from '@playwright/test';
import { login, ADMIN_USER, ADMIN_PASS } from './helpers';

test('W-ERR-01 未匹配路由 404', async ({ page }) => {
  await page.goto('/does-not-exist');
  await expect(page).toHaveTitle('404 - CPP OJ');
  await expect(page.locator('h2')).toContainText('页面不存在');
});

test('W-ERR-02 输入 HTML 转义防 XSS', async ({ page }) => {
  await login(page, ADMIN_USER, ADMIN_PASS);
  const xssTitle = `<script>alert('xss')</script>`;
  await page.goto('/admin/problems/new');
  await page.locator('#title').fill(xssTitle);
  await page.locator('#description').fill('desc <img src=x onerror=alert(1)>');
  await page.locator('form.admin-form button[type=submit]').click();
  await page.waitForURL(/\/admin\/problems/);
  // 列表中以文本显示原样尖括号，未被当作 HTML 执行
  await expect(page.locator('table.problem-table')).toContainText('<script>');
  // 详情页同样转义
  const link = page.locator('a.problem-title', { hasText: '<script>' });
  await expect(link).toHaveCount(1);
  const pid = (await link.first().getAttribute('href'))!.split('/').pop();
  await page.goto(`/problems/${pid}`);
  await expect(page.locator('.markdown-body')).toContainText('<script>');
  // 清理：删除该题
  await page.goto('/admin/problems');
  page.once('dialog', (d) => d.accept());
  await page.locator(`form.admin-delete[action*='/${pid}/delete'] button`).click();
});

test('W-ERR-03 会话 Cookie HttpOnly', async ({ page }) => {
  await login(page, ADMIN_USER, ADMIN_PASS);
  const cookies = await page.evaluate(() => document.cookie);
  expect(cookies).not.toContain('oj_session');   // HttpOnly 不可被 JS 读取
});
```

### 6.9 concurrency.spec.ts（M7）

```ts
import { test, expect } from '@playwright/test';
import { register, submitProblem, resultStatus, ts, JUDGE_PROBLEM_ID } from './helpers';

const AC = `#include <iostream>
int main(){int n;std::cin>>n;int a,b,c;std::cin>>a>>b>>c;std::cout<<a<<" "<<b;return 0;}`;

const CE = 'this is not valid c++';

test('W-CONC-01 双并发提交互不污染', async ({ browser }) => {
  const ctxA = await browser.newContext();
  const ctxB = await browser.newContext();
  const pageA = await ctxA.newPage();
  const pageB = await ctxB.newPage();
  await register(pageA, `con_a_${ts()}`);
  await register(pageB, `con_b_${ts()}`);
  const [rA, rB] = await Promise.all([
    submitProblem(pageA, JUDGE_PROBLEM_ID, AC),
    submitProblem(pageB, JUDGE_PROBLEM_ID, CE),
  ]);
  expect(await resultStatus(pageA)).toBe('通过 (AC)');
  expect(await resultStatus(pageB)).toBe('编译错误 (CE)');
  await ctxA.close();
  await ctxB.close();
});
```

### 6.10 执行

```bash
cd web_auto
npm install
npx playwright install chromium          # 首次：下载浏览器内核
npx playwright test                      # 全量（headless，workers 按 config）
npx playwright test judge.spec.ts        # 仅判题
npx playwright test --grep "MLE|RE"      # 按用例名关键字过滤
npx playwright test --headed             # 有头模式调试
npx playwright test --workers=1          # 串行（避免判题信号量排队干扰）
npx playwright test --reporter=html      # 生成 HTML 报告 → playwright-report/
npx playwright show-report               # 查看报告
npx playwright codegen 127.0.0.1:8080    # 录制辅助生成/补充用例
```

> MLE/TLE 用例各占判题信号量（全局并发 ≤ 2，FR-3.5），判题用例间存在排队，
> 全量执行建议 `--workers=1`，耗时约 2~3 分钟。
> `W-ERR-04`（停库）用例破坏性较大，未写入自动脚本，需人工执行。

---

## 7. 测试数据准备与清理

- **自动生成**：注册类用例用户名为 `web/webdup/loginok/..._<时间戳>`；管理类用例题目为
  `webadmin_<时间戳>`。重复执行互不冲突。
- **清理**：删除类用例自建自删；注册用户、判题提交（`submissions`）为累积数据，
  如需干净环境可手动清理：

  ```sql
  USE cpp_oj;
  DELETE FROM submissions;                       -- 清理判题提交
  DELETE FROM users WHERE username LIKE 'web%' OR username LIKE 'judge%' OR username LIKE 'login%'
    OR username LIKE 'con_%' OR username LIKE 'logout%' OR username LIKE 'redi%'
    OR username LIKE 'mismatch%' OR username LIKE 'webuser%' OR username LIKE 'nounlink%';
  DELETE FROM problems WHERE title LIKE 'webadmin%';
  ```

- **注意**：`admin` 账号请勿删除；删除题目会级联删除其 `testcases`（FR-4.3）。

---

## 8. 注意事项与已知问题

1. **ace.js 提交同步**：`Ctrl+Enter`（`editor.commands.addCommand` 中调用
   `form.submit()`）按浏览器规范**不会触发** `submit` 事件，隐藏 `textarea#code`
   不会同步编辑器内容（`www/problem_detail.html`）。若通过 `Ctrl+Enter` 提交后发现
   结果是「预填模板」的判题结果而非编辑内容，属于前端缺陷，`W-JUDGE-09` 即为此设计。
   自动化脚本统一采用「点击「运行提交」按钮」的可靠路径。
2. **同步判题 + 信号量限流**：判题为同步阻塞，全局并发 ≤ 2（FR-3.5）。并发执行
   MLE/TLE 用例会排队，判题结果断言超时建议 30s；全量跑用 `--workers=1`。
3. **会话内存存储**：`oj_server` 重启后所有会话失效（`API.md` 5.1），测试中登录态
   全部失效，需重新登录；`W-ERR-04` 停库后必须重启服务。Playwright 每个 test 默认
   独立 context，登录态互不串扰。
4. **测试污染**：本套用例会写入 `users` / `submissions` / `problems` / `testcases`，
   如题库预置题目发生变化（判题用题 id 变更），请同步更新 `helpers.ts` 的
   `JUDGE_PROBLEM_ID` 与样本代码的输入/输出格式。
5. **MySQL 端口**：本机业务库实例为 `127.0.0.1:3307`；多实例环境下以
   `src/config.h` `kDbPort` 与实际监听为准（SPEC 4.3.1）。
6. **判题依赖**：判题需 g++ 在 PATH、系统存在 `nobody` 用户、`/tmp` 可建临时目录；
   任一缺失则判题用例失败（启动自检会提示，见 SPEC A.1）。
7. **无头浏览器网络**：`layout.html` 引入 Google Fonts（`fonts.googleapis.com`），
   离线环境不影响功能断言（仅字体回退），无需修改用例。
8. **Playwright CLI 相关**：`npx playwright codegen` 可录制真实操作辅助快速补用例；
   失败用例会保留 trace/screenshot（见 `playwright.config.ts`），可用
   `npx playwright show-trace` 回放定位。
