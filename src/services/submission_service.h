#pragma once

#include "models/submission.h"

namespace oj::services {

// 持久化一条提交记录到 submissions 表（FR-5.1：用户、题目、代码、状态、时间/内存）。
// 返回 true = 落库成功；false = 数据库不可用/执行失败（已打错误日志，不抛异常）。
bool CreateSubmission(const oj::models::Submission& submission);

}  // namespace oj::services
