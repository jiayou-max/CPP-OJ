#pragma once

#include "httplib.h"

namespace oj {

// 注册所有 HTTP 路由（独立于 main，便于单元测试直接构造 Server 并验证）
void RegisterRoutes(httplib::Server& svr);

}  // namespace oj
