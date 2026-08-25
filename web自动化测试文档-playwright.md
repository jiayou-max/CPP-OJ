# Playwright CLI 交互式执行手册（实测流程）

> 本手册记录基于 **`playwright-cli` 交互式命令** 的实测操作流程，面向仿 LeetCode OJ
> （cpp-oj-vibe-coding）。与 [web自动化测试文档.md](./web自动化测试文档.md)（`npx playwright test`
> TS 用例）互为补充：前者适合**人眼观察**逐步执行与排查，后者适合一键回归。
>
> 以下命令均为 **Windows PowerShell** 实测通过，被测服务 `http://127.0.0.1:8080`。

---

## 1. 启动与通用约定

- **有头模式 + 本机已装 Chrome**：首次 `playwright-cli open --browser=chromium` 会因缺少
  自管 Chromium 内核而失败，报 `Browser "chrome-for-testing" is not installed`；
  安装内核需下载 ~190MB，建议直接复用本机 Chrome：

  ```powershell
  playwright-cli open --headed --browser=chrome
  ```

- **每步停顿 1s**（便于观察）：命令后追加 `; Start-Sleep -Seconds 1`。
- **定位元素**：snapshot 给出的 ref（如 `e18`）每次快照会变，**不可写死**；
  可复现写法优先用 CSS 选择器（与 [web自动化测试文档.md](./web自动化测试文档.md) §3.2 一致）。
- **取原始值**：`playwright-cli --raw eval "..."` 只回显结果，便于管道/比对。
- **看控制台报错**：`playwright-cli console`（`/favicon.ico` 404 属无害噪音）。

---

## 2. M1 首页与静态资源（4.1，已实测 4/4 通过）

### 2.1 W-HOME-01 游客访问首页成功渲染 ✅

```powershell
playwright-cli goto http://127.0.0.1:8080/; Start-Sleep -Seconds 1
# 输出：Page Title: CPP OJ - 在线评测系统
playwright-cli snapshot; Start-Sleep -Seconds 1
```

校验点（snapshot 中逐一确认）：
- 品牌 `CPP OJ`（导航 `link "/> CPPOJ"`，url `/`）；
- hero 标题 `让每一行代码 都被严谨验证`；
- CTA `开始刷题`（url `/problems`）；
- 特性区四张卡片：`判题内核 / 细粒度结果 / 数据持久化 / 嵌入式编辑器`。

### 2.2 W-HOME-02 首页统计实时数字展示 ✅

```powershell
playwright-cli --raw eval "JSON.stringify([...document.querySelectorAll('.hero-stats .stat-value')].map(e => e.textContent.trim()))"; Start-Sleep -Seconds 1
# 实测输出：["2","2","0"]
```

校验点：
- 恰好 3 个值，均为非负整数（`/^\d+$/`）；
- 值非 `--` 即数据库正常；本机无 `mysql` 客户端，DB 交叉比对需自装客户端或走接口文档。

### 2.3 W-HOME-03 首页导航与 CTA 跳转 ✅

```powershell
# ① 点击「开始刷题」CTA
playwright-cli click "a.btn-primary[href='/problems']"; Start-Sleep -Seconds 1
# URL → http://127.0.0.1:8080/problems，标题 题库 - CPP OJ

# ② 返回首页，点导航「登录」
playwright-cli goto http://127.0.0.1:8080/; Start-Sleep -Seconds 1
playwright-cli click "a[href='/login']"; Start-Sleep -Seconds 1
# URL → /login，标题 登录 - CPP OJ

# ③ 返回首页，点导航「注册」（注意：页面上有 2 个 a[href='/register']，
#    裸选择器会 strict mode violation，需用 nav 限定）
playwright-cli goto http://127.0.0.1:8080/; Start-Sleep -Seconds 1
playwright-cli click "nav a[href='/register']"; Start-Sleep -Seconds 1
# URL → /register，标题 注册 - CPP OJ
```

> 已登录态导航（用户名 + 登出、admin 显示「管理」）由 M3/M5 模块覆盖。

### 2.4 W-HOME-04 静态资源可加载 ✅

```powershell
playwright-cli --raw eval "Promise.all(['/static/css/style.css','/static/js/ace/ace.min.js','/static/js/ace/mode-c_cpp.min.js','/static/js/ace/theme-monokai.min.js'].map(async u => { const r = await fetch(u); const b = await r.text(); return {url:u, status:r.status, ct:r.headers.get('content-type'), len:b.length}; }))"; Start-Sleep -Seconds 1
```

实测结果：4 个资源全部 `status: 200` 且 `len > 0`；
`style.css` → `text/css`，3 个 ace js → `application/javascript`。

### 2.5 实测结果汇总（2026-08-24）

| 用例 | 结论 | 备注 |
|------|------|------|
| W-HOME-01 | ✅ 通过 | 标题/hero/CTA/特性区齐全 |
| W-HOME-02 | ✅ 通过 | `2/2/0`，非负整数；未做 DB 交叉比对（本机无 mysql 客户端） |
| W-HOME-03 | ✅ 通过 | CTA→`/problems`；登录→`/login`；注册→`/register` |
| W-HOME-04 | ✅ 通过 | 4 个静态资源 200 且非空，Content-Type 正确 |

---

## 3. M2 题库浏览与做题页（4.2，已实测 5/5 通过）

### 3.1 W-PROB-01 题库列表展示全部题目 ✅

```powershell
playwright-cli goto http://127.0.0.1:8080/problems; Start-Sleep -Seconds 1
# 输出：Page Title: 题库 - CPP OJ
playwright-cli snapshot; Start-Sleep -Seconds 1
```
校验点（snapshot 中逐一确认）：
- 头部「共 N 题」数字 > 0（实测「共 2 题」）；
- 每题一行：编号、标题链接（指向 `/problems/{id}`）、难度（简单/中等/困难）。

DOM 结构校验（与主文档 §3.2 选择器对齐）：

```powershell
playwright-cli --raw eval "JSON.stringify({table: document.querySelectorAll('table.problem-table tbody tr').length, titleLinks: [...document.querySelectorAll('a.problem-title')].map(a=>a.getAttribute('href')), badges: [...document.querySelectorAll('a.problem-title')].map(a=>a.closest('tr').querySelector('.badge')?.className), header: document.querySelector('main')?.textContent?.match(/共 \d+ 题/)?.[0]})"
# 实测：{"table":2,"titleLinks":["/problems/5","/problems/6"],"badges":["badge badge-easy","badge badge-medium"],"header":"共 2 题"}
```

### 3.2 W-PROB-02 题目详情页结构完整 ✅

从题库点击标题进入题 5 详情页（也可直接 `goto /problems/5`）：

```powershell
playwright-cli click "a.problem-title[href='/problems/5']"; Start-Sleep -Seconds 1
# 输出：Page Title: 两数之和 - CPP OJ
playwright-cli snapshot; Start-Sleep -Seconds 1
```
校验点（snapshot）：
- 题头含 `#5`、标题「两数之和」、难度 `简单`；
- 描述区 `.markdown-body` 非空（Markdown 已转 HTML：标题/示例/code/strong）；
- 右侧 ace 编辑器（`main.cpp` · `C++17 · g++ -O2`）+ 「运行提交」按钮。

DOM 结构校验：

```powershell
playwright-cli --raw eval "JSON.stringify({editor: !!document.querySelector('#code-editor'), ace_initialized: !!document.querySelector('.ace_editor'), textarea_code: (()=>{const t=document.querySelector('textarea#code[name=code]'); return {exists:!!t, hasValue:(t? t.value.length>0 : false)};})(), form_action: document.querySelector('#submit-form')?.getAttribute('action'), btn: document.querySelector('#submit-form button')?.textContent.trim(), markdown: (()=>{const m=document.querySelector('.markdown-body'); return {exists:!!m, nonEmpty:(m? m.textContent.trim().length>0:false)};})()})"
# 实测：editor/ace_initialized=true，textarea_code.hasValue=true，
#       form_action="/problems/5/submit"，btn="运行提交"，markdown.nonEmpty=true
```

### 3.3 W-PROB-03 详情页 Markdown 渲染 ✅

```powershell
playwright-cli --raw eval "JSON.stringify({h1h2: document.querySelectorAll('.markdown-body h1, .markdown-body h2').length, h3: document.querySelectorAll('.markdown-body h3').length, p: document.querySelectorAll('.markdown-body p').length, strong: document.querySelectorAll('.markdown-body strong').length, code: document.querySelectorAll('.markdown-body code').length, rawHash: (document.querySelector('.markdown-body')?.textContent||'').includes('#')})"
# 实测：h1h2=1, h3=1, p=3, strong=1, code=4, rawHash=false
#       （标题/示例/段落/强调/行内 code 均渲染为 HTML，无裸露 # 原文字符）
```
> 题 5 无代码块故 `pre=0`；若需校验 `<pre>`，可访问题 6 或其他含代码块的题。

### 3.4 W-PROB-04 不存在题目返回 404 ✅

```powershell
playwright-cli goto http://127.0.0.1:8080/problems/999; Start-Sleep -Seconds 1
# 输出：Page Title: 404 - CPP OJ，HTTP status: 404 Not Found
playwright-cli --raw eval "JSON.stringify({code: document.querySelector('h1.error-code')?.textContent.trim(), h2: document.querySelector('h2')?.textContent.trim()})"
# 实测：{"code":"404","h2":"题目不存在"}
```

### 3.5 W-PROB-05 非法 id 返回 404 ✅

```powershell
playwright-cli goto http://127.0.0.1:8080/problems/abc; Start-Sleep -Seconds 1
# 输出：Page Title: 404 - CPP OJ，HTTP status: 404 Not Found
playwright-cli --raw eval "JSON.stringify({code: document.querySelector('h1.error-code')?.textContent.trim(), h2: document.querySelector('h2')?.textContent.trim()})"
# 实测：{"code":"404","h2":"页面不存在"}
```

### 3.6 实测结果汇总（2026-08-25）

| 用例 | 结论 | 备注 |
|------|------|------|
| W-PROB-01 | ✅ 通过 | 共 2 题；链接/徽章选择器与文档一致 |
| W-PROB-02 | ✅ 通过 | 编辑器/隐藏 textarea 预填/`action=/problems/5/submit`/运行提交按钮 |
| W-PROB-03 | ✅ 通过 | h2/h3/p/strong/code 渲染为 HTML，无裸露 `#` |
| W-PROB-04 | ✅ 通过 | 404 页，`h1.error-code=404`，h2「题目不存在」 |
| W-PROB-05 | ✅ 通过 | 404 页，`h1.error-code=404`，h2「页面不存在」 |

---

## 4. M3 用户系统（4.3，已实测 9/9 通过）

> 注册/登录表单字段选择器（与主文档 §3.2 一致）：`form.auth-form`、`#username`、
> `#password`、`#password2`、`form.auth-form button[type=submit]`。
> 用户名为示例值，实际执行时用唯一后缀（`web_<时间戳>`）避免重名冲突。

### 4.1 W-AUTH-01 注册成功并自动登录 ✅

```powershell
playwright-cli goto http://127.0.0.1:8080/register; Start-Sleep -Seconds 1
playwright-cli fill "#username" "web_<时间戳>"; Start-Sleep -Seconds 1
playwright-cli fill "#password" "secret123"; Start-Sleep -Seconds 1
playwright-cli fill "#password2" "secret123"; Start-Sleep -Seconds 1
playwright-cli click "form.auth-form button[type=submit]"; Start-Sleep -Seconds 1
# 输出：URL → /problems（302 自动登录），标题 题库 - CPP OJ
```
校验：

```powershell
playwright-cli --raw eval "JSON.stringify({user: document.querySelector('.navbar-user')?.textContent.trim(), logoutBtn: !!document.querySelector('form.navbar-logout button')})"
# 实测：{"user":"web_20260825122126079","logoutBtn":true}
```

### 4.2 W-AUTH-02 重复用户名被拒绝 ✅

```powershell
# 先登出，再用 W-AUTH-01 的用户名注册
playwright-cli click "form.navbar-logout button"; Start-Sleep -Seconds 1
playwright-cli goto http://127.0.0.1:8080/register; Start-Sleep -Seconds 1
playwright-cli fill "#username" "web_<同上>"; Start-Sleep -Seconds 1
playwright-cli fill "#password" "secret123"; Start-Sleep -Seconds 1
playwright-cli fill "#password2" "secret123"; Start-Sleep -Seconds 1
playwright-cli click "form.auth-form button[type=submit]"; Start-Sleep -Seconds 1
# 输出：URL 仍为 /register（200，不跳转）
```
校验：

```powershell
playwright-cli --raw eval "JSON.stringify({error: document.querySelector('.form-error')?.textContent.trim()})"
# 实测：{"error":"用户名已存在，请换一个。"}
```

### 4.3 W-AUTH-03 保留用户名 admin 被拒绝 ✅

```powershell
playwright-cli fill "#username" "admin"; Start-Sleep -Seconds 1
playwright-cli fill "#password" "admin123"; Start-Sleep -Seconds 1
playwright-cli fill "#password2" "admin123"; Start-Sleep -Seconds 1
playwright-cli click "form.auth-form button[type=submit]"; Start-Sleep -Seconds 1
# 实测 .form-error：用户名已存在，请换一个。
```

### 4.4 W-AUTH-04 两次密码不一致被拒绝 ✅

```powershell
playwright-cli fill "#username" "mismatch_<时间戳>"; Start-Sleep -Seconds 1
playwright-cli fill "#password" "secret123"; Start-Sleep -Seconds 1
playwright-cli fill "#password2" "secret456"; Start-Sleep -Seconds 1
playwright-cli click "form.auth-form button[type=submit]"; Start-Sleep -Seconds 1
# 实测 .form-error：两次输入的密码不一致。
```

### 4.5 W-AUTH-05 非法用户名/短密码被拒绝 ✅

```powershell
playwright-cli fill "#username" "b@d!name"; Start-Sleep -Seconds 1
playwright-cli fill "#password" "secret123"; Start-Sleep -Seconds 1
playwright-cli fill "#password2" "secret123"; Start-Sleep -Seconds 1
playwright-cli click "form.auth-form button[type=submit]"; Start-Sleep -Seconds 1
# 实测 .form-error：用户名仅允许字母/数字/下划线（1~64 位），密码长度 6~64。
# 另测：密码 "123"（<6 位）同样被拒，报同一文案。
```

### 4.6 W-AUTH-06 登录成功 ✅

```powershell
playwright-cli goto http://127.0.0.1:8080/login; Start-Sleep -Seconds 1
playwright-cli fill "#username" "web_<已注册用户>"; Start-Sleep -Seconds 1
playwright-cli fill "#password" "secret123"; Start-Sleep -Seconds 1
playwright-cli click "form.auth-form button[type=submit]"; Start-Sleep -Seconds 1
# 输出：URL → /problems；导航 .navbar-user 显示用户名 + 登出按钮
```

### 4.7 W-AUTH-07 登录失败统一文案（防枚举）✅

```powershell
# ① 已存在用户 + 错误密码
playwright-cli click "form.navbar-logout button"; Start-Sleep -Seconds 1
playwright-cli goto http://127.0.0.1:8080/login; Start-Sleep -Seconds 1
playwright-cli fill "#username" "web_<已注册用户>"; Start-Sleep -Seconds 1
playwright-cli fill "#password" "wrongpass"; Start-Sleep -Seconds 1
playwright-cli click "form.auth-form button[type=submit]"; Start-Sleep -Seconds 1
# ② 不存在用户 + 任意密码
playwright-cli fill "#username" "nouser_<时间戳>"; Start-Sleep -Seconds 1
playwright-cli fill "#password" "whatever1"; Start-Sleep -Seconds 1
playwright-cli click "form.auth-form button[type=submit]"; Start-Sleep -Seconds 1
```
两种场景 `.form-error` 均一致：

```powershell
# 实测：{"error":"用户名或密码错误。"}（不区分账号不存在/密码错误，防枚举）
```

### 4.8 W-AUTH-08 已登录访问登录/注册页重定向 ✅

```powershell
playwright-cli fill "#username" "web_<已注册用户>"; Start-Sleep -Seconds 1
playwright-cli fill "#password" "secret123"; Start-Sleep -Seconds 1
playwright-cli click "form.auth-form button[type=submit]"; Start-Sleep -Seconds 1
playwright-cli goto http://127.0.0.1:8080/login; Start-Sleep -Seconds 1
# 输出：URL 仍为 /problems（302 重定向，不显示登录表单）
playwright-cli goto http://127.0.0.1:8080/register; Start-Sleep -Seconds 1
# 输出：URL 仍为 /problems（302 重定向）
```

### 4.9 W-AUTH-09 登出成功并清会话 ✅

```powershell
playwright-cli click "form.navbar-logout button"; Start-Sleep -Seconds 1
# 输出：URL → /problems
```
校验导航恢复未登录态（用户名元素消失）：

```powershell
playwright-cli --raw eval "JSON.stringify({nav: document.querySelector('nav')?.textContent.trim().replace(/\\s+/g,' '), user: !!document.querySelector('.navbar-user')})"
# 实测：{"nav":"题库 首页 登录 注册","user":false}
```
校验会话已失效（原 Cookie 销毁，受保护接口被重定向）：

```powershell
playwright-cli goto http://127.0.0.1:8080/admin/problems; Start-Sleep -Seconds 1
# 输出：URL → /login（302 重定向）
```

### 4.10 实测结果汇总（2026-08-25）

| 用例 | 结论 | 备注 |
|------|------|------|
| W-AUTH-01 | ✅ 通过 | 注册→302 `/problems`，自动登录，导航显示用户名+登出 |
| W-AUTH-02 | ✅ 通过 | 200 注册页，`.form-error`「用户名已存在，请换一个。」 |
| W-AUTH-03 | ✅ 通过 | admin 保留用户名被拒，同文案 |
| W-AUTH-04 | ✅ 通过 | `.form-error`「两次输入的密码不一致。」 |
| W-AUTH-05 | ✅ 通过 | 非法用户名/短密码均报「用户名仅允许字母/数字/下划线…」 |
| W-AUTH-06 | ✅ 通过 | 登录→302 `/problems`，导航显示用户名 |
| W-AUTH-07 | ✅ 通过 | 错密码/不存在用户两种场景文案一致（防枚举） |
| W-AUTH-08 | ✅ 通过 | 已登录访问 `/login`、`/register` 均 302 → `/problems` |
| W-AUTH-09 | ✅ 通过 | 登出后导航恢复登录/注册；`/admin/problems` 302 → `/login` |

---

## 5. 后续模块（M4~M7）执行提示

- 沿用 §1 的启动方式与定位/取值约定；
- 命令映射见 [web自动化测试文档.md](./web自动化测试文档.md) §3.2 选择器速查表；
- ace 编辑器填码、判题提交、管理后台 confirm 弹窗等交互命令在本手册持续补充。
