# lualog
c++和lua通用的多线程日志库

# 依赖
- c++17
- [lua](https://github.com/xiyoo0812/lua.git)5.2以上
- sol2
- 项目路径如下<br>
  |--proj <br>
  &emsp;|--lua <br>
  &emsp;|--lualog <br>
  &emsp;|--sol2 <br>

# 编译
- msvc : 准备好lua依赖库并放到指定位置，将proj文件加到sln后编译。
- linux：准备好lua依赖库并放到指定位置，执行make -f lualog.mak

# 功能
- 支持C++和lua使用
- 多线程日志输出
- 日志定时滚动输出
- 日志最大行数滚动输出
- 日志分级、分文件输出

# lua使用方法
```lua
local llog = require("lualog")

local logger = llog.logger.new()

logger:start()
logger:debug("aaaaaaaaaa")
logger:info("bbbb")
logger:warn("cccccc")
logger:dump("dddddddddd")
logger:error("eeeeeeeeeeee")

logger:stop()

```

# C++使用方法
```c++
#include "logger.h"

auto logger = std::make_shared<log_service>();
logger->start();
logger->add_dest(logpath, logname, rolltype, maxline);

LOG_DEBUG(logger) << "aaaaaaaaaa";
LOG_WARN(logger) << "bbbb";
LOG_INFO(logger) << "cccccc";
LOG_ERROR(logger) << "dddddddddd";
LOG_FETAL(logger) << "eeeeeeeeeeee";

logger->stop()

```
