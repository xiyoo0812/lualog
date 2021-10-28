#pragma once

#include <list>
#include <array>
#include <ctime>
#include <mutex>
#include <vector>
#include <chrono>
#include <thread>
#include <sstream>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <condition_variable>
#include <assert.h>

#include "fmt/core.h"

#ifdef WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

using namespace std::chrono;

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
            return { "\x1b[32m","\x1b[37m", "\x1b[32m", "\x1b[33m", "\x1b[32m", "\x1b[31m", "\x1b[31m" };
        }
    };

    class log_filter {
    public:
        void filter(log_level llv, bool on) {
            if (on)
                switch_bits_ |= (1 << ((int)llv - 1));
            else
                switch_bits_ &= ~(1 << ((int)llv - 1));
        }
        bool is_filter(log_level llv) const {
            return 0 == (switch_bits_ & (1 << ((int)llv - 1)));
        }
    private:
        unsigned switch_bits_ = -1;
    }; // class log_filter

    class log_time : public ::tm {
    public:
        int tm_usec = 0;

        log_time() { }
        log_time(const ::tm& tm, int usec) : ::tm(tm), tm_usec(usec) { }
        static log_time now() {
            system_clock::duration dur = system_clock::now().time_since_epoch();
            auto time_t = duration_cast<seconds>(dur).count();
            auto time_ms = duration_cast<milliseconds>(dur).count();
            return log_time(*std::localtime(&time_t), time_ms % 1000);
        }
    }; // class log_time

    template<log_level> class log_stream;
    class log_message {
    public:
        template<log_level>
        friend class log_stream;
        int line() const { return line_; }
        bool is_grow() const { return grow_; }
        void set_grow(bool grow) { grow_ = grow; }
        log_level level() const { return level_; }
        const std::string msg() const { return stream_.str(); }
        const std::string source() const { return source_; }
        const log_time& get_log_time()const { return log_time_; }
        void clear() {
            stream_.clear();
            stream_.str("");
        }
        template<class T>
        log_message& operator<<(const T& value) {
            stream_ << value;
            return *this;
        }
    private:
        int                 line_ = 0;
        bool                grow_ = false;
        log_time            log_time_;
        std::string         source_;
        std::stringstream   stream_;
        log_level           level_ = log_level::LOG_LEVEL_DEBUG;
    }; // class log_message

    class log_message_pool {
    public:
        log_message_pool(size_t msg_size) {
            for (size_t i = 0; i < msg_size; ++i) {
                messages_.push_back(std::make_shared<log_message>());
            }
        }
        ~log_message_pool() {
            messages_.clear();
        }
        std::shared_ptr<log_message> allocate() {
            if (messages_.empty()) {
                auto logmsg = std::make_shared<log_message>();
                logmsg->set_grow(true);
                return logmsg;
            }
            std::unique_lock<std::mutex>lock(mutex_);
            auto logmsg = messages_.front();
            messages_.pop_front();
            logmsg->clear();
            return logmsg;
        }
        void release(std::shared_ptr<log_message> logmsg) {
            if (!logmsg->is_grow()) {
                std::unique_lock<std::mutex>lock(mutex_);
                messages_.push_back(logmsg);
            }
        }

    private:
        std::mutex                  mutex_;
        std::list<std::shared_ptr<log_message>>    messages_;
    }; // class log_message_pool

    class log_message_queue {
    public:
        void put(std::shared_ptr<log_message> logmsg) {
            std::unique_lock<std::mutex> lock(mutex_);
            messages_.push_back(logmsg);
            condv_.notify_all();
        }

        void timed_getv(std::vector<std::shared_ptr<log_message>>& vec_msg, size_t number, int time) {
            std::unique_lock<std::mutex>lock(mutex_);
            if (messages_.empty()) {
                condv_.wait_for(lock, std::chrono::milliseconds(time));
            }
            while (!messages_.empty() && number > 0) {
                auto logmsg = messages_.front();
                vec_msg.push_back(logmsg);
                messages_.pop_front();
                number--;
            }
        }

    private:
        std::mutex                  mutex_;
        std::condition_variable     condv_;
        std::list<std::shared_ptr<log_message>> messages_;
    }; // class log_message_queue

    class log_service;
    class log_dest {
    public:
        log_dest(std::shared_ptr<log_service>service) : log_service_(service) {}
        virtual ~log_dest() { }

        virtual void flush() {};
        virtual void raw_write(std::string msg, log_level lvl) = 0;
        virtual void write(std::shared_ptr<log_message> logmsg);
        virtual std::string build_prefix(std::shared_ptr<log_message> logmsg);
        virtual std::string build_postfix(std::shared_ptr<log_message> logmsg);

    protected:
        std::shared_ptr<log_service>    log_service_ = nullptr;
    }; // class log_dest

    class stdio_dest : public log_dest {
    public:
        stdio_dest(std::shared_ptr<log_service> service) : log_dest(service) {}
        virtual ~stdio_dest() { }

        virtual void raw_write(std::string msg, log_level lvl) {
#ifdef WIN32
            auto colors = level_colors<log_level>()();
            std::cout << colors[(int)lvl];
#endif // WIN32
            std::cout << msg;
        }
    }; // class stdio_dest

    class log_file_base : public log_dest {
    public:
        log_file_base(std::shared_ptr<log_service> service, size_t max_line, int pid)
            : log_dest(service), pid_(pid), line_(0), max_line_(max_line){ }

        virtual ~log_file_base() {
            if (file_) {
                file_->flush();
                file_->close();
            }
        }
        virtual void raw_write(std::string msg, log_level lvl) {
            if (file_) file_->write(msg.c_str(), msg.size());
        }
        virtual void flush() {
            if (file_) file_->flush();
        }
        const log_time& file_time() const { return file_time_; }

    protected:
        virtual void create(const std::string& file_path, const log_time& file_time, const char* mode) {
            if (file_) {
                file_->flush();
                file_->close();
            }
            file_name_ = file_path;
            file_time_ = file_time;
            file_ = std::make_unique<std::ofstream>(file_path, std::ios::binary | std::ios::out | std::ios::app);
        }

        int             pid_;
        size_t          line_;
        size_t          max_line_;
        log_time        file_time_;
        std::string     file_name_;
        std::unique_ptr<std::ofstream> file_ = nullptr;
    }; // class log_file

    class rolling_hourly {
    public:
        bool eval(const log_file_base* log_file, const std::shared_ptr<log_message> logmsg) const {
            const log_time& ftime = log_file->file_time();
            const log_time& ltime = logmsg->get_log_time();
            return ltime.tm_year != ftime.tm_year || ltime.tm_mon != ftime.tm_mon ||
                ltime.tm_mday != ftime.tm_mday || ltime.tm_hour != ftime.tm_hour;
        }

    }; // class rolling_hourly

    class rolling_daily {
    public:
        bool eval(const log_file_base* log_file, const std::shared_ptr<log_message> logmsg) const {
            const log_time& ftime = log_file->file_time();
            const log_time& ltime = logmsg->get_log_time();
            return ltime.tm_year != ftime.tm_year || ltime.tm_mon != ftime.tm_mon || ltime.tm_mday != ftime.tm_mday;
        }
    }; // class rolling_daily

    template<class rolling_evaler>
    class log_rollingfile : public log_file_base {
    public:
        log_rollingfile(std::shared_ptr<log_service> service, int pid, const std::string& log_path, const std::string& log_name, size_t max_line = 10000)
            : log_file_base(service, max_line, pid), log_name_(log_name), log_path_(log_path) {
            std::filesystem::create_directories(log_path_);
        }

        virtual void write(std::shared_ptr<log_message> logmsg) {
            line_++;
            if (rolling_evaler_.eval(this, logmsg) || line_ >= max_line_) {
                std::string file_path = new_log_file_path(logmsg);
                create(file_path, logmsg->get_log_time(), "a+");
                assert(file_);
                line_ = 0;
            }
            log_file_base::write(logmsg);
        }

    protected:
        std::string new_log_file_path(const std::shared_ptr<log_message> logmsg) {
            const log_time& t = logmsg->get_log_time();
            return fmt::format("{}{}-{:4d}{:02d}{:02d}-{:02d}{:02d}{:02d}.{:03d}.p{}.log", log_path_.string(), log_name_,
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, t.tm_usec, pid_);
        }

        std::string             log_name_;
        std::filesystem::path   log_path_;
        rolling_evaler          rolling_evaler_;
    }; // class log_rollingfile

    typedef log_rollingfile<rolling_hourly> log_hourlyrollingfile;
    typedef log_rollingfile<rolling_daily> log_dailyrollingfile;

    template<log_level level>
    class log_stream {
    public:
        log_stream(std::shared_ptr<log_service> service, const std::string source = "", int line = 0)
            : service_(service) {
            if (!service->is_filter(level)) {
                logmsg_ = service->message_pool()->allocate();
                logmsg_->log_time_ = log_time::now();
                logmsg_->level_ = level;
                logmsg_->source_ = source;
                logmsg_->line_ = line;
            }
        }
        ~log_stream()         {
            if (nullptr != logmsg_) {
                service_->submit(logmsg_);
            }
        }

        template<class T>
        log_stream& operator<<(const T& value) {
            if (nullptr != logmsg_) {
                *logmsg_ << value;
            }
            return *this;
        }

    private:
        std::shared_ptr<log_message> logmsg_ = nullptr;
        std::shared_ptr<log_service> service_ = nullptr;
    };

    class log_service : public std::enable_shared_from_this<log_service> {
    public:
        void daemon(bool status) { log_daemon_ = status; }
        std::shared_ptr<log_filter> get_filter() { return log_filter_; }
        std::shared_ptr<log_message_pool> message_pool() { return message_pool_; }

        bool add_dest(std::string log_path, std::string log_name, rolling_type roll_type, size_t max_line) {
            std::unique_lock<std::mutex> lock(mutex_);
            if (dest_names_.find(log_name) == dest_names_.end()) {
                if (roll_type == rolling_type::DAYLY) {
                    auto logfile = std::make_shared<log_hourlyrollingfile>(shared_from_this(), log_pid_, log_path, log_name, max_line);
                    dest_names_.insert(std::make_pair(log_name, logfile));
                }
                else {
                    auto logfile = std::make_shared<log_hourlyrollingfile>(shared_from_this(), log_pid_, log_path, log_name, max_line);
                    dest_names_.insert(std::make_pair(log_name, logfile));
                }
                return true;
            }
            return false;
        }

        bool add_lvl_dest(std::string log_path, std::string log_name, log_level log_lvl, rolling_type roll_type, size_t max_line) {
            std::unique_lock<std::mutex> lock(mutex_);
            if (roll_type == rolling_type::DAYLY) {
                auto logfile = std::make_shared<log_hourlyrollingfile>(shared_from_this(), log_pid_, log_path, log_name, max_line);
                dest_lvls_.insert(std::make_pair(log_lvl, logfile));
            }
            else {
                auto logfile = std::make_shared<log_hourlyrollingfile>(shared_from_this(), log_pid_, log_path, log_name, max_line);
                dest_lvls_.insert(std::make_pair(log_lvl, logfile));
            }
            return true;
        }

        void del_dest(std::string log_name) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto it = dest_names_.find(log_name);
            if (it != dest_names_.end()) {
                dest_names_.erase(it);
            }
        }

        void del_lvl_dest(log_level log_lvl) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto it = dest_lvls_.find(log_lvl);
            if (it != dest_lvls_.end()) {
                dest_lvls_.erase(it);
            }
        }

        void start() {
            if (stop_msg_ == nullptr) {
                log_pid_ = ::getpid();
                stop_msg_ = message_pool_->allocate();
                log_filter_ = std::make_shared<log_filter>();
                std_dest_ = std::make_shared<stdio_dest>(shared_from_this());
                std::thread(&log_service::run, this).swap(thread_);
            }
        }

        void stop() {
            if (thread_.joinable()) {
                logmsgque_->put(stop_msg_);
                thread_.join();
            }
        }

        void submit(std::shared_ptr<log_message> logmsg) {
            logmsgque_->put(logmsg);
        }

        void flush() {
            std::unique_lock<std::mutex> lock(mutex_);
            for (auto dest : dest_names_)
                dest.second->flush();
            for (auto dest : dest_lvls_)
                dest.second->flush();
        }

        bool is_ignore_prefix() const { return ignore_prefix_; }
        bool is_ignore_postfix() const { return ignore_postfix_; }
        void ignore_prefix() { ignore_prefix_ = true; }
        void ignore_postfix() { ignore_postfix_ = true; }

        bool is_filter(log_level lv) {
            return log_filter_->is_filter(lv); 
        }

        void filter(log_level lv, bool on) {
            log_filter_->filter(lv, on);
        }

        template<log_level level>
        log_stream<level> hold(std::string source = "", int line = 0) {
            return log_stream<level>(shared_from_this(), source, line);
        }

        template<log_level level>
        void output(std::string msg) {
            hold<level>(__FILE__, __LINE__) << msg;
        }

    private:
        void run() {
            bool loop = true;
            while (loop) {
                std::vector<std::shared_ptr<log_message>> logmsgs;
                logmsgque_->timed_getv(logmsgs, log_getv_, log_period_);
                for (auto logmsg : logmsgs) {
                    if (logmsg == stop_msg_) {
                        loop = false;
                        continue;
                    }
                    if (!log_daemon_) {
                        std_dest_->write(logmsg);
                    }
                    auto iter = dest_lvls_.find(logmsg->level());
                    if (iter != dest_lvls_.end()) {
                        iter->second->write(logmsg);
                    }
                    else {
                        for (auto dest : dest_names_) {
                            dest.second->write(logmsg);
                        }
                    }
                    message_pool_->release(logmsg);
                }
                flush();
            }
        }

        int             log_pid_ = 0;
        int             log_getv_ = 50;
        int             log_period_ = 10;
        bool            log_daemon_ = false;
        bool            ignore_prefix_ = false;
        bool            ignore_postfix_ = true;
        std::mutex                      mutex_;
        std::thread                     thread_;
        std::shared_ptr<log_dest>       std_dest_ = nullptr;
        std::shared_ptr<log_message>    stop_msg_ = nullptr;
        std::shared_ptr<log_filter>     log_filter_ = nullptr;
        std::unordered_map<log_level, std::shared_ptr<log_dest>> dest_lvls_;
        std::unordered_map<std::string, std::shared_ptr<log_dest>> dest_names_;
        std::shared_ptr<log_message_queue>  logmsgque_ = std::make_shared<log_message_queue>();
        std::shared_ptr<log_message_pool>   message_pool_ = std::make_shared<log_message_pool>(3000);
    }; // class log_service

    inline void log_dest::write(std::shared_ptr<log_message> logmsg) {
        auto logtxt = fmt::format("{}{}{}\n", build_prefix(logmsg), logmsg->msg(), build_postfix(logmsg));
        raw_write(logtxt, logmsg->level());
    }

    inline std::string log_dest::build_prefix(std::shared_ptr<log_message> logmsg) {
        if (!log_service_->is_ignore_prefix()) {
            auto names = level_names<log_level>()();
            const log_time& t = logmsg->get_log_time();
            return fmt::format("{:4d}{:02d}{:02d} {:02d}:{:02d}:{:02d}.{:03d}/{} ", t.tm_year + 1900,
                t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
                    t.tm_usec, names[(int)logmsg->level()]);
        }
        return "";
    }

    inline std::string log_dest::build_postfix(std::shared_ptr<log_message> logmsg) {
        if (!log_service_->is_ignore_postfix()) {
            return fmt::format("[{}:{}]", logmsg->source().c_str(), logmsg->line());
        }
        return "";
    }
}

#define LOG_WARN(service) service->hold<logger::log_level::LOG_LEVEL_WARN>(__FILE__, __LINE__)
#define LOG_INFO(service) service->hold<logger::log_level::LOG_LEVEL_INFO>(__FILE__, __LINE__)
#define LOG_DUMP(service) service->hold<logger::log_level::LOG_LEVEL_DUMP>(__FILE__, __LINE__)
#define LOG_DEBUG(service) service->hold<logger::log_level::LOG_LEVEL_DEBUG>(__FILE__, __LINE__)
#define LOG_ERROR(service) service->hold<logger::log_level::LOG_LEVEL_ERROR>(__FILE__, __LINE__)
#define LOG_FATAL(service) service->hold<logger::log_level::LOG_LEVEL_FATAL>(__FILE__, __LINE__)
