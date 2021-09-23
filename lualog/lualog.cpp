#include "logger.h"
#include "sol/sol.hpp"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

using namespace logger;


static void llog_init(std::string log_path, std::string log_name, rolling_type roll_type = rolling_type::HOURLY, size_t max_line = 100000) {
    log_service::default_instance()->start(log_path, log_name, roll_type, max_line);
}

static void llog_close() {
    log_service::default_instance()->stop();
}

static void llog_daemon(bool daemon) {
    log_service::default_instance()->daemon(daemon);
}

static void llog_is_filter(log_level lvl) {
    log_service::default_instance()->is_filter(lvl);
}

static void llog_filter(log_level lvl, bool on) {
    log_service::default_instance()->get_filter()->filter(lvl, on);
}

static void llog_add_dest(std::string name, std::string log_name, rolling_type roll_type = rolling_type::HOURLY, size_t max_line = 100000) {
    log_service::default_instance()->add_dest(name, log_name, roll_type, max_line);
}

static void llog_del_dest(std::string name) {
    log_service::default_instance()->del_dest(name);
}

static void llog_add_lvl_dest(std::string name, std::string log_name, log_level lvl, rolling_type roll_type = rolling_type::HOURLY, size_t max_line = 100000) {
    log_service::default_instance()->add_level_dest(name, log_name, lvl, roll_type, max_line);
}

static void llog_del_lvl_dest(log_level lvl) { 
    log_service::default_instance()->del_lvl_dest(lvl);
}

template<log_level level>
static void llog_log(std::string log, std::string source = "", int line = 0) {
    logger_output<level>(source, line) << log;
}

#ifdef _MSC_VER
#define LLOG_API extern "C" _declspec(dllexport)
#else
#define LLOG_API extern "C"
#endif

LLOG_API int luaopen_lualog(lua_State* L) {
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
    lualog.set_function("init", llog_init);
    lualog.set_function("close", llog_close);
    lualog.set_function("daemon", llog_daemon);
    lualog.set_function("filter", llog_filter);
    lualog.set_function("is_filter", llog_is_filter);
    lualog.set_function("add_dest", llog_add_dest);
    lualog.set_function("del_dest", llog_del_dest);
    lualog.set_function("add_lvl_dest", llog_add_lvl_dest);
    lualog.set_function("del_lvl_dest", llog_del_lvl_dest);
    lualog.set_function("info", llog_log<log_level::LOG_LEVEL_INFO>);
    lualog.set_function("warn", llog_log<log_level::LOG_LEVEL_WARN>);
    lualog.set_function("dump", llog_log<log_level::LOG_LEVEL_DUMP>);
    lualog.set_function("debug", llog_log<log_level::LOG_LEVEL_DEBUG>);
    lualog.set_function("error", llog_log<log_level::LOG_LEVEL_ERROR>);
    lualog.set_function("fatal", llog_log<log_level::LOG_LEVEL_FATAL>);
    sol::reference lualog_ref = lualog;
    lualog_ref.push();
    return 1;
}
