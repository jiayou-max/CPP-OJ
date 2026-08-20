-- =====================================================================
-- 仿 LeetCode OJ：数据库建表脚本（阶段 1，对应 SPEC 4.3 数据模型）
-- ---------------------------------------------------------------------
-- 用法: mysql -uroot < db/schema.sql
-- 幂等：所有语句均可重复执行（CREATE ... IF NOT EXISTS）。
-- 全库统一 utf8mb4 / utf8mb4_unicode_ci（SPEC 4.3，与 src/db/db.cpp 一致）。
-- 仅 MySQL 8（依赖表名等数据库对象、ENUM、外键级联）。
-- =====================================================================

CREATE DATABASE IF NOT EXISTS cpp_oj
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

USE cpp_oj;

-- ---------------------------------------------------------------------
-- users：注册用户 + 内置唯一管理员（FR-1.1 ~ FR-1.4）
--   password = sha256(salt + 明文密码) 的 64 位 hex（VARCHAR(255) 留有扩展余量）
--   salt     = 16 随机字节的 32 位 hex，由 db/init_admin.cpp / 注册接口生成
--   role     = admin / user；admin 仅由初始化脚本创建（FR-1.3），注册接口不可同名
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS users (
  id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  username    VARCHAR(64)  NOT NULL,
  password    VARCHAR(255) NOT NULL,
  salt        VARCHAR(32)  NOT NULL,
  role        ENUM('admin','user') NOT NULL DEFAULT 'user',
  created_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE KEY uk_users_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---------------------------------------------------------------------
-- problems：题目（FR-2.1 / FR-4.2）
--   description = Markdown 原文，渲染时由后端 md4c 转 HTML（SPEC 4.2）
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS problems (
  id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  title       VARCHAR(255) NOT NULL,
  description MEDIUMTEXT   NOT NULL,
  difficulty  ENUM('easy','medium','hard') NOT NULL DEFAULT 'easy',
  created_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  KEY idx_problems_difficulty (difficulty)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---------------------------------------------------------------------
-- testcases：每题的输入/输出用例（FR-3.2 / FR-4.2）
--   `order` 为用例序号（ORDER 是 MySQL 保留字，必须反引号）；
--   独立成表便于逐用例管理（SPEC 4.3 说明）。
--   题目物理删除时级联删除其全部用例（FR-4.3，SPEC 4.3 级联删除）。
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS testcases (
  id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  problem_id  BIGINT UNSIGNED NOT NULL,
  `order`     INT NOT NULL,
  input       TEXT NOT NULL,
  expected    TEXT NOT NULL,
  CONSTRAINT fk_testcases_problem
    FOREIGN KEY (problem_id) REFERENCES problems (id)
    ON DELETE CASCADE,
  KEY idx_testcases_problem (problem_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ---------------------------------------------------------------------
-- submissions：提交记录（FR-5.1）
--   status = 判题总体状态（AC/WA/TLE/MLE/RE/CE，FR-3.3）
--   time_ms / memory_kb = 本次实际运行时间与内存峰值（FR-3.4）
--   题目物理删除时级联删除其提交（FR-4.3「物理删除，无回收站」，
--   保证 admin 删除题目不会因外键阻塞）；user 无删除功能，故 RESTRICT。
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS submissions (
  id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  user_id     BIGINT UNSIGNED NOT NULL,
  problem_id  BIGINT UNSIGNED NOT NULL,
  code        MEDIUMTEXT NOT NULL,
  status      ENUM('ac','wa','tle','mle','re','ce') NOT NULL,
  time_ms     INT NOT NULL DEFAULT 0,
  memory_kb   INT NOT NULL DEFAULT 0,
  created_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT fk_submissions_user
    FOREIGN KEY (user_id) REFERENCES users (id)
    ON DELETE RESTRICT,
  CONSTRAINT fk_submissions_problem
    FOREIGN KEY (problem_id) REFERENCES problems (id)
    ON DELETE CASCADE,
  KEY idx_submissions_user (user_id),
  KEY idx_submissions_problem (problem_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
