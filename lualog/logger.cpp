#define LUA_LIB
#include "logger.h"

#ifdef WIN32
#include <windows.h>
#endif

namespace logger {
    // class log_time
    // --------------------------------------------------------------------------------
    log_time log_time::now() {
        system_clock::duration dur = system_clock::now().time_since_epoch();
        time_t time = duration_cast<seconds>(dur).count();
        auto time_ms = duration_cast<milliseconds>(dur).count();
        return log_time(*std::localtime(&time), time, time_ms % 1000);
    }

    // class log_message
    // --------------------------------------------------------------------------------
    void log_message::option(log_level level, sstring&& msg, cpchar tag, cpchar feature, cpchar source, int32_t line) {
        log_time_ = log_time::now();
        feature_ = feature;
        source_ = source;
        level_ = level;
        line_ = line;
        tag_ = tag;
        msg_ = msg;
    }

    // class log_message_pool
    // --------------------------------------------------------------------------------
    sptr<log_message> log_message_pool::allocate() {
        if (alloc_msgs_->empty()) {
            if (free_msgs_->empty()) {
                free_msgs_->reserve(QUEUE_SIZE);
                alloc_msgs_->reserve(QUEUE_SIZE);
                for (size_t i = 0; i < QUEUE_SIZE; ++i) {
                    alloc_msgs_->push_back(std::make_shared<log_message>());
                }
            } else {
                std::unique_lock<spin_mutex> lock(mutex_);
                alloc_msgs_.swap(free_msgs_);
            }
        }
        if (alloc_msgs_->empty()) {
            return std::make_shared<log_message>();
        }
        auto logmsg = alloc_msgs_->back();
        alloc_msgs_->pop_back();
        return logmsg;
    }

    void log_message_pool::recycle(sptr<log_messages> logmsgs) {
        std::unique_lock<spin_mutex> lock(mutex_);
        size_t fspace = free_msgs_->capacity() - free_msgs_->size();
        size_t n = std::min(fspace, logmsgs->size());
        if (n == 0) return;
        auto siter = logmsgs->begin();
        free_msgs_->insert(free_msgs_->end(), std::make_move_iterator(siter), std::make_move_iterator(siter + n));
    }

    // class log_message_queue
    // --------------------------------------------------------------------------------
    void log_message_queue::put(sptr<log_message> logmsg) {
        std::unique_lock<spin_mutex> lock(mutex_);
        write_msgs_->push_back(std::move(logmsg));
    }

    sptr<log_messages> log_message_queue::timed_getv(bool running) {
        if (running) {
            if (write_msgs_->empty()) return nullptr;
            std::unique_lock<spin_mutex> lock(mutex_, std::try_to_lock);
            if (lock.owns_lock()) {
                read_msgs_.swap(write_msgs_);
                return read_msgs_;
            }
            return nullptr;
        }
        std::unique_lock<spin_mutex> lock(mutex_);
        read_msgs_.swap(write_msgs_);
        return read_msgs_;
    }

    // class log_dest
    // --------------------------------------------------------------------------------
    void log_dest::write(sptr<log_message> logmsg) {
        auto logtxt = fmt::format("{}{}{}\n", build_prefix(logmsg), logmsg->msg(), build_suffix(logmsg));
        raw_write(logtxt, logmsg->level());
    }

    cstring log_dest::build_prefix(sptr<log_message> logmsg) {
        if (!ignore_prefix_) {
            if (last_time_ != logmsg->time()) {
                fmt::format_to(time_buf_, "{:%Y-%m-%d %H:%M:%S}", logmsg->logtime());
            }
            auto names = level_names<log_level>()();
            return fmt::format("[{}.{:03d}][{}][{}] ", time_buf_, logmsg->get_usec(), logmsg->tag(), names[(int)logmsg->level()]);
        }
        return "";
    }

    cstring log_dest::build_suffix(sptr<log_message> logmsg) {
        if (!ignore_suffix_) {
            return fmt::format("[{}:{}]", logmsg->source(), logmsg->line());
        }
        return "";
    }

    // class stdio_dest
    // --------------------------------------------------------------------------------
    void stdio_dest::write(sptr<log_message> logmsg) {
        auto logtxt = fmt::format("{}{}{}", build_prefix(logmsg), logmsg->msg(), build_suffix(logmsg));
        raw_write(logtxt, logmsg->level());
    }

    void stdio_dest::raw_write(sstring& msg, log_level lvl) {
#ifdef WIN32
        auto colors = level_colors<log_level>()();
        std::cout << colors[(int)lvl];
#endif // WIN32
        std::cout << msg << std::endl;
    }

    // class log_file_base
    // --------------------------------------------------------------------------------
    log_file_base::~log_file_base() {
        unmap_file();
    }

    void log_file_base::raw_write(sstring& msg, log_level lvl) {
        size_t msize = msg.size();
        if (size_ + msize > alc_size_) {
            size_t required_pages = (size_ + msize - alc_size_ + PAGE_SIZE - 1) / PAGE_SIZE;
            alc_size_ += required_pages * PAGE_SIZE;
            unmap_file();
            map_file();
        }
        if (buff_) {
            memcpy(buff_ + size_, msg.data(), msize);
            size_ += msize;
        }
    }

    void log_file_base::map_file() {
#ifdef WIN32
        HANDLE hf = CreateFile(file_path_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (!hf) return;
        HANDLE hfm = CreateFileMapping(hf, 0, PAGE_READWRITE, 0, alc_size_, NULL);
        if (!hfm) return;
        buff_ = (char*)MapViewOfFile(hfm, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        CloseHandle(hfm);
        CloseHandle(hf);
#else
        FILE* f = fopen(file_path_.c_str(), "wb+");
        if (!f) return;
        int fd = fileno(f);
        ftruncate(fd, alc_size_);
        buff_ = (char*)mmap(NULL, alc_size_, PROT_WRITE, MAP_SHARED, fileno(f), 0);
        fclose(f);
#endif // WIN32
    }

    void log_file_base::unmap_file() {
#ifdef WIN32
        if (buff_) UnmapViewOfFile(buff_);
#else
        if (buff_) munmap(buff_, alc_size_);
#endif // WIN32
        buff_ = nullptr;
    }

    bool log_file_base::check_full(size_t size) {
        return alc_size_ > max_size_;
    }

    bool log_file_base::create(path file_path, sstring file_name, const std::tm& file_time) {
        unmap_file();
        size_ = 0;
        alc_size_ = PAGE_SIZE;
        file_time_ = file_time;
        file_path.append(file_name);
        file_path_ = file_path.string();
        map_file();
        return buff_ != nullptr;
    }

    // class rolling_hourly
    // --------------------------------------------------------------------------------
    bool rolling_hourly::eval(const log_file_base* log_file, const sptr<log_message> logmsg) const {
        return logmsg->logtime().tm_hour != log_file->file_time().tm_hour;
    }

    // class rolling_daily
    // --------------------------------------------------------------------------------
    bool rolling_daily::eval(const log_file_base* log_file, const sptr<log_message> logmsg) const {
        return logmsg->logtime().tm_mday != log_file->file_time().tm_mday;
    }

    // class log_rollingfile
    // --------------------------------------------------------------------------------
    template<class rolling_evaler>
    log_rollingfile<rolling_evaler>::log_rollingfile(path& log_path, cpchar feature, size_t max_size, size_t clean_time)
        : log_file_base(max_size), log_path_(log_path), feature_(feature), clean_time_(clean_time){
    }

    template<class rolling_evaler>
    void log_rollingfile<rolling_evaler>::write(sptr<log_message> logmsg) {
            auto logtxt = fmt::format("{}{}{}\n", build_prefix(logmsg), logmsg->msg(), build_suffix(logmsg));
            if (buff_ == nullptr || rolling_evaler_.eval(this, logmsg) || check_full(logtxt.size())) {
                create_directories(log_path_);
                try {
                    for (auto entry : recursive_directory_iterator(log_path_)) {
                        if (entry.is_directory() || entry.path().extension().string() != ".log") continue;
                        if (entry.path().stem().has_extension()) {
                            auto ftime = last_write_time(entry.path());
                            if ((size_t)duration_cast<seconds>(file_time_type::clock::now() - ftime).count() > clean_time_) {
                                remove(entry.path());
                            }
                        }
                    }
                } catch (...) {}
                create(log_path_, new_log_file_name(logmsg), logmsg->logtime());
                assert(buff_);
            }
            raw_write(logtxt, logmsg->level());
        }

    template<class rolling_evaler>
    sstring log_rollingfile<rolling_evaler>::new_log_file_name(const sptr<log_message> logmsg) {
        return fmt::format("{}-{:%Y%m%d-%H%M%S}.{:03d}.p{}.log", feature_, logmsg->logtime(), logmsg->get_usec(), ::getpid());
    }

    // class log_service
    // --------------------------------------------------------------------------------


    void log_service::option(cpchar log_path, cpchar service, cpchar index) {
        log_path_ = log_path;
        service_ = fmt::format("{}-{}", service, index);
        create_directories(log_path);
        add_dest(service);
        //启动日志线程
        std::thread(&log_service::run, this).swap(thread_);
    }

    path log_service::build_path(cpchar feature) {
        path log_path = log_path_;
        if (strncmp(service_.c_str(), feature, strlen(feature)) == 0) {
            log_path.append(service_);
        } else {
            log_path.append(feature);
        }
        return log_path;
    }

    bool log_service::add_dest(cpchar feature) {
        std::unique_lock<spin_mutex> lock(mutex_);
        if (dest_features_.find(feature) == dest_features_.end()) {
            sptr<log_dest> logfile = nullptr;
            path logger_path = build_path(feature);
            if (rolling_type_ == rolling_type::DAYLY) {
                logfile = std::make_shared<log_dailyrollingfile>(logger_path, feature, max_size_, clean_time_);
            } else {
                logfile = std::make_shared<log_hourlyrollingfile>(logger_path, feature, max_size_, clean_time_);
            }
            if (!main_dest_) {
                main_dest_ = logfile;
                return true;
            }
            dest_features_.insert(std::make_pair(feature, logfile));
        }
        return true;
    }

    bool log_service::add_lvl_dest(log_level log_lvl) {
        auto names = level_names<log_level>()();
        sstring feature = names[(int)log_lvl];
        std::transform(feature.begin(), feature.end(), feature.begin(), [](auto c) { return std::tolower(c); });
        path logger_path = build_path(service_.c_str());
        logger_path.append(feature);
        std::unique_lock<spin_mutex> lock(mutex_);
        if (rolling_type_ == rolling_type::DAYLY) {
            auto logfile = std::make_shared<log_dailyrollingfile>(logger_path, feature.c_str(), max_size_, clean_time_);
            dest_lvls_.insert(std::make_pair(log_lvl, logfile));
        }
        else {
            auto logfile = std::make_shared<log_hourlyrollingfile>(logger_path, feature.c_str(), max_size_, clean_time_);
            dest_lvls_.insert(std::make_pair(log_lvl, logfile));
        }
        return true;
    }

    bool log_service::add_file_dest(cpchar feature, cpchar fname) {
        std::unique_lock<spin_mutex> lock(mutex_);
        if (dest_features_.find(feature) == dest_features_.end()) {
            auto logfile = std::make_shared<log_file_base>(max_size_);
            path logger_path = build_path(service_.c_str());
            create_directories(logger_path);
            if (!logfile->create(logger_path, fname, log_time::now())) {
                return false;
            }
            logfile->ignore_prefix(true);
            dest_features_.insert(std::make_pair(feature, logfile));
        }
        return true;
    }

    void log_service::del_agent(uint32_t tid) {
        std::unique_lock<spin_mutex> lock(mutex_);
        agents_.erase(tid);
    }

    void log_service::add_agent(sptr<log_agent> agent) {
        std::unique_lock<spin_mutex> lock(mutex_);
        agents_.insert(std::make_pair(agent->get_id(), agent));
    }

    void log_service::del_dest(cpchar feature) {
        std::unique_lock<spin_mutex> lock(mutex_);
        auto it = dest_features_.find(feature);
        if (it != dest_features_.end()) {
            dest_features_.erase(it);
        }
    }

    void log_service::del_lvl_dest(log_level log_lvl) {
        std::unique_lock<spin_mutex> lock(mutex_);
        auto it = dest_lvls_.find(log_lvl);
        if (it != dest_lvls_.end()) {
            dest_lvls_.erase(it);
        }
    }

    void log_service::set_dest_clean_time(cpchar feature, size_t clean_time){
        std::unique_lock<spin_mutex> lock(mutex_);
        auto it = dest_features_.find(feature);
        if (it != dest_features_.end()) {
            it->second->set_clean_time(clean_time);
        }
    }

    void log_service::ignore_prefix(cpchar feature, bool prefix) {
        auto iter = dest_features_.find(feature);
        if (iter != dest_features_.end()) {
            iter->second->ignore_prefix(prefix);
            return;
        }
    }

    void log_service::ignore_suffix(cpchar feature, bool suffix) {
        auto iter = dest_features_.find(feature);
        if (iter != dest_features_.end()) {
            iter->second->ignore_suffix(suffix);
            return;
        }
    }

    log_service::log_service(){
        std_dest_ = std::make_shared<stdio_dest>();
    }

    log_service::~log_service() {
        auto tid = std::this_thread::get_id();
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
            agents_.clear();
            dest_lvls_.clear();
            dest_features_.clear();
            main_dest_ = nullptr;
            std_dest_ = nullptr;
        }
    }

    void log_service::flush() {
        std::unique_lock<spin_mutex> lock(mutex_);
        for (auto dest : dest_features_)
            dest.second->flush();
        for (auto dest : dest_lvls_)
            dest.second->flush();
        if (main_dest_) {
            main_dest_->flush();
        }
    }
   
    void log_service::run() {
        running_ = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        while (true) {
            bool empty = true;
            for (auto [_, agent] : agents_) {
                auto logmsgs = agent->timed_getv(running_);
                if (logmsgs == nullptr) continue;
                for (auto logmsg : *logmsgs) {
                    if (!log_daemon_) {
                        std_dest_->write(logmsg);
                    }
                    main_dest_->write(logmsg);
                    auto it_lvl = dest_lvls_.find(logmsg->level());
                    if (it_lvl != dest_lvls_.end()) {
                        it_lvl->second->write(logmsg);
                    }
                    auto it_fea = dest_features_.find(logmsg->feature());
                    if (it_fea != dest_features_.end()) {
                        it_fea->second->write(logmsg);
                    }
                }
                empty = false;
                agent->recycle(logmsgs);
                logmsgs->clear();
                flush();
            }
            if (!running_ && empty) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    log_agent::log_agent() {
        logmsgque_ = std::make_shared<log_message_queue>();
        message_pool_ = std::make_shared<log_message_pool>();
    }

    log_agent::~log_agent() {
        auto service = service_.lock();
        if (service) {
            service->del_agent(get_id());
        }
    }

    void log_agent::attach(wptr<log_service> service) { 
        service_ = service;
        auto lservice = service_.lock();
        if (lservice) {
            lservice->add_agent(shared_from_this());
        }
    }

    void log_agent::output(log_level level, sstring&& msg, cpchar tag, cpchar feature, cpchar source, int line) {
        if (!is_filter(level)) {
            auto logmsg_ = message_pool_->allocate();
            logmsg_->option(level, std::move(msg), tag, feature, source, line);
            logmsgque_->put(logmsg_);
        }
    }

    uint32_t log_agent::get_id() {
        auto tid = std::this_thread::get_id();
        return *(uint32_t*)&tid;
    }

    void log_agent::filter(log_level llv, bool on) {
        if (on)
            filter_bits_ |= (1 << ((int)llv - 1));
        else
            filter_bits_ &= ~(1 << ((int)llv - 1));
    }
}
