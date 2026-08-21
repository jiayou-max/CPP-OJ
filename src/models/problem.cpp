#include "models/problem.h"

namespace oj::models {

const char* DifficultyName(Difficulty difficulty) {
  switch (difficulty) {
    case Difficulty::kEasy: return "easy";
    case Difficulty::kMedium: return "medium";
    case Difficulty::kHard: return "hard";
  }
  return "easy";
}

const char* DifficultyLabel(Difficulty difficulty) {
  switch (difficulty) {
    case Difficulty::kEasy: return "简单";
    case Difficulty::kMedium: return "中等";
    case Difficulty::kHard: return "困难";
  }
  return "简单";
}

Difficulty DifficultyFromName(const std::string& name) {
  if (name == "medium") {
    return Difficulty::kMedium;
  }
  if (name == "hard") {
    return Difficulty::kHard;
  }
  return Difficulty::kEasy;
}

}  // namespace oj::models
