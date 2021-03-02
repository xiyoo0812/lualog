# lualog
c++和lua通用的日志库，需要C++17支持

# 编译
- msvc : 准备好lua依赖库并放到指定位置，将proj文件加到sln后编译。
- linux：准备好lua依赖库并放到指定位置，执行make -f lualog.mak

# 依赖
- lua5.3
- c++17

# lua使用方法
```lua
local llog = require("lualog")

llog.init("./newlog/", "qtest", 0, 500000)

llog.debug("aaaaaaaaaa")
llog.info("bbbb")
llog.warn("cccccc")
llog.dump("dddddddddd")
llog.error("eeeeeeeeeeee")
```

# C++使用方法
参考lualog.cpp封装给lua的接口