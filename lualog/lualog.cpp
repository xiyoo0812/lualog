#define LUA_LIB
#include "logger.h"
#include "sol/sol.hpp"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

namespace logger {

    sol::table open_lualog(sol::this_state L) {
        sol::state_view lua(L);
        auto lualog = lua.create_table();
        lualog.new_enum("LOG_LEVEL",
            "INFO", log_level::LOG_LEVEL_INFO,
            "WARN", log_level::LOG_LEVEL_WARN,
            "DUMP", log_level::LOG_LEVEL_DUMP,
            "DEBUG", log_level::LOG_LEVEL_DEBUG,
            "ERROR", log_level::LOG_LEVEL_ERROR,
            "FATAL", log_level::LOG_LEVEL_FATAL
        );
        lualog.new_usertype<log_service>("logger",
            "stop", &log_service::stop,
            "start", &log_service::start,
            "daemon", &log_service::daemon,
            "option", &log_service::option,
            "filter", &log_service::filter,
            "add_dest", &log_service::add_dest,
            "del_dest", &log_service::del_dest,
            "is_filter", &log_service::is_filter,
            "add_lvl_dest", &log_service::add_lvl_dest,
            "del_lvl_dest", &log_service::del_lvl_dest,
            "info", &log_service::output<log_level::LOG_LEVEL_INFO>,
            "warn", &log_service::output<log_level::LOG_LEVEL_WARN>,
            "dump", &log_service::output<log_level::LOG_LEVEL_DUMP>,
            "debug", &log_service::output<log_level::LOG_LEVEL_DEBUG>,
            "error", &log_service::output<log_level::LOG_LEVEL_ERROR>,
            "fatal", &log_service::output<log_level::LOG_LEVEL_FATAL>
        );
        return lualog;
    }
}

extern "C" {
    LUALIB_API int luaopen_lualog(lua_State* L) {
        system("echo logger service init.");
        return sol::stack::call_lua(L, 1, logger::open_lualog);
    }
}
