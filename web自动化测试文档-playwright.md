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

## 5. M4 判题内核（4.4，已实测 11/11 通过）

> **前置补充**：判题依赖 g++、`/tmp` 临时目录与 DB（详见 [web自动化测试文档.md](./web自动化测试文档.md)
> §8-6）。本机 MySQL 运行在 **WSL** 内、由 `wslrelay` 转发到 `127.0.0.1:3307`（root 免密），
> Windows 无 `mysql` 客户端时可用 `wsl` 直连查库（用于 W-JUDGE-10）：
>
> ```powershell
> wsl bash -lc "mysql -uroot -h127.0.0.1 -P3307 --table -e 'SELECT ... FROM cpp_oj.xxx;'"
> ```
>
> **ace 填码约定**：判题代码含引号/换行，经 PowerShell 转义易出错，本手册改用 **JS 脚本文件 +
> `run-code`** 注入（等价 `setAceCode`，见 [web自动化测试文档.md](./web自动化测试文档.md) §3.3）：
>
> ```powershell
> playwright-cli run-code --filename=playwright-js/set_ac.js   # 每份样本对应一个脚本
> ```
>
> 脚本内容（以 `set_ac.js` 为例）：`document.getElementById('code-editor').env.editor.setValue(code,-1)`；
> 样本 ①~⑥ 对应 `playwright-js/set_{ac,wa,tle,mle,re,ce}.js`，Ctrl+Enter 用 `ctrl_enter.js`。
> 判题为**同步阻塞**，`playwright-cli click` 默认动作超时 5s，TLE 等慢用例可能报
> `TimeoutError`——此时**提交已发出**，稍候再查页面即可，不影响结果。

### 5.1 登录身份

```powershell
playwright-cli open --headed --browser=chrome http://127.0.0.1:8080/
playwright-cli goto http://127.0.0.1:8080/register; Start-Sleep -Seconds 1
playwright-cli fill "#username" "judge_<时间戳>"; Start-Sleep -Seconds 1
playwright-cli fill "#password" "secret123"; Start-Sleep -Seconds 1
playwright-cli fill "#password2" "secret123"; Start-Sleep -Seconds 1
playwright-cli click "form.auth-form button[type=submit]"; Start-Sleep -Seconds 1
# 实测：URL → /problems（302 自动登录），.navbar-user=judge_20260825124712007，logoutBtn=true
```
> 交互式单会话可复用同一 judge 用户跑完全部判题用例（自动化脚本为每用例新建，见 §6.6）。

### 5.2 W-JUDGE-01 游客提交重定向登录页 ✅

```powershell
# 未登录时直接打开题 5 详情页点「运行提交」
playwright-cli goto http://127.0.0.1:8080/problems/5; Start-Sleep -Seconds 1
playwright-cli click "#submit-form button[type=submit]"; Start-Sleep -Seconds 1
# 输出：URL → http://127.0.0.1:8080/login，标题 登录 - CPP OJ（未进入判题流程）
```

### 5.3 W-JUDGE-02 AC：正确代码全部通过 ✅

```powershell
playwright-cli goto http://127.0.0.1:8080/problems/5; Start-Sleep -Seconds 1
playwright-cli run-code --filename=playwright-js/set_ac.js; Start-Sleep -Seconds 1
# 实测：{"set":"AC","len":243}
playwright-cli click "#submit-form button[type=submit]"; Start-Sleep -Seconds 1
# 输出：URL → /problems/5/submit，标题 判题结果：两数之和 - CPP OJ
```
校验：

```powershell
playwright-cli --raw eval "JSON.stringify({status: document.querySelector('.result-status')?.textContent.trim(), chips: [...document.querySelectorAll('.result-stats .stat-chip')].map(e=>e.textContent.trim()), cases: [...document.querySelectorAll('table.case-table tbody tr')].map(r=>r.textContent.replace(/\s+/g,' ').trim())})"
# 实测：{"status":"通过 (AC)","chips":["10 ms","9300 KB"],
#        "cases":["#1通过5 ms9300 KB","#2通过5 ms9300 KB"]}
```

### 5.4 W-JUDGE-03 WA：答案错误可见失败用例 ✅

```powershell
playwright-cli goto http://127.0.0.1:8080/problems/5; Start-Sleep -Seconds 1
playwright-cli run-code --filename=playwright-js/set_wa.js; Start-Sleep -Seconds 1
playwright-cli click "#submit-form button[type=submit]"; Start-Sleep -Seconds 1
playwright-cli --raw eval "JSON.stringify({status: document.querySelector('.result-status')?.textContent.trim(), cases: [...document.querySelectorAll('table.case-table tbody tr')].map(r=>r.textContent.replace(/\s+/g,' ').trim())})"
# 实测：{"status":"答案错误 (WA)","cases":["#1答案错误5 ms9796 KB","#2答案错误5 ms9796 KB"]}
```

### 5.5 W-JUDGE-04 TLE：死循环有界返回 ✅

```powershell
playwright-cli goto http://127.0.0.1:8080/problems/5; Start-Sleep -Seconds 1
playwright-cli run-code --filename=playwright-js/set_tle.js; Start-Sleep -Seconds 1
$t0 = Get-Date; playwright-cli click "#submit-form button[type=submit]"; (Get-Date) - $t0
# 实测：click 报 TimeoutError（默认动作超时 5s，判题同步阻塞耗时更长）——提交已发出；
#       稍候再查页面即得结果
playwright-cli --raw eval "JSON.stringify({url: location.pathname, status: document.querySelector('.result-status')?.textContent.trim(), cases: [...document.querySelectorAll('table.case-table tbody tr')].map(r=>r.textContent.replace(/\s+/g,' ').trim())})"
# 实测：{"url":"/problems/5/submit","status":"运行超时 (TLE)",
#        "cases":["#1超时2004 ms9800 KB","#2超时2002 ms9800 KB"]}
```
服务器未卡死校验（对照 FR-3.5 / A8）：

```powershell
playwright-cli goto http://127.0.0.1:8080/problems; Start-Sleep -Seconds 1
# 实测：标题 题库 - CPP OJ，main 内「共 2 题」→ 判题后服务正常响应
```

### 5.6 W-JUDGE-05 MLE：内存超限 ✅

```powershell
playwright-cli goto http://127.0.0.1:8080/problems/5; Start-Sleep -Seconds 1
playwright-cli run-code --filename=playwright-js/set_mle.js; Start-Sleep -Seconds 1
playwright-cli click "#submit-form button[type=submit]"; Start-Sleep -Seconds 1
# 实测 .result-status：内存超限 (MLE)；逐用例 内存超限 2000 ms 9800 KB（256MB RLIMIT_AS）
```

### 5.7 W-JUDGE-06 RE：运行崩溃 ✅

```powershell
playwright-cli goto http://127.0.0.1:8080/problems/5; Start-Sleep -Seconds 1
playwright-cli run-code --filename=playwright-js/set_re.js; Start-Sleep -Seconds 1
playwright-cli click "#submit-form button[type=submit]"; Start-Sleep -Seconds 1
# 实测 .result-status：运行错误 (RE)；逐用例 运行错误 121/26 ms 9812 KB
```

### 5.8 W-JUDGE-07 CE：编译错误附日志 ✅

```powershell
playwright-cli goto http://127.0.0.1:8080/problems/5; Start-Sleep -Seconds 1
playwright-cli run-code --filename=playwright-js/set_ce.js; Start-Sleep -Seconds 1
playwright-cli click "#submit-form button[type=submit]"; Start-Sleep -Seconds 1
playwright-cli --raw eval "JSON.stringify({status: document.querySelector('.result-status')?.textContent.trim(), h2: [...document.querySelectorAll('h2')].map(h=>h.textContent.trim()), logHead: document.querySelector('pre.compile-log')?.textContent.slice(0,120)})"
# 实测：{"status":"编译错误 (CE)","h2":["编译错误日志","逐用例结果"],
#        "logHead":"main.cpp:1:1: error: expected unqualified-id before ‘this’\n ..."}
```

### 5.9 W-JUDGE-08 结果页资源统计展示 ✅

在 5.3 的 AC 结果页直接校验：

```powershell
playwright-cli --raw eval "JSON.stringify([...document.querySelectorAll('.result-stats .stat-chip')].map(e=>e.textContent.trim()))"
# 实测：["10 ms","9300 KB"]（恰 2 枚 chip，分别含 ms / KB）
```
`table.case-table` 每行亦含时间/内存列（见 5.3 的 cases 输出，A12）。

### 5.10 W-JUDGE-09 编辑器 Ctrl+Enter 提交 ✅（2026-08-25 修复后复验）

```powershell
playwright-cli goto http://127.0.0.1:8080/problems/5; Start-Sleep -Seconds 1
playwright-cli run-code --filename=playwright-js/set_ac.js; Start-Sleep -Seconds 1
playwright-cli run-code --filename=playwright-js/ctrl_enter.js; Start-Sleep -Seconds 1
# ctrl_enter.js：click .ace_editor 聚焦 + keyboard.press('Control+Enter')
Start-Sleep -Seconds 6
playwright-cli --raw eval "JSON.stringify({url: location.pathname, title: document.title, status: document.querySelector('.result-status')?.textContent.trim(), chips: [...document.querySelectorAll('.result-stats .stat-chip')].map(e=>e.textContent.trim())})"
# 实测：{"url":"/problems/5/submit","title":"判题结果：两数之和 - CPP OJ",
#        "status":"通过 (AC)","chips":["10 ms","9832 KB"]}
```
> **修复确认**：前端已在 `www/problem_detail.html` 的 ace `Ctrl+Enter` 命令 `exec` 中先执行
> `ta.value = editor.getValue()` 再 `form.submit()`（提交前同步隐藏 textarea）。
> 复验提交的为**编辑内容**（非模板）：落库 id=122，`status=ac`，`code` 为 AC 样本
> （`#include <vector>`），与修复前 id=121 提交模板的行为形成对照。

> 历史记录（修复前复现）：`Ctrl+Enter` 提交结果为 WA（预填模板），与
> [web自动化测试文档.md](./web自动化测试文档.md) §8-1 注明的缺陷一致；现已在源码层修复。

### 5.11 W-JUDGE-10 提交落库校验 ✅（DB 侧，wsl mysql 直连）

```powershell
wsl bash -lc "mysql -uroot -h127.0.0.1 -P3307 --table -e 'SELECT id,user_id,problem_id,status,time_ms,memory_kb,LEFT(code,40) AS code_head FROM cpp_oj.submissions ORDER BY id DESC LIMIT 10;'"
```
实测 7 条提交全部落库（六状态 + Ctrl+Enter 模板提交）：

| id | status | time_ms | memory_kb | code_head |
|----|--------|---------|-----------|-----------|
| 121 | wa | 184 | 9816 | `#include <iostream> using namespace std`（**模板**，W-JUDGE-09） |
| 120 | ce | 0 | 0 | `this is not valid c++` |
| 119 | re | 147 | 9812 | `int x=1/0` |
| 118 | mle | 4000 | 9800 | `new char[300MB]` |
| 117 | tle | 4006 | 9800 | `while(1){}` |
| 116 | wa | 10 | 9796 | `cout<<"0 0"` |
| 115 | ac | 10 | 9300 | `#include <vector>`（AC 样本） |

AC 记录（id=115）完整校验（FR-5.1 / A13）：

```powershell
wsl bash -lc "mysql -uroot -h127.0.0.1 -P3307 --table -e 'SELECT s.id,u.username,u.role,s.problem_id,s.status,s.time_ms,s.memory_kb,s.code FROM cpp_oj.submissions s JOIN cpp_oj.users u ON u.id=s.user_id WHERE s.id=115;'"
# 实测：username=judge_20260825124712007, role=user, problem_id=5, status=ac,
#       time_ms=10>0, memory_kb=9300>0, code 与 AC 样本逐字一致
```

### 5.12 W-JUDGE-11 题目无用例判题不可用 500 ✅

```powershell
playwright-cli goto http://127.0.0.1:8080/problems/6; Start-Sleep -Seconds 1
playwright-cli run-code --filename=playwright-js/set_ac.js; Start-Sleep -Seconds 1
playwright-cli click "#submit-form button[type=submit]"; Start-Sleep -Seconds 1
playwright-cli --raw eval "JSON.stringify({url: location.pathname, title: document.title, code: document.querySelector('h1.error-code')?.textContent.trim(), h2: [...document.querySelectorAll('h2')].map(h=>h.textContent.trim()), msg: document.querySelector('p')?.textContent.trim()})"
# 实测：{"url":"/problems/6/submit","title":"500 - CPP OJ","code":"500",
#        "h2":["判题不可用"],"msg":"该题目暂无测试用例，请联系管理员。"}
```

### 5.13 实测结果汇总（2026-08-25）

| 用例 | 结论 | 备注 |
|------|------|------|
| W-JUDGE-01 | ✅ 通过 | 游客点提交 → `/login`，不进入判题 |
| W-JUDGE-02 | ✅ 通过 | `通过 (AC)`，2/2 用例通过（5ms/9300KB） |
| W-JUDGE-03 | ✅ 通过 | `答案错误 (WA)`，逐用例可见失败 |
| W-JUDGE-04 | ✅ 通过 | `运行超时 (TLE)`，单用例 ~2004ms 有界；判题后题库页正常 200 |
| W-JUDGE-05 | ✅ 通过 | `内存超限 (MLE)`（RLIMIT_AS 256MB） |
| W-JUDGE-06 | ✅ 通过 | `运行错误 (RE)` |
| W-JUDGE-07 | ✅ 通过 | `编译错误 (CE)` + `pre.compile-log` 含 g++ error |
| W-JUDGE-08 | ✅ 通过 | 2 枚 stat-chip（`ms`/`KB`），逐用例含时间/内存列 |
| W-JUDGE-09 | ✅ 通过 | 修复后复验：Ctrl+Enter 提交编辑内容 → AC；落库 code 为 AC 样本（id=122） |
| W-JUDGE-10 | ✅ 通过 | AC 记录落库：user/problem/status/time/mem/code 全对 |
| W-JUDGE-11 | ✅ 通过 | 题 6 无用例 → 500「判题不可用」 |

> 历史：W-JUDGE-09 修复前为 ❌（Ctrl+Enter 提交模板 → WA），前端修复后复验通过
> （`www/problem_detail.html` 提交前同步隐藏 textarea），详见 5.10。

---

## 6. M5 管理后台（4.5，已实测 10/10 通过）

> **前置**：管理用例统一以 `admin` / `admin123` 登录（`oj_init_admin` 初始化）。测试创建的题目
> 标题统一 `webadmin_*_<时间戳>` 前缀，删除类用例自建自删，本手册结束后已把残留测试题清空。
> **confirm 弹窗**：删除按钮点击后弹出原生 `confirm`，`playwright-cli dialog-accept` /
> `dialog-dismiss` **必须在弹窗出现后**再调用（先 `click` 触发弹窗，再处理弹窗）。
> **eval 引号**：`eval` 的 JS 内避免内嵌双引号（会被 CLI 参数解析破坏），一律用单引号
> 或 `filter(t=>t.name==='...')` 写法。

### 6.1 W-ADMIN-01 游客访问管理后台重定向登录 ✅

```powershell
playwright-cli goto http://127.0.0.1:8080/admin/problems; Start-Sleep -Seconds 1
# 输出：URL → http://127.0.0.1:8080/login，标题 登录 - CPP OJ（RequireAdmin 未登录先回登录页）
```

### 6.2 W-ADMIN-02 普通用户访问管理后台 403 ✅

```powershell
# 注册普通用户并自动登录（webuser_20260825131249004）
playwright-cli goto http://127.0.0.1:8080/register; Start-Sleep -Seconds 1
playwright-cli fill "#username" "webuser_<时间戳>"; Start-Sleep -Seconds 1
playwright-cli fill "#password" "secret123"; Start-Sleep -Seconds 1
playwright-cli fill "#password2" "secret123"; Start-Sleep -Seconds 1
playwright-cli click "form.auth-form button[type=submit]"; Start-Sleep -Seconds 1

playwright-cli goto http://127.0.0.1:8080/admin/problems; Start-Sleep -Seconds 1
playwright-cli --raw eval "JSON.stringify({code: document.querySelector('h1.error-code')?.textContent.trim(), h2: document.querySelector('h2')?.textContent.trim(), msg: [...document.querySelectorAll('p')].map(p=>p.textContent.trim())})"
# 实测：{"code":"403","h2":"访问被拒绝","msg":["需要管理员权限。"]}
playwright-cli goto http://127.0.0.1:8080/admin/problems/new; Start-Sleep -Seconds 1
# 实测：同样 403 / 访问被拒绝（A5）
```

### 6.3 W-ADMIN-03 普通用户无管理入口 ✅

```powershell
playwright-cli --raw eval "JSON.stringify({nav: document.querySelector('nav')?.textContent.trim().replace(/\s+/g,' '), adminLink: [...document.querySelectorAll('nav a')].some(a=>a.getAttribute('href')==='/admin/problems'), user: !!document.querySelector('.navbar-user')})"
# 实测：{"nav":"题库 首页 webuser_20260825131249004登出","adminLink":false,"user":true}（FR-4.4）
```

### 6.4 W-ADMIN-04 admin 进入管理后台列表 ✅

```powershell
playwright-cli click "form.navbar-logout button"; Start-Sleep -Seconds 1
playwright-cli goto http://127.0.0.1:8080/login; Start-Sleep -Seconds 1
playwright-cli fill "#username" "admin"; Start-Sleep -Seconds 1
playwright-cli fill "#password" "admin123"; Start-Sleep -Seconds 1
playwright-cli click "form.auth-form button[type=submit]"; Start-Sleep -Seconds 1
playwright-cli goto http://127.0.0.1:8080/admin/problems; Start-Sleep -Seconds 1
playwright-cli --raw eval "JSON.stringify({title: document.title, h1: document.querySelector('h1')?.textContent.trim(), newBtn: !!document.querySelector('a[href=\u0027/admin/problems/new\u0027]'), rows: document.querySelectorAll('table.problem-table tbody tr').length})"
# 实测：{"title":"题目管理 - CPP OJ","h1":"题目管理","newBtn":true,"rows":2}
# 导航栏出现「管理」入口（a[href='/admin/problems']），FR-1.4
```

### 6.5 W-ADMIN-05 新增题目（含多组用例） ✅

新增表单默认 2 组用例（`.tc-group`）。**每组 input/expected 都必须填写非空**，留空组会导致
服务端校验失败（见 6.6 的 `ValidTestcases` 每组校验规则）。

```powershell
playwright-cli goto http://127.0.0.1:8080/admin/problems/new; Start-Sleep -Seconds 1
playwright-cli fill "#title" "webadmin_c_<时间戳>"; Start-Sleep -Seconds 1
playwright-cli select "#difficulty" "medium"; Start-Sleep -Seconds 1
playwright-cli fill "#description" "## 自动化测试题"; Start-Sleep -Seconds 1
playwright-cli fill "#tc-list .tc-group:nth-child(1) textarea[name='input[]']" "1 2"; Start-Sleep -Seconds 1
playwright-cli fill "#tc-list .tc-group:nth-child(1) textarea[name='expected[]']" "3"; Start-Sleep -Seconds 1
playwright-cli fill "#tc-list .tc-group:nth-child(2) textarea[name='input[]']" "3 4"; Start-Sleep -Seconds 1
playwright-cli fill "#tc-list .tc-group:nth-child(2) textarea[name='expected[]']" "7"; Start-Sleep -Seconds 1
playwright-cli click "form.admin-form button[type=submit]"; Start-Sleep -Seconds 1
# 输出：URL → /admin/problems，列表含新题（id=175，徽章 badge badge-medium）
```
校验（管理列表 / 题库 / 详情 / DB）：

```powershell
playwright-cli --raw eval "JSON.stringify([...document.querySelectorAll('table.problem-table tbody tr')].map(r=>({id: r.querySelector('.col-id')?.textContent.trim(), title: r.querySelector('.problem-title')?.textContent.trim(), badge: r.querySelector('.col-diff .badge')?.className})).filter(x=>x.title?.startsWith('webadmin_c_'))"
# 实测：[{"id":"175","title":"webadmin_c_20260825131707806","badge":"badge badge-medium"}]
playwright-cli goto http://127.0.0.1:8080/problems; Start-Sleep -Seconds 1
# 实测：「共 3 题」，含 /problems/175 链接；详情页描述渲染正常
wsl bash -lc "mysql -uroot -h127.0.0.1 -P3307 --table -e 'SELECT id,title,difficulty FROM cpp_oj.problems WHERE id=175; SELECT problem_id,input,expected FROM cpp_oj.testcases WHERE problem_id=175;'"
# 实测：problems 1 行 + testcases 2 组（1 2/3、3 4/7）→ 事务落库（A4）
```

### 6.6 W-ADMIN-06 新增题目校验失败不落库 ✅

两种场景实测：

```powershell
# ① 空标题/空描述直接提交 → 被浏览器原生 required 校验拦截（不发请求、无 .form-error、无落库）
playwright-cli goto http://127.0.0.1:8080/admin/problems/new; Start-Sleep -Seconds 1
playwright-cli click "form.admin-form button[type=submit]"; Start-Sleep -Seconds 1
# 实测：URL 仍为 /admin/problems/new，无 .form-error；DB MAX(id) 不变

# ② 填标题/描述、用例组全空 → 服务端 kInvalidInput 重渲染新增表单页并带错误
playwright-cli fill "#title" "webadmin_c_<时间戳>"; Start-Sleep -Seconds 1
playwright-cli fill "#description" "## 自动化测试题"; Start-Sleep -Seconds 1
playwright-cli click "form.admin-form button[type=submit]"; Start-Sleep -Seconds 1
playwright-cli --raw eval "JSON.stringify({h1: document.querySelector('h1')?.textContent.trim(), err: document.querySelector('.form-error')?.textContent.trim()})"
# 实测：{"h1":"新增题目","err":"请填写标题、Markdown 描述，并至少提供一组「输入/期望输出」均非空的测试用例。"}
#       页面仍是表单页（无 table.problem-table），URL 为 POST 地址 /admin/problems；DB 无新增
```
> 注意：用例校验为**每组**均需 input/expected 非空（`problem_service.cpp` `ValidTestcases`）。
> 新增表单默认 2 组，若只填 1 组、留空第 2 组，同样触发该校验（应填满或点「移除」）。

### 6.7 W-ADMIN-07 编辑题目预填与保存 ✅

```powershell
# 先创建 webadmin_e_<时间戳>（默认难度 easy，2 组用例），实测 id=176
playwright-cli goto http://127.0.0.1:8080/admin/problems/176/edit; Start-Sleep -Seconds 1
playwright-cli --raw eval "JSON.stringify({h1: document.querySelector('h1')?.textContent.trim(), title: document.querySelector('#title').value, diff: document.querySelector('#difficulty').value, desc: document.querySelector('#description').value, ins: [...document.querySelectorAll('textarea')].filter(t=>t.name==='input[]').map(t=>t.value), exps: [...document.querySelectorAll('textarea')].filter(t=>t.name==='expected[]').map(t=>t.value), formAction: document.querySelector('form.admin-form')?.getAttribute('action'), submitLabel: document.querySelector('form.admin-form button[type=submit]')?.textContent.trim()})"
# 实测：h1=编辑题目，title/desc 回填，diff=easy，2 组用例内容齐全，
#       formAction=/admin/problems/176/edit，submitLabel=保存修改
```

修改（标题加 `_edited`、难度改 `困难`、移除第 2 组用例并改为 1 组 `9 9/18`）并保存：

```powershell
playwright-cli fill "#title" "webadmin_e_20260825132139127_edited"; Start-Sleep -Seconds 1
playwright-cli select "#difficulty" "hard"; Start-Sleep -Seconds 1
playwright-cli click "#tc-list .tc-group:nth-child(2) .tc-remove"; Start-Sleep -Seconds 1
playwright-cli fill "#tc-list .tc-group:nth-child(1) textarea[name='input[]']" "9 9"; Start-Sleep -Seconds 1
playwright-cli fill "#tc-list .tc-group:nth-child(1) textarea[name='expected[]']" "18"; Start-Sleep -Seconds 1
playwright-cli click "form.admin-form button[type=submit]"; Start-Sleep -Seconds 1
# 实测：URL → /admin/problems；列表行 title=webadmin_e_..._edited，徽章 badge badge-hard
```

复验（重进编辑页 + DB 用例整体替换）：

```powershell
playwright-cli goto http://127.0.0.1:8080/admin/problems/176/edit; Start-Sleep -Seconds 1
# 实测：title=..._edited, diff=hard, 仅 1 组用例（9 9 / 18）
wsl bash -lc "mysql -uroot -h127.0.0.1 -P3307 --table -e 'SELECT problem_id,input,expected FROM cpp_oj.testcases WHERE problem_id=176;'"
# 实测：仅 1 行（9 9/18）→ 旧用例已删、新用例已插（事务替换，FR-4.2）
```

### 6.8 W-ADMIN-08 删除题目二次确认（取消） ✅

```powershell
# 先创建 webadmin_del_<时间戳>（2 组用例填满），实测 id=177
playwright-cli click "form.admin-delete[action*='/177/delete'] button"; Start-Sleep -Seconds 1
# 输出出现 Modal state：confirm「确定要删除该题目吗？此操作不可恢复，相关测试用例将级联删除。」
playwright-cli dialog-dismiss; Start-Sleep -Seconds 1
# 校验：未发生 POST 删除
playwright-cli --raw eval "JSON.stringify({still: [...document.querySelectorAll('table.problem-table tbody tr')].some(r=>r.querySelector('.col-id')?.textContent.trim()==='177')})"
# 实测：still=true；/problems/177 仍 200（详情页正常，FR-4.3 前端二次确认）
```

### 6.9 W-ADMIN-09 删除题目物理删除（确认） ✅

```powershell
# 先创建 webadmin_del2_<时间戳>，实测 id=178
playwright-cli click "form.admin-delete[action*='/178/delete'] button"; Start-Sleep -Seconds 1
playwright-cli dialog-accept; Start-Sleep -Seconds 1
# 输出：URL → /admin/problems（POST 后 302 回列表）
playwright-cli --raw eval "JSON.stringify({gone: ![...document.querySelectorAll('table.problem-table tbody tr')].some(r=>r.querySelector('.col-id')?.textContent.trim()==='178')})"
# 实测：gone=true（列表不再含 178）
playwright-cli goto http://127.0.0.1:8080/problems/178; Start-Sleep -Seconds 1
# 实测：标题 404 - CPP OJ，h1.error-code=404，h2=题目不存在
wsl bash -lc "mysql -uroot -h127.0.0.1 -P3307 --table -e 'SELECT COUNT(*) AS p178 FROM cpp_oj.problems WHERE id=178; SELECT COUNT(*) AS tc178 FROM cpp_oj.testcases WHERE problem_id=178;'"
# 实测：p178=0, tc178=0 → 题目与用例物理删除（CASCADE，FR-4.3）
```

### 6.10 W-ADMIN-10 新增表单用例动态增删 ✅

```powershell
playwright-cli goto http://127.0.0.1:8080/admin/problems/new; Start-Sleep -Seconds 1
playwright-cli --raw eval "JSON.stringify({groups: document.querySelectorAll('.tc-group').length})"
# 实测：groups=2
playwright-cli click "#add-tc"; Start-Sleep -Seconds 1
# 实测：groups=3
playwright-cli click "#tc-list .tc-group:nth-child(1) .tc-remove"; Start-Sleep -Seconds 1
# 实测：groups=2
playwright-cli click "#tc-list .tc-group:nth-child(1) .tc-remove"; Start-Sleep -Seconds 1
playwright-cli --raw eval "JSON.stringify({groups: document.querySelectorAll('.tc-group').length, removeHidden: [...document.querySelectorAll('.tc-remove')].map(b=>b.hidden)})"
# 实测：groups=1, removeHidden=[true] → 仅剩 1 组时「移除」隐藏（syncRemoveButtons）
```

### 6.11 实测结果汇总（2026-08-25）

| 用例 | 结论 | 备注 |
|------|------|------|
| W-ADMIN-01 | ✅ 通过 | 游客访问 `/admin/problems` → `/login` |
| W-ADMIN-02 | ✅ 通过 | `/admin/problems` 与 `/new` 均 403「访问被拒绝/需要管理员权限。」 |
| W-ADMIN-03 | ✅ 通过 | 普通用户导航无「管理」入口（adminLink=false） |
| W-ADMIN-04 | ✅ 通过 | admin 列表页：题目管理/新增按钮/导航「管理」 |
| W-ADMIN-05 | ✅ 通过 | 新增（2 组用例）→ 列表/题库/详情可见，DB 事务落库 |
| W-ADMIN-06 | ✅ 通过 | 空提交被原生校验拦截；全空用例触发服务端 `.form-error`，均不落库 |
| W-ADMIN-07 | ✅ 通过 | 预填正确；保存后列表更新（困难徽章），用例整体替换（DB 佐证） |
| W-ADMIN-08 | ✅ 通过 | confirm 取消后无 POST，题目保留，详情页仍 200 |
| W-ADMIN-09 | ✅ 通过 | confirm 确认后物理删除，列表移除、详情 404、DB 级联删除 |
| W-ADMIN-10 | ✅ 通过 | `.tc-group` 2→3→2→1，仅剩 1 组时「移除」隐藏 |

> 本手册执行后已将创建的测试题目（`webadmin_c/e/del/del2_*`）全部删除，题库恢复为题 5/6。

---

## 7. 后续模块（M6~M7）执行提示

- 沿用 §1 的启动方式与定位/取值约定；
- 命令映射见 [web自动化测试文档.md](./web自动化测试文档.md) §3.2 选择器速查表；
- M5 管理后台 confirm 弹窗：`playwright-cli dialog-accept` / `dialog-dismiss`；
- M6 的 `W-ERR-04`（停库）破坏性大，建议最后单独执行并确认 MySQL 已恢复；
- 判题相关命令（run-code 脚本注入、同步判题慢用例）见 §5。
