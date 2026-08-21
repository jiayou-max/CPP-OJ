#pragma once

#include <memory>
#include <string>
#include <vector>

#include <mysql/mysql.h>

#include "db/db.h"

namespace oj::db {

// 预编译语句（PREPARE/EXECUTE）RAII 封装（SPEC 4.6 src/db/db_query.h|.cpp）。
//
// 安全基础（D13 / NFR-3）：动态参数一律经 mysql_stmt_bind_param 绑定，由
// MySQL 服务端处理转义，杜绝 SQL 注入（不使用字符串拼接）。
//
// 用法（连接来自连接池，对象析构时自动归还）：
//   PreparedStatement stmt(DbPool()->Acquire(),
//                          "SELECT id,title,description,difficulty FROM problems WHERE id=?");
//   stmt.Bind(123LL);
//   if (!stmt.Execute()) return false;
//   while (stmt.Fetch()) { const auto t = stmt.GetString(1); }
//
// Move-only（持有连接，不可被拷贝）。
class PreparedStatement {
 public:
  // 构造即 prepare。conn 必须有效（Acquire() 成功）；prepare 失败时
  // PrepareOk() 返回 false，Error() 给出原因（SQL 错误 / 表不存在等）。
  PreparedStatement(PooledConnection conn, const std::string& sql);
  ~PreparedStatement();

  PreparedStatement(const PreparedStatement&) = delete;
  PreparedStatement& operator=(const PreparedStatement&) = delete;
  PreparedStatement(PreparedStatement&& other) noexcept;
  PreparedStatement& operator=(PreparedStatement&& other) noexcept;

  bool PrepareOk() const { return prepare_ok_; }
  const std::string& Error() const { return error_; }

  // 绑定参数：按 SQL 中 ? 的先后顺序调用（字符串或 64 位整数）。
  // 返回值恒为 true（参数校验延迟到 Execute 由 MySQL 报错）；仅记录状态。
  void Bind(const std::string& value);
  void Bind(long long value);

  // 执行语句（内部完成参数绑定）。失败返回 false 并记录 Error()。
  bool Execute();

  // 前进到下一结果行；返回 false 表示行集耗尽（或非查询语句）。
  bool Fetch();

  // 当前行某列是否为 NULL。
  bool IsNull(unsigned int col) const;

  // 当前行某列以字符串读取（NULL 视为空串）。col 越界返回空串。
  std::string GetString(unsigned int col) const;

  // 列数（仅 SELECT 有结果集；INSERT/UPDATE/DELETE 为 0）。
  unsigned int ColumnCount() const { return column_count_; }

  // 影响行数（Execute 后有效）。
  unsigned long long AffectedRows() const;

  // 自增主键（INSERT 后有效）。
  unsigned long long InsertId() const;

 private:
  // 依据执行后的结果元数据分配结果缓冲区并绑定到语句。
  bool AllocateResults();

  // 已收集的绑定参数（延迟到 Execute 统一构建 MYSQL_BIND 数组）。
  struct Param {
    bool is_string = true;
    std::string str;
    long long integer = 0;
    unsigned long str_len = 0;
  };

  PooledConnection conn_;
  MYSQL_STMT* stmt_ = nullptr;
  bool prepare_ok_ = false;
  std::string error_;

  std::vector<Param> params_;

  // 结果绑定缓冲（全按字符串读，GetString 用）。
  // result_null_ 用堆上 bool 数组（vector<bool> 位压缩，无法取元素地址供
  // MYSQL_BIND::is_null 使用）。
  std::vector<MYSQL_BIND> result_binds_;
  std::vector<std::string> result_buffers_;
  std::vector<unsigned long> result_lengths_;
  std::unique_ptr<bool[]> result_null_;
  unsigned int column_count_ = 0;
};

}  // namespace oj::db
