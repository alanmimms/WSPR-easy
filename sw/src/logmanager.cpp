#include "logmanager.hpp"
#include <cstdio>

LOG_MODULE_REGISTER(wspr_log, LOG_LEVEL_INF);

namespace wspr {

// --- Logger Implementation ---

Logger::Logger(const std::string& name, LogManager& mgr) 
    : name(name), manager(mgr) {}

bool Logger::isEnabled(const char* type) const {
    return manager.isEnabled(name, type);
}

void Logger::inf(const char* type, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(LOG_LEVEL_INF, type, fmt, args);
    va_end(args);
}

void Logger::dbg(const char* type, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(LOG_LEVEL_DBG, type, fmt, args);
    va_end(args);
}

void Logger::wrn(const char* type, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(LOG_LEVEL_WRN, type, fmt, args);
    va_end(args);
}

void Logger::err(const char* type, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(LOG_LEVEL_ERR, type, fmt, args);
    va_end(args);
}

void Logger::vlog(int level, const char* type, const char* fmt, va_list args) {
    if (!manager.isEnabled(name, type)) return;

    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);

    const char* subName = name.c_str();
    
    if (type) {
        switch (level) {
            case LOG_LEVEL_ERR: LOG_ERR("[%s:%s] %s", subName, type, buf); break;
            case LOG_LEVEL_WRN: LOG_WRN("[%s:%s] %s", subName, type, buf); break;
            case LOG_LEVEL_INF: LOG_INF("[%s:%s] %s", subName, type, buf); break;
            case LOG_LEVEL_DBG: LOG_DBG("[%s:%s] %s", subName, type, buf); break;
        }
    } else {
        switch (level) {
            case LOG_LEVEL_ERR: LOG_ERR("[%s] %s", subName, buf); break;
            case LOG_LEVEL_WRN: LOG_WRN("[%s] %s", subName, buf); break;
            case LOG_LEVEL_INF: LOG_INF("[%s] %s", subName, buf); break;
            case LOG_LEVEL_DBG: LOG_DBG("[%s] %s", subName, buf); break;
        }
    }
}

// --- LogManager Implementation ---

LogManager& LogManager::instance() {
    static LogManager inst;
    return inst;
}

Logger& LogManager::registerSubsystem(const std::string& name, 
                                     std::vector<std::string> subtypes) {
    if (loggers.find(name) == loggers.end()) {
        loggers[name] = std::make_unique<Logger>(name, *this);
        
        SubsystemState state;
        state.masterEnabled = true;
        for (const auto& st : subtypes) {
            state.subtypes[st] = true;
        }
        states[name] = state;
    }
    return *loggers[name];
}

bool LogManager::isEnabled(const std::string& sub, const char* type) {
    auto it = states.find(sub);
    if (it == states.end()) return true; 
    if (!it->second.masterEnabled) return false;
    if (type == nullptr) return true;
    
    auto sit = it->second.subtypes.find(type);
    if (sit == it->second.subtypes.end()) return true;
    return sit->second;
}

void LogManager::setSubsystem(const std::string& sub, bool enable) {
    if (states.count(sub)) {
        states[sub].masterEnabled = enable;
    }
}

void LogManager::setSubtype(const std::string& sub, const std::string& type, bool enable) {
    if (states.count(sub) && states[sub].subtypes.count(type)) {
        states[sub].subtypes[type] = enable;
    }
}

void LogManager::setAll(bool enable) {
    for (auto& s : states) {
        s.second.masterEnabled = enable;
    }
}

void LogManager::listSubsystems(const struct shell* sh) {
    shell_print(sh, "%-12s | %s", "Subsystem", "State");
    shell_print(sh, "-------------|-------");
    for (const auto& s : states) {
        shell_print(sh, "%-12s | %s", s.first.c_str(), 
                    s.second.masterEnabled ? "ENABLED" : "disabled");
    }
}

void LogManager::listSubtypes(const struct shell* sh, const std::string& sub) {
    if (states.count(sub)) {
        shell_print(sh, "Subsystem: %s (%s)", sub.c_str(), 
                    states[sub].masterEnabled ? "ENABLED" : "disabled");
        for (const auto& st : states[sub].subtypes) {
            shell_print(sh, "  + %-10s : %s", st.first.c_str(), 
                        st.second ? "ON" : "off");
        }
    } else {
        shell_error(sh, "Unknown subsystem: %s", sub.c_str());
    }
}

} // namespace wspr
