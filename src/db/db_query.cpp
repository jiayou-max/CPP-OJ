#include "db/db_query.h"

#include <cstring>

#include "logger.h"

namespace oj::db {

PreparedStatement::PreparedStatement(PooledConnection conn, const std::string& sql)
    : conn_(std::move(conn)) {
  if (!conn_) {
    error_ = "无可用数据库连接";
    return;
  }

  MYSQL* mysql = conn_.get();
  stmt_ = mysql_stmt_init(mysql);
  if (stmt_ == nullptr) {
    error_ = "mysql_stmt_init 失败（内存不足）";
    return;
  }

  if (mysql_stmt_prepare(stmt_, sql.data(), static_cast<unsigned long>(sql.size())) != 0) {
    error_ = mysql_stmt_error(stmt_);
    return;
  }

  prepare_ok_ = true;
}

PreparedStatement::~PreparedStatement() {
  if (stmt_ != nullptr) {
    mysql_stmt_free_result(stmt_);
    mysql_stmt_close(stmt_);
    stmt_ = nullptr;
  }
}

PreparedStatement::PreparedStatement(PreparedStatement&& other) noexcept
    : conn_(std::move(other.conn_)),
      stmt_(other.stmt_),
      prepare_ok_(other.prepare_ok_),
      error_(std::move(other.error_)),
      params_(std::move(other.params_)),
      result_binds_(std::move(other.result_binds_)),
      result_buffers_(std::move(other.result_buffers_)),
      result_lengths_(std::move(other.result_lengths_)),
      result_null_(std::move(other.result_null_)),
      column_count_(other.column_count_) {
  other.stmt_ = nullptr;
  other.prepare_ok_ = false;
  other.column_count_ = 0;
}

PreparedStatement& PreparedStatement::operator=(PreparedStatement&& other) noexcept {
  if (this != &other) {
    if (stmt_ != nullptr) {
      mysql_stmt_free_result(stmt_);
      mysql_stmt_close(stmt_);
    }
    conn_ = std::move(other.conn_);
    stmt_ = other.stmt_;
    prepare_ok_ = other.prepare_ok_;
    error_ = std::move(other.error_);
    params_ = std::move(other.params_);
    result_binds_ = std::move(other.result_binds_);
    result_buffers_ = std::move(other.result_buffers_);
    result_lengths_ = std::move(other.result_lengths_);
    result_null_ = std::move(other.result_null_);
    column_count_ = other.column_count_;
    other.stmt_ = nullptr;
    other.prepare_ok_ = false;
    other.column_count_ = 0;
  }
  return *this;
}

void PreparedStatement::Bind(const std::string& value) {
  Param p;
  p.is_string = true;
  p.str = value;
  p.str_len = static_cast<unsigned long>(value.size());
  params_.push_back(std::move(p));
}

void PreparedStatement::Bind(long long value) {
  Param p;
  p.is_string = false;
  p.integer = value;
  params_.push_back(p);
}

bool PreparedStatement::Execute() {
  if (!prepare_ok_) {
    LogError("db_query: 语句未准备成功，无法执行: " + error_);
    return false;
  }

  // 构建参数绑定数组（字符串参数需在其生命周期内保持有效 → 存于 params_）。
  std::vector<MYSQL_BIND> binds(params_.size());
  for (std::size_t i = 0; i < params_.size(); ++i) {
    MYSQL_BIND& b = binds[i];
    std::memset(&b, 0, sizeof(b));
    if (params_[i].is_string) {
      b.buffer_type = MYSQL_TYPE_STRING;
      b.buffer = params_[i].str.data();
      b.buffer_length = params_[i].str_len;
      b.length = &params_[i].str_len;
    } else {
      b.buffer_type = MYSQL_TYPE_LONGLONG;
      b.buffer = &params_[i].integer;
    }
  }

  if (mysql_stmt_bind_param(stmt_, binds.data()) != 0) {
    error_ = mysql_stmt_error(stmt_);
    LogError("db_query: 绑定参数失败: " + error_);
    return false;
  }

  if (mysql_stmt_execute(stmt_) != 0) {
    error_ = mysql_stmt_error(stmt_);
    LogError("db_query: 执行失败: " + error_);
    return false;
  }

  // 参数缓冲仅在本次 execute 期间被读取，执行完成后即可释放，
  // 以便同一语句重复执行时重新 Bind（params_ 不累计）。
  params_.clear();

  return AllocateResults();
}

bool PreparedStatement::AllocateResults() {
  result_binds_.clear();
  result_buffers_.clear();
  result_lengths_.clear();
  result_null_.reset();
  column_count_ = 0;

  MYSQL_RES* meta = mysql_stmt_result_metadata(stmt_);
  if (meta == nullptr) {
    // 非查询语句（INSERT/UPDATE/DELETE）没有结果集，直接成功。
    return true;
  }

  column_count_ = mysql_num_fields(meta);
  MYSQL_FIELD* fields = mysql_fetch_fields(meta);
  if (column_count_ == 0) {
    mysql_free_result(meta);
    return true;
  }

  result_binds_.resize(column_count_);
  result_buffers_.resize(column_count_);
  result_lengths_.assign(column_count_, 0);
  result_null_ = std::make_unique<bool[]>(column_count_);
  for (unsigned int i = 0; i < column_count_; ++i) {
    // 全按字符串读（MySQL 自动做类型转换）。初始缓冲取字段最大可能长度，
    // 实际值更长时在 Fetch 中按截断长度扩容重取。
    unsigned long cap = fields[i].max_length > 0 ? fields[i].max_length
                                                 : fields[i].length;
    if (cap == 0) {
      cap = 4096;
    }
    result_buffers_[i].resize(cap + 1, '\0');

    MYSQL_BIND& b = result_binds_[i];
    std::memset(&b, 0, sizeof(b));
    b.buffer_type = MYSQL_TYPE_STRING;
    b.buffer = result_buffers_[i].data();
    b.buffer_length = static_cast<unsigned long>(result_buffers_[i].size());
    b.length = &result_lengths_[i];
    b.is_null = result_null_.get() + i;
  }
  mysql_free_result(meta);

  if (mysql_stmt_bind_result(stmt_, result_binds_.data()) != 0) {
    error_ = mysql_stmt_error(stmt_);
    LogError("db_query: 绑定结果失败: " + error_);
    return false;
  }
  return true;
}

bool PreparedStatement::Fetch() {
  if (column_count_ == 0) {
    return false;
  }

  for (;;) {
    const int rc = mysql_stmt_fetch(stmt_);
    if (rc == 0) {
      return true;
    }
    if (rc == MYSQL_DATA_TRUNCATED) {
      // 某列缓冲区不足：按实际长度扩容后重绑，再取本行完整数据。
      // （truncated 时行游标不前进，重新 fetch 会返回同一行。）
      bool resized = false;
      for (unsigned int i = 0; i < column_count_; ++i) {
        if (!result_null_[i] && result_lengths_[i] + 1 > result_buffers_[i].size()) {
          result_buffers_[i].resize(result_lengths_[i] + 1, '\0');
          result_binds_[i].buffer = result_buffers_[i].data();
          result_binds_[i].buffer_length =
              static_cast<unsigned long>(result_buffers_[i].size());
          resized = true;
        }
      }
      if (resized && mysql_stmt_bind_result(stmt_, result_binds_.data()) == 0) {
        continue;
      }
      return false;
    }
    if (rc == MYSQL_NO_DATA) {
      return false;
    }
    error_ = mysql_stmt_error(stmt_);
    LogError("db_query: 取行失败: " + error_);
    return false;
  }
}

bool PreparedStatement::IsNull(unsigned int col) const {
  if (col >= column_count_) {
    return true;
  }
  return result_null_[col];
}

std::string PreparedStatement::GetString(unsigned int col) const {
  if (col >= column_count_) {
    return {};
  }
  if (result_null_[col]) {
    return {};
  }
  return result_buffers_[col].substr(0, result_lengths_[col]);
}

unsigned long long PreparedStatement::AffectedRows() const {
  return stmt_ != nullptr ? mysql_stmt_affected_rows(stmt_) : 0;
}

unsigned long long PreparedStatement::InsertId() const {
  return stmt_ != nullptr ? mysql_stmt_insert_id(stmt_) : 0;
}

}  // namespace oj::db
