# lualog
c++和lua通用的多线程日志库，需要C++17支持

# 依赖
- c++17
- [lua](https://github.com/xiyoo0812/lua.git)5.2以上
- 项目路径如下<br>
  |--proj <br>
  &emsp;|--lua <br>
  &emsp;|--lualog

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

llog.init("./path/", "test_log", 0, 500000)

llog.debug("aaaaaaaaaa")
llog.info("bbbb")
llog.warn("cccccc")
llog.dump("dddddddddd")
llog.error("eeeeeeeeeeee")
```

# C++使用方法
```c++
#include "logger.h"

LOG_START("./path/", "test_log", 0, 500000);
LOG_DEBUG << "aaaaaaaaaa";
LOG_WARN << "bbbb";
LOG_INFO << "cccccc";
LOG_ERROR << "dddddddddd";
LOG_FETAL << "eeeeeeeeeeee";

LOG_STOP();

```
