#pragma once

#include <array>
#include <ctime>
#include <vector>
#include <chrono>
#include <thread>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <assert.h>

#include "fmt/chrono.h"
#include "lua_kit.h"

#ifdef WIN32
#define getpid _getpid
#else
#include <unistd.h>
#include <sys/mman.h>
#endif

using namespace luakit;
using namespace std::chrono;
using namespace std::filesystem;

using cpchar    = const char*;
using sstring   = std::string;
using vstring   = std::string_view;
using cstring   = const std::string;

template <class T>
using sptr      = std::shared_ptr<T>;

namespace logger {
    enum class log_level {
        LOG_LEVEL_DEBUG = 1,
        LOG_LEVEL_INFO,
        LOG_LEVEL_WARN,
        LOG_LEVEL_DUMP,
        LOG_LEVEL_ERROR,
        LOG_LEVEL_FATAL,
    };

    enum class rolling_type {
        HOURLY = 0,
        DAYLY = 1,
    }; //rolling_type

    const size_t QUEUE_SIZE = 10000;
    const size_t PAGE_SIZE  = 65536;
    const size_t MAX_SIZE   = 1024 * 1024 * 16;
    const size_t CLEAN_TIME = 7 * 24 * 3600;

    template <typename T>
    struct level_names {};
    template <> struct level_names<log_level> {
        constexpr std::array<const char*, 7> operator()() const {
            return { "UNKNW", "DEBUG", "INFO", "WARN", "DUMP", "ERROR","FATAL" };
        }
    };

    template <typename T>
    struct level_colors {};
    template <> struct level_colors<log_level> {
        constexpr std::array<const char*, 7> operator()() const {
            return { "\x1b[32m", "\x1b[37m", "\x1b[32m", "\x1b[33m", "\x1b[33m", "\x1b[31m", "\x1b[32m" };
        }
    };

    class log_time : public std::tm {
    public:
        static log_time now();
        log_time(const std::tm& tm, int usec) : std::tm(tm), tm_usec(usec) { }
        log_time() { }

    public:
        int tm_usec = 0;
    }; // class log_time

    class log_message {
    public:
        vstring tag() const { return tag_; }
        vstring msg() const { return msg_; }
        int32_t line() const { return line_; }
        vstring source() const { return source_; }
        vstring feature() const { return feature_; }
        bool is_grow() const { return grow_; }
        void set_grow(bool grow) { grow_ = grow; }
        int get_usec() { return log_time_.tm_usec; }
        log_level level() const { return level_; }
        const std::tm& get_time() const { return log_time_; }
        void option(log_level level, sstring&& msg, cpchar tag, cpchar feature, cpchar source, int32_t line);

    private:
        bool                grow_ = false;
        int32_t             line_ = 0;
        log_time            log_time_;
        sstring             source_, msg_, feature_, tag_;
        log_level           level_ = log_level::LOG_LEVEL_DEBUG;
    }; // class log_message
    typedef std::list<sptr<log_message>> log_message_list;

    class log_message_pool {
    public:
        log_message_pool(size_t msg_size);
        ~log_message_pool();

        sptr<log_message> allocate();
        void release(sptr<log_message> logmsg);

    private:
        spin_mutex mutex_;
        sptr<log_message_list> free_messages_ = std::make_shared<log_message_list>();
        sptr<log_message_list> alloc_messages_ = std::make_shared<log_message_list>();
    }; // class log_message_pool

    class log_message_queue {
    public:
        void put(sptr<log_message> logmsg);
        sptr<log_message_list> timed_getv();

    private:
        spin_mutex                  spin_;
        sptr<log_message_list> read_messages_ = std::make_shared<log_message_list>();
        sptr<log_message_list> write_messages_ = std::make_shared<log_message_list>();
    }; // class log_message_queue

    class log_dest {
    public:
        virtual void flush() {};
        virtual void write(sptr<log_message> logmsg);
        virtual void set_clean_time(size_t clean_time) {}
        virtual void raw_write(sstring& msg, log_level lvl) = 0;
        virtual void ignore_prefix(bool prefix) { ignore_prefix_ = prefix; }
        virtual void ignore_suffix(bool suffix) { ignore_suffix_ = suffix; }
        virtual cstring build_prefix(sptr<log_message> logmsg);
        virtual cstring build_suffix(sptr<log_message> logmsg);

    protected:
        bool ignore_suffix_ = true;
        bool ignore_prefix_ = false;
    }; // class log_dest

    class stdio_dest : public log_dest {
    public:
        virtual void write(sptr<log_message> logmsg);
        virtual void raw_write(sstring& msg, log_level lvl);
    }; // class stdio_dest

    class log_file_base : public log_dest {
    public:
        log_file_base(size_t max_size) : size_(0), alc_size_(USHRT_MAX), max_size_(max_size > USHRT_MAX ? max_size : USHRT_MAX) {}
        virtual ~log_file_base();

        const std::tm& file_time() const { return file_time_; }
        virtual void raw_write(sstring& msg, log_level lvl);
        bool create(path file_path, sstring file_name, const std::tm& file_time);

    protected:
        void map_file();
        void unmap_file();
        bool check_full(size_t size);

    protected:
        std::tm         file_time_;
        size_t          size_, alc_size_, max_size_;
        char*           buff_ = nullptr;
        sstring         file_path_;
    }; // class log_file

    class rolling_hourly {
    public:
        bool eval(const log_file_base* log_file, const sptr<log_message> logmsg) const;
    }; // class rolling_hourly

    class rolling_daily {
    public:
        bool eval(const log_file_base* log_file, const sptr<log_message> logmsg) const;
    }; // class rolling_daily

    template<class rolling_evaler>
    class log_rollingfile : public log_file_base {
    public:
        log_rollingfile(path& log_path, cpchar namefix, size_t max_szie = MAX_SIZE, size_t clean_time = CLEAN_TIME);

        virtual void write(sptr<log_message> logmsg);
        virtual void set_clean_time(size_t clean_time) { clean_time_ = clean_time; }

    protected:
        sstring new_log_file_name(const sptr<log_message> logmsg);

        path                    log_path_;
        sstring                 feature_;
        rolling_evaler          rolling_evaler_;
        size_t                  clean_time_ = CLEAN_TIME;
    }; // class log_rollingfile

    typedef log_rollingfile<rolling_hourly> log_hourlyrollingfile;
    typedef log_rollingfile<rolling_daily> log_dailyrollingfile;

    class log_service {
    public:
        void stop();
        void start();

        void daemon(bool status) { log_daemon_ = status; }
        void option(cpchar log_path, cpchar service, cpchar index);

        bool add_dest(cpchar feature);
        bool add_lvl_dest(log_level log_lvl);
        bool add_file_dest(cpchar feature, cpchar fname);

        void del_dest(cpchar feature);
        void del_lvl_dest(log_level log_lvl);

        void ignore_prefix(cpchar feature, bool prefix);
        void ignore_suffix(cpchar feature, bool suffix);

        void set_max_size(size_t max_size) { max_size_ = max_size; }
        void set_rolling_type(rolling_type type) { rolling_type_ = type; }
        void set_clean_time(size_t clean_time) { clean_time_ = clean_time; }
        void set_dest_clean_time(cpchar feature, size_t clean_time);

        void filter(log_level lv, bool on);
        bool is_filter(log_level lv) { return 0 == (filter_bits_ & (1 << ((int)lv - 1))); }

        void output(log_level level, sstring&& msg, cpchar tag, cpchar feature, cpchar source = "", int line = 0);

    protected:
        path build_path(cpchar feature);
        void flush();
        void run();

        path            log_path_;
        spin_mutex      mutex_;
        std::thread     thread_;
        sstring         service_;
        sptr<log_dest>  std_dest_ = nullptr;
        sptr<log_dest>  main_dest_ = nullptr;
        sptr<log_message> stop_msg_ = nullptr;
        sptr<log_message_queue> logmsgque_ = nullptr;
        sptr<log_message_pool> message_pool_ = nullptr;
        std::map<log_level, sptr<log_dest>> dest_lvls_;
        std::map<sstring, sptr<log_dest>, std::less<>> dest_features_;
        size_t max_size_ = MAX_SIZE, clean_time_ = CLEAN_TIME;
        rolling_type rolling_type_ = rolling_type::DAYLY;
        int32_t filter_bits_ = -1;
        bool log_daemon_ = false;
    }; // class log_service

    extern "C" {
        LUALIB_API void init_logger();
        LUALIB_API void stop_logger();
        LUALIB_API void option_logger(cpchar log_path, cpchar service, cpchar index);
        LUALIB_API void output_logger(log_level level, sstring&& msg, cpchar tag, cpchar feature, cpchar source, int line);
    }
}

#define LOG_WARN(msg) output_logger(logger::log_level::LOG_LEVEL_WARN, msg, "", "", __FILE__, __LINE__)
#define LOG_INFO(msg) output_logger(logger::log_level::LOG_LEVEL_INFO, msg, "", "", __FILE__, __LINE__)
#define LOG_DUMP(msg) output_logger(logger::log_level::LOG_LEVEL_DUMP, msg, "", "", __FILE__, __LINE__)
#define LOG_DEBUG(msg) output_logger(logger::log_level::LOG_LEVEL_DEBUG, msg, "", "", __FILE__, __LINE__)
#define LOG_ERROR(msg) output_logger(logger::log_level::LOG_LEVEL_ERROR, msg, "", "", __FILE__, __LINE__)
#define LOG_FATAL(msg) output_logger(logger::log_level::LOG_LEVEL_FATAL, msg, "", "", __FILE__, __LINE__)
#define LOGF_WARN(msg, feature) output_logger(logger::log_level::LOG_LEVEL_WARN, msg, "", feature, __FILE__, __LINE__)
#define LOGF_INFO(msg, feature) output_logger(logger::log_level::LOG_LEVEL_INFO, msg, "", feature, __FILE__, __LINE__)
#define LOGF_DUMP(msg, feature) output_logger(logger::log_level::LOG_LEVEL_DUMP, msg, "", feature, __FILE__, __LINE__)
#define LOGF_DEBUG(msg, feature) output_logger(logger::log_level::LOG_LEVEL_DEBUG, msg, "", feature, __FILE__, __LINE__)
#define LOGF_ERROR(msg, feature) output_logger(logger::log_level::LOG_LEVEL_ERROR, msg, "", feature, __FILE__, __LINE__)
#define LOGF_FATAL(msg, feature) output_logger(logger::log_level::LOG_LEVEL_FATAL, msg, "", feature, __FILE__, __LINE__)
#define LOGTF_WARN(msg, tag, feature) output_logger(logger::log_level::LOG_LEVEL_WARN, msg, tag, feature, __FILE__, __LINE__)
#define LOGTF_INFO(msg, tag, feature) output_logger(logger::log_level::LOG_LEVEL_INFO, msg, tag, feature, __FILE__, __LINE__)
#define LOGTF_DUMP(msg, tag, feature) output_logger(logger::log_level::LOG_LEVEL_DUMP, msg, tag, feature, __FILE__, __LINE__)
#define LOGTF_DEBUG(msg, tag, feature) output_logger(logger::log_level::LOG_LEVEL_DEBUG, msg, tag, feature, __FILE__, __LINE__)
#define LOGTF_ERROR(msg, tag, feature) output_logger(logger::log_level::LOG_LEVEL_ERROR, msg, tag, feature, __FILE__, __LINE__)
#define LOGTF_FATAL(msg, tag, feature) output_logger(logger::log_level::LOG_LEVEL_FATAL, msg, tag, feature, __FILE__, __LINE__)
