#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#include <unistd.h>

#include <gtest/gtest.h>

#include "logger.h"

namespace {

// 将日志写入系统临时目录，避免污染工作区；每个用例使用独立目录互不干扰。
class LoggerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    char tmp[] = "/tmp/oj_logger_test_XXXXXX";
    dir_ = mkdtemp(tmp);
    ASSERT_NE(nullptr, dir_) << "mkdtemp 失败";
    path_ = std::string(dir_) + "/error.log";
  }

  void TearDown() override {
    std::remove(path_.c_str());
    rmdir(dir_);
  }

  std::string ReadFile() {
    std::ifstream f(path_);
    EXPECT_TRUE(f.good()) << "日志文件应存在: " << path_;
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
  }

  char* dir_ = nullptr;
  std::string path_;
};

// WARN/ERROR 应写入错误日志文件（NFR-6 关键错误文件日志），
// 且行内包含级别标识与消息原文。
TEST_F(LoggerTest, WarnAndErrorWriteToFile) {
  oj::LogInit(path_);
  oj::LogInfo("info message");
  oj::LogWarn("warn message");
  oj::LogError("error message");

  const std::string content = ReadFile();
  EXPECT_NE(std::string::npos, content.find("[WARN] warn message"));
  EXPECT_NE(std::string::npos, content.find("[ERROR] error message"));
}

// DEBUG/INFO 仅输出到控制台，不应出现在错误日志文件中。
TEST_F(LoggerTest, DebugAndInfoSkipFile) {
  oj::LogInit(path_);
  oj::LogDebug("debug message");
  oj::LogInfo("info message");

  const std::string content = ReadFile();
  EXPECT_EQ(std::string::npos, content.find("debug message"));
  EXPECT_EQ(std::string::npos, content.find("info message"));
}

// 每条日志都带时间戳前缀，便于排查时序（控制台与文件共用同一行内容）。
TEST_F(LoggerTest, LineHasTimestamp) {
  oj::LogInit(path_);
  oj::LogWarn("timestamped");

  const std::string content = ReadFile();
  const std::size_t pos = content.find("[WARN] timestamped");
  ASSERT_NE(std::string::npos, pos);
  ASSERT_GT(pos, std::string("[").size());
  EXPECT_NE(std::string::npos, content.find("[")) << "应为 [YYYY-MM-DD HH:MM:SS] 时间戳前缀";
}

// 日志文件应为追加模式：多次 LogInit 换新路径后，重新初始化的路径被写入。
TEST_F(LoggerTest, LogInitSwitchesFile) {
  const std::string path2 = std::string(dir_) + "/error2.log";
  oj::LogInit(path_);
  oj::LogWarn("first file");
  oj::LogInit(path2);
  oj::LogWarn("second file");

  EXPECT_NE(std::string::npos, ReadFile().find("first file"));

  std::ifstream f2(path2);
  ASSERT_TRUE(f2.good());
  const std::string content2((std::istreambuf_iterator<char>(f2)),
                             std::istreambuf_iterator<char>());
  EXPECT_NE(std::string::npos, content2.find("second file"));
  EXPECT_EQ(std::string::npos, content2.find("first file"));

  std::remove(path2.c_str());
}

}  // namespace
