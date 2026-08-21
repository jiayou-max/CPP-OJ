#pragma once

#include <string>

namespace oj::models {

// 题目难度（problems.difficulty，SPEC 4.3）。
enum class Difficulty {
  kEasy,
  kMedium,
  kHard,
};

// 题目（problems 表一行，对应 SPEC 4.6 src/models/problem.h）。
struct Problem {
  long long id = 0;
  std::string title;       // 题目标题
  std::string description; // Markdown 原文（渲染由 markdown/md.h 负责）
  Difficulty difficulty = Difficulty::kEasy;
};

// 难度枚举 → 小写字符串（easy/medium/hard，与库中 ENUM 一致）。
const char* DifficultyName(Difficulty difficulty);

// 难度枚举 → 中文显示名（视图层用于列表/详情展示）。
const char* DifficultyLabel(Difficulty difficulty);

// 字符串 → 难度枚举；非法值返回 kEasy。
Difficulty DifficultyFromName(const std::string& name);

}  // namespace oj::models
