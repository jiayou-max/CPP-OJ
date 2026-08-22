#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CPP OJ 接口自动化测试（Python 实现）

依据《接口自动化测试文档.md》整理，使用 requests 构造 HTTP 请求逐项验证接口，
可用于服务端回归测试（改代码后重跑）。

前置条件：
  - 服务已在本机 base 上运行（默认 http://localhost:8080）
  - 题 5 有测试用例（两数之和），题 6 无测试用例（用于 500 场景）
  - MySQL 可达（落库校验仅打印，不做硬断言）

用法：
  python3 src/test/python/test_api.py [--base http://localhost:8080] [--full]

  --full  额外执行耗时场景（TLE 死循环、MLE 内存超限）

退出码：0 = 全部通过；1 = 存在失败用例。
"""

import argparse
import re
import sys
import time

import requests

DEFAULT_BASE = "http://localhost:8080"

# 题 5（两数之和）正确解，输入 n / 数组 / target，输出 i j
AC_CODE = """#include <iostream>
#include <vector>
using namespace std;
int main(){int n;cin>>n;vector<int>a(n);for(int i=0;i<n;i++)cin>>a[i];int t;cin>>t;for(int i=0;i<n;i++)for(int j=i+1;j<n;j++)if(a[i]+a[j]==t){cout<<i<<" "<<j<<"\\n";return 0;}return 0;}"""

WA_CODE = '#include <iostream>\nint main(){std::cout<<"wrong answer\\n";return 0;}'
RE_CODE = 'int main(){int a=0;return 1/a;}'
CE_CODE = 'this is not valid c++'
OK_CODE = 'int main(){return 0;}'
TLE_CODE = 'int main(){while(true){}}'
MLE_CODE = '#include <vector>\nint main(){std::vector<int*> v;while(true){v.push_back(new int[100000]);}}'

_PASS = "PASS"
_FAIL = "FAIL"


class ApiTester:
    def __init__(self, base, full=False):
        self.base = base.rstrip("/")
        self.full = full
        self.results = []  # (desc, ok, detail)

    # ---------- 结果收集 ----------
    def check(self, desc, ok, detail=""):
        tag = _PASS if ok else _FAIL
        suffix = f"  [{detail}]" if detail else ""
        print(f"{tag}: {desc}{suffix}")
        self.results.append((desc, ok, detail))

    def request(self, method, path, session=None, **kwargs):
        """发请求并捕获异常，失败返回 (None, 异常信息)。"""
        url = self.base + path
        try:
            resp = (session or requests).request(method, url, timeout=60, **kwargs)
            return resp, None
        except requests.RequestException as exc:
            return None, str(exc)

    def html_get(self, session, path, allow_redirects=False):
        resp, err = self.request("GET", path, session=session,
                                 allow_redirects=allow_redirects)
        if resp is None:
            return None, err
        resp.encoding = resp.apparent_encoding or "utf-8"
        return resp, err

    def form_post(self, session, path, data, allow_redirects=False):
        resp, err = self.request("POST", path, session=session,
                                 data=data,
                                 headers={"Content-Type": "application/x-www-form-urlencoded"},
                                 allow_redirects=allow_redirects)
        if resp is None:
            return None, err
        resp.encoding = resp.apparent_encoding or "utf-8"
        return resp, err

    # ---------- 2. 页面路由（GET） ----------
    def test_page_routes(self):
        print("\n== 2. 页面路由 (GET) ==")
        s = requests.Session()

        # 2.1 GET / -> 302 /problems
        resp, err = self.html_get(s, "/")
        if err:
            self.check("GET / 302 重定向", False, err)
        else:
            loc = resp.headers.get("Location", "")
            self.check("GET / 302 重定向",
                       resp.status_code == 302 and loc.endswith("/problems"),
                       f"status={resp.status_code} location={loc}")

        # 2.2 GET /problems -> 200 题库页
        resp, err = self.html_get(s, "/problems")
        if err:
            self.check("GET /problems 200 题库页", False, err)
        else:
            title = re.search(r"<title>([^<]*)</title>", resp.text)
            self.check("GET /problems 200 题库页",
                       resp.status_code == 200 and title is not None and
                       title.group(1) == "题库 - CPP OJ",
                       f"status={resp.status_code} title={title.group(1) if title else '?'}")

        # 2.3 GET /problems/{id}
        resp, err = self.html_get(s, "/problems/5")
        if err:
            self.check("GET /problems/5 200 详情页", False, err)
        else:
            self.check("GET /problems/5 200 详情页", resp.status_code == 200,
                       f"status={resp.status_code}")
            hits = (resp.text.count('name="code"') +
                    resp.text.count("/problems/5/submit"))
            self.check("详情页含编辑器与提交表单", hits >= 2, f"hits={hits}")

        resp, err = self.html_get(s, "/problems/abc")
        self.check("GET /problems/abc 404（id 非法）",
                   resp is not None and resp.status_code == 404,
                   "err" if err else f"status={resp.status_code}")
        resp, err = self.html_get(s, "/problems/999")
        self.check("GET /problems/999 404（不存在）",
                   resp is not None and resp.status_code == 404,
                   "err" if err else f"status={resp.status_code}")

        # 2.4 GET /login / /register -> 200 表单页
        for path in ("/login", "/register"):
            resp, err = self.html_get(s, path)
            self.check(f"GET {path} 200 表单页",
                       resp is not None and resp.status_code == 200,
                       "err" if err else f"status={resp.status_code}")

        # 2.5 GET /static/*
        resp, err = self.html_get(s, "/static/css/style.css")
        ctype = resp.headers.get("Content-Type", "") if resp is not None else ""
        self.check("GET /static/css/style.css 200 静态资源",
                   resp is not None and resp.status_code == 200 and
                   ctype.startswith("text/css"),
                   "err" if err else f"status={resp.status_code} type={ctype}")

        # 2.6 未匹配路由 -> 404 错误页
        resp, err = self.html_get(s, "/does-not-exist")
        title = re.search(r"<title>([^<]*)</title>", resp.text) if resp is not None else None
        self.check("GET /does-not-exist 404 错误页",
                   resp is not None and resp.status_code == 404 and
                   title is not None and "404" in title.group(1),
                   "err" if err else
                   f"status={resp.status_code if resp is not None else '?'} "
                   f"title={title.group(1) if title else '?'}")

    # ---------- 3. 注册 / 登录 / 登出（POST） ----------
    def test_auth(self):
        print("\n== 3. 注册 / 登录 / 登出 (POST) ==")
        self.username = f"py_test_{int(time.time())}"

        reg = requests.Session()
        # 3.1 注册成功 -> 302 + Set-Cookie
        resp, err = self.form_post(reg, "/register",
                                   {"username": self.username,
                                    "password": "secret123"})
        loc = resp.headers.get("Location", "") if resp is not None else ""
        setcookie = resp.headers.get("Set-Cookie", "") if resp is not None else ""
        self.check("POST /register 302 + Set-Cookie + Location /problems",
                   not err and resp is not None and resp.status_code == 302 and
                   "oj_session=" in setcookie and loc.endswith("/problems"),
                   "err" if err else
                   f"status={resp.status_code if resp is not None else '?'} set-cookie={'yes' if 'oj_session=' in setcookie else 'no'} loc={loc}")

        # 3.2 注册校验失败
        resp, err = self.form_post(reg, "/register",
                                   {"username": "b@d!name", "password": "secret123"})
        self.check("POST /register 非法用户名 200 + 错误提示",
                   not err and resp is not None and resp.status_code == 200 and
                   "用户名仅允许字母/数字/下划线" in resp.text,
                   "err" if err else
                   f"status={resp.status_code if resp is not None else '?'}")

        # 用户名已存在（重复注册）
        resp, err = self.form_post(reg, "/register",
                                   {"username": self.username,
                                    "password": "secret123"})
        self.check("POST /register 用户名已存在 200 + 错误提示",
                   not err and resp is not None and resp.status_code == 200 and
                   "用户名已存在，请换一个。" in resp.text,
                   "err" if err else
                   f"status={resp.status_code if resp is not None else '?'}")

        # 保留字 admin
        resp, err = self.form_post(reg, "/register",
                                   {"username": "admin", "password": "admin123"})
        self.check("POST /register 保留字 admin 200 + 错误提示",
                   not err and resp is not None and resp.status_code == 200 and
                   "用户名已存在" in resp.text,
                   "err" if err else
                   f"status={resp.status_code if resp is not None else '?'}")

        # 3.3 登录成功 -> 302 + Set-Cookie
        self.sess = requests.Session()
        resp, err = self.form_post(self.sess, "/login",
                                   {"username": self.username,
                                    "password": "secret123"})
        loc = resp.headers.get("Location", "") if resp is not None else ""
        setcookie = resp.headers.get("Set-Cookie", "") if resp is not None else ""
        self.check("POST /login 302 + Set-Cookie + Location /problems",
                   not err and resp is not None and resp.status_code == 302 and
                   "oj_session=" in setcookie and loc.endswith("/problems"),
                   "err" if err else
                   f"status={resp.status_code if resp is not None else '?'} set-cookie={'yes' if 'oj_session=' in setcookie else 'no'} loc={loc}")

        # 3.4 登录失败（统一文案，防枚举）
        for name, pw in ((self.username, "wrongpass"), ("nouser_xyz", "whatever1")):
            resp, err = self.form_post(self.sess, "/login",
                                       {"username": name, "password": pw})
            self.check(f"POST /login 失败 {name} -> 200 统一文案",
                       not err and resp is not None and resp.status_code == 200 and
                       "用户名或密码错误。" in resp.text,
                       "err" if err else
                       f"status={resp.status_code if resp is not None else '?'}")

        # 已登录访问 /login -> 302 /problems
        resp, err = self.html_get(self.sess, "/login")
        loc = resp.headers.get("Location", "") if resp is not None else ""
        self.check("GET /login（已登录）302 -> /problems",
                   not err and resp is not None and resp.status_code == 302 and
                   loc.endswith("/problems"),
                   "err" if err else
                   f"status={resp.status_code if resp is not None else '?'} loc={loc}")

        # 3.5 登出
        # 未登录 -> 302 /login
        anon = requests.Session()
        resp, err = self.form_post(anon, "/logout", {})
        loc = resp.headers.get("Location", "") if resp is not None else ""
        self.check("POST /logout 未登录 302 -> /login",
                   not err and resp is not None and resp.status_code == 302 and
                   loc.endswith("/login"),
                   "err" if err else
                   f"status={resp.status_code if resp is not None else '?'} loc={loc}")

        # 已登录 -> 302 /problems（注意必须带表单体）
        resp, err = self.form_post(self.sess, "/logout", {})
        loc = resp.headers.get("Location", "") if resp is not None else ""
        self.check("POST /logout 已登录 302 -> /problems",
                   not err and resp is not None and resp.status_code == 302 and
                   loc.endswith("/problems"),
                   "err" if err else
                   f"status={resp.status_code if resp is not None else '?'} loc={loc}")

        # 登出后会话失效：重新登录供后续判题使用
        resp, err = self.form_post(self.sess, "/login",
                                   {"username": self.username,
                                    "password": "secret123"})
        self.check("登出后重新登录成功",
                   not err and resp is not None and resp.status_code == 302,
                   "err" if err else f"status={resp.status_code if resp is not None else '?'}")

    # ---------- 4. 判题提交（POST /problems/{id}/submit） ----------
    def test_submit(self):
        print("\n== 4. 判题提交 (POST /problems/{id}/submit) ==")

        # 4.1 访问控制：未登录 -> 302 /login
        anon = requests.Session()
        resp, err = self.form_post(anon, "/problems/5/submit", {"code": "x"})
        loc = resp.headers.get("Location", "") if resp is not None else ""
        self.check("submit 未登录 302 -> /login",
                   not err and resp is not None and resp.status_code == 302 and
                   loc.endswith("/login"),
                   "err" if err else
                   f"status={resp.status_code if resp is not None else '?'} loc={loc}")

        def submit(code, path="/problems/5/submit", expected_status=200, needles=()):
            resp, err = self.form_post(self.sess, path, {"code": code})
            if err:
                return None, err
            ok_status = resp.status_code == expected_status
            ok_needles = all(needle in resp.text for needle in needles)
            return resp, ok_status and ok_needles

        # 4.2 AC
        resp, ok = submit(AC_CODE, needles=("通过 (AC)", "用时"))
        self.check("submit AC -> 200 通过 (AC) + 用时统计", ok,
                   "err" if isinstance(ok, str) else
                   f"status={resp.status_code if resp is not None else '?'}")

        # WA
        resp, ok = submit(WA_CODE, needles=("答案错误 (WA)",))
        self.check("submit WA -> 答案错误 (WA)", ok,
                   "err" if isinstance(ok, str) else
                   f"status={resp.status_code if resp is not None else '?'}")

        # RE（除零崩溃）
        resp, ok = submit(RE_CODE, needles=("运行错误 (RE)",))
        self.check("submit RE -> 运行错误 (RE)", ok,
                   "err" if isinstance(ok, str) else
                   f"status={resp.status_code if resp is not None else '?'}")

        # CE（非法代码 + 编译日志）
        resp, ok = submit(CE_CODE, needles=("编译错误 (CE)", "编译错误日志"))
        self.check("submit CE -> 编译错误 (CE) + 编译错误日志", ok,
                   "err" if isinstance(ok, str) else
                   f"status={resp.status_code if resp is not None else '?'}")

        if self.full:
            resp, ok = submit(TLE_CODE, needles=("超时 (TLE)",))
            self.check("submit TLE -> 超时 (TLE)", ok,
                       "err" if isinstance(ok, str) else
                       f"status={resp.status_code if resp is not None else '?'}")
            resp, ok = submit(MLE_CODE, needles=("内存超限 (MLE)",))
            self.check("submit MLE -> 内存超限 (MLE)", ok,
                       "err" if isinstance(ok, str) else
                       f"status={resp.status_code if resp is not None else '?'}")

        # 4.3 异常路径
        resp, ok = submit(OK_CODE, path="/problems/6/submit", expected_status=500)
        self.check("submit 题 6 无测试用例 -> 500 判题不可用", ok,
                   "err" if isinstance(ok, str) else
                   f"status={resp.status_code if resp is not None else '?'}")
        resp, ok = submit(OK_CODE, path="/problems/abc/submit", expected_status=404)
        self.check("submit 非法 id -> 404", ok,
                   "err" if isinstance(ok, str) else
                   f"status={resp.status_code if resp is not None else '?'}")
        resp, ok = submit(OK_CODE, path="/problems/999/submit", expected_status=404)
        self.check("submit 题目不存在 -> 404", ok,
                   "err" if isinstance(ok, str) else
                   f"status={resp.status_code if resp is not None else '?'}")

    # ---------- 4.4 落库校验（可选，需 MySQL 可达） ----------
    def check_db(self):
        print("\n== 4.4 落库校验（仅打印，MySQL 不可达时跳过） ==")
        try:
            import mysql.connector
        except ImportError:
            print("SKIP: 未安装 mysql-connector-python，跳过落库校验")
            return
        try:
            conn = mysql.connector.connect(host="127.0.0.1", port=3307,
                                           user="root", database="cpp_oj")
            cur = conn.cursor()
            cur.execute("SELECT id,user_id,problem_id,status,time_ms,memory_kb "
                        "FROM submissions ORDER BY id DESC LIMIT 5;")
            rows = cur.fetchall()
            conn.close()
        except Exception as exc:
            print(f"SKIP: 落库校验失败（{exc}）")
            return
        for row in rows:
            print("  submission:", row)

    # ---------- 汇总 ----------
    def summary(self):
        total = len(self.results)
        passed = sum(1 for _, ok, _ in self.results if ok)
        failed = total - passed
        print(f"\n{'=' * 60}")
        print(f"结果汇总: {passed}/{total} 通过")
        if failed:
            print("失败用例:")
            for desc, ok, detail in self.results:
                if not ok:
                    print(f"  FAIL: {desc}  [{detail}]")
        return failed == 0


def main():
    parser = argparse.ArgumentParser(description="CPP OJ 接口自动化测试")
    parser.add_argument("--base", default=DEFAULT_BASE, help="服务地址，默认 http://localhost:8080")
    parser.add_argument("--full", action="store_true", help="额外执行 TLE / MLE 耗时场景")
    args = parser.parse_args()

    tester = ApiTester(args.base, full=args.full)
    tester.test_page_routes()
    tester.test_auth()
    tester.test_submit()
    tester.check_db()
    ok = tester.summary()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
