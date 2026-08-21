#include "models/submission.h"

namespace oj::models {

const char* SubmissionStatusName(SubmissionStatus status) {
  switch (status) {
    case SubmissionStatus::kAc: return "ac";
    case SubmissionStatus::kWa: return "wa";
    case SubmissionStatus::kTle: return "tle";
    case SubmissionStatus::kMle: return "mle";
    case SubmissionStatus::kRe: return "re";
    case SubmissionStatus::kCe: return "ce";
  }
  return "ac";
}

const char* SubmissionStatusLabel(SubmissionStatus status) {
  switch (status) {
    case SubmissionStatus::kAc: return "通过 (AC)";
    case SubmissionStatus::kWa: return "答案错误 (WA)";
    case SubmissionStatus::kTle: return "运行超时 (TLE)";
    case SubmissionStatus::kMle: return "内存超限 (MLE)";
    case SubmissionStatus::kRe: return "运行错误 (RE)";
    case SubmissionStatus::kCe: return "编译错误 (CE)";
  }
  return "通过 (AC)";
}

SubmissionStatus SubmissionStatusFromName(const std::string& name) {
  if (name == "wa") return SubmissionStatus::kWa;
  if (name == "tle") return SubmissionStatus::kTle;
  if (name == "mle") return SubmissionStatus::kMle;
  if (name == "re") return SubmissionStatus::kRe;
  if (name == "ce") return SubmissionStatus::kCe;
  return SubmissionStatus::kAc;
}

}  // namespace oj::models
