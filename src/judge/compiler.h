#pragma once

#include <string>

#include "config.h"

namespace oj::judge {

// 编译结果（对应 SPEC 4.4「编译器封装」：
//   临时目录 + g++ 编译 + 编译超时兜底 + CE 日志捕获）。
struct CompileResult {
  bool ok = false;         // 是否编译成功（产出可执行文件）
  bool timeout = false;    // 编译超时（>kCompileTimeoutSec 兜底 → CE 标注「编译超时」）
  std::string error_log;   // CE：完整 g++ stderr 日志
};

// g++ 编译器封装：创建随机临时目录 → 写 main.cpp → g++ -O2 编译，
// 提供编译超时兜底并捕获 CE 日志。编译成功后可执行文件保留在临时目录，
// 供判题运行阶段（runner）后续复用。
class Compiler {
 public:
  Compiler() = default;
  ~Compiler();

  Compiler(const Compiler&) = delete;
  Compiler& operator=(const Compiler&) = delete;

  // 编译用户代码。成功返回 ok=true，且可从 binary_path() 取得可执行文件；
  // 失败返回 CE（error_log 含完整 g++ stderr），超时额外置 timeout=true。
  CompileResult Compile(const std::string& source_code);

  // 编译临时目录 / 可执行文件路径（编译成功后有效，判题结束调用 Cleanup()）。
  const std::string& work_dir() const { return work_dir_; }
  const std::string& binary_path() const { return binary_path_; }

  // 覆盖编译超时秒数（默认 kCompileTimeoutSec；测试可缩小以快速验证超时兜底）。
  void set_compile_timeout_sec(unsigned int sec) { compile_timeout_sec_ = sec; }

  // 递归删除临时目录（幂等；析构自动调用）。
  void Cleanup();

 private:
  std::string work_dir_;
  std::string binary_path_;
  unsigned int compile_timeout_sec_ = kCompileTimeoutSec;
};

}  // namespace oj::judge
