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

## 3. 后续模块（M2~M7）执行提示

- 沿用 §1 的启动方式与定位/取值约定；
- 命令映射见 [web自动化测试文档.md](./web自动化测试文档.md) §3.2 选择器速查表；
- 注册/登录/登出、ace 编辑器填码、判题提交等交互命令在本手册持续补充。
