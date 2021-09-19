#include "logger.h"

extern "C"
{
    #include "lua.h"
    #include "lauxlib.h"
    #include "lualib.h"
}

using namespace logger;

#ifdef _MSC_VER
#define LLOG_API extern "C" _declspec(dllexport)
#else
#define LLOG_API extern "C"
#endif

static int llog_init(lua_State* L) {
    size_t max_line = 10000;
    rolling_type roll_type = rolling_type::HOURLY;
    auto service = log_service::default_instance();
    std::string log_path = lua_tostring(L, 1);
    std::string log_name = lua_tostring(L, 2);
    if (lua_gettop(L) > 3) {
        roll_type = (rolling_type)lua_tointeger(L, 3);
        max_line = lua_tointeger(L, 4);
    }
    service->start(log_path, log_name, roll_type, max_line);
    if (lua_gettop(L) > 4) {
        bool is_daemon = lua_toboolean(L, 5);
        service->daemon(is_daemon);
    }
    lua_pushboolean(L, true);
    return 1;
}

static int llog_close(lua_State* L) {
    auto service = log_service::default_instance();
    service->stop();
    return 0;
}

static int llog_filter(lua_State* L) {
    auto log_filter = log_service::default_instance()->get_filter();
    if (log_filter) {
        log_filter->filter((log_level)lua_tointeger(L, 1), lua_toboolean(L, 2));
    }
    return 0;
}

static int llog_daemon(lua_State* L) {
    auto service = log_service::default_instance();
    service->daemon(lua_toboolean(L, 1));
    return 0;
}

static int llog_is_filter(lua_State* L) {
    auto service = log_service::default_instance();
    lua_pushboolean(L, service->is_filter((log_level)lua_tointeger(L, 1)));
    return 1;
}

static int llog_add_dest(lua_State* L) {
    size_t max_line = 10000;
    rolling_type roll_type = rolling_type::HOURLY;
    auto service = log_service::default_instance();
    std::string log_path = lua_tostring(L, 1);
    std::string log_name = lua_tostring(L, 2);
    if (lua_gettop(L) > 3) {
        roll_type = (rolling_type)lua_tointeger(L, 3);
        max_line = lua_tointeger(L, 4);
    }
    bool res = service->add_dest(log_path, log_name, roll_type, max_line);
    lua_pushboolean(L, res);
    return 1;
}

static int llog_del_dest(lua_State* L) {
    auto service = log_service::default_instance();
    std::string log_name = lua_tostring(L, 1);
    service->del_dest(log_name);
    return 0;
}

static int llog_del_lvl_dest(lua_State* L) {
    auto service = log_service::default_instance();
    log_level log_lvl = (log_level)lua_tointeger(L, 1);
    service->del_lvl_dest(log_lvl);
    return 0;
}

static int llog_add_lvl_dest(lua_State* L) {
    size_t max_line = 10000;
    rolling_type roll_type = rolling_type::HOURLY;
    auto service = log_service::default_instance();
    std::string log_path = lua_tostring(L, 1);
    std::string log_name = lua_tostring(L, 2);
    log_level log_lvl = (log_level)lua_tointeger(L, 3);
    if (lua_gettop(L) > 4) {
        roll_type = (rolling_type)lua_tointeger(L, 4);
        max_line = lua_tointeger(L, 5);
    }
    bool res = service->add_level_dest(log_path, log_name, log_lvl, roll_type, max_line);
    lua_pushboolean(L, res);
    return 1;
}

template<log_level level>
static int llog_log(lua_State* L) {
    int line = 0;
    std::string source = "";
    std::string log_msg = lua_tostring(L, 1);
    if (lua_gettop(L) > 2) {
        source = lua_tostring(L, 2);
        line = (int)lua_tointeger(L, 3);
    }
    logger_output<level>(source.c_str(), line) << log_msg;
    auto log_filter = log_service::default_instance()->get_filter();
    lua_pushboolean(L, log_filter ? log_filter->is_filter(level) : true);
    return 1;
}

static const luaL_Reg lualog_funcs[] = {
    { "init", llog_init },
    { "close", llog_close },
    { "filter", llog_filter },
    { "daemon", llog_daemon },
    { "add_dest", llog_add_dest },
    { "del_dest", llog_del_dest },
    { "is_filter", llog_is_filter },
    { "add_lvl_dest", llog_add_lvl_dest },
    { "del_lvl_dest", llog_del_lvl_dest },
    { "info", llog_log<log_level::LOG_LEVEL_INFO> },
    { "warn", llog_log<log_level::LOG_LEVEL_WARN> },
    { "dump", llog_log<log_level::LOG_LEVEL_DUMP> },
    { "debug", llog_log<log_level::LOG_LEVEL_DEBUG> },
    { "error", llog_log<log_level::LOG_LEVEL_ERROR> },
    { "fatal", llog_log<log_level::LOG_LEVEL_FATAL> },
    { NULL, NULL },
};

LLOG_API int luaopen_lualog(lua_State * L) {
    lua_newtable(L);
    luaL_newlib(L, lualog_funcs);
    return 1;
}
