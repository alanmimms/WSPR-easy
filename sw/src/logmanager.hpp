#pragma once

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdarg>

namespace wspr {

class LogManager;

/**
 * @brief Logger handle for a specific subsystem
 */
class Logger {
public:
    Logger(const std::string& name, LogManager& mgr);

    void inf(const char* type, const char* fmt, ...);
    void dbg(const char* type, const char* fmt, ...);
    void wrn(const char* type, const char* fmt, ...);
    void err(const char* type, const char* fmt, ...);

    // Overloads for no-subtype logging
    void inf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vlog(LOG_LEVEL_INF, nullptr, fmt, args);
        va_end(args);
    }
    void dbg(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vlog(LOG_LEVEL_DBG, nullptr, fmt, args);
        va_end(args);
    }
    void wrn(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vlog(LOG_LEVEL_WRN, nullptr, fmt, args);
        va_end(args);
    }
    void err(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vlog(LOG_LEVEL_ERR, nullptr, fmt, args);
        va_end(args);
    }

    bool isEnabled(const char* type = nullptr) const;

private:
    void vlog(int level, const char* type, const char* fmt, va_list args);

    std::string name;
    LogManager& manager;
};

struct SubsystemState {
    bool masterEnabled;
    std::map<std::string, bool> subtypes;
};

class LogManager {
public:
    static LogManager& instance();

    /**
     * @brief Register a subsystem for logging
     * @param name Name of the subsystem
     * @param subtypes Optional list of subtypes for granular control
     * @return Reference to a Logger instance for this subsystem
     */
    Logger& registerSubsystem(const std::string& name, 
                             std::vector<std::string> subtypes = {});

    bool isEnabled(const std::string& sub, const char* type = nullptr);

    void setSubsystem(const std::string& sub, bool enable);
    void setSubtype(const std::string& sub, const std::string& type, bool enable);
    void setAll(bool enable);

    void listSubsystems(const struct shell* sh);
    void listSubtypes(const struct shell* sh, const std::string& sub);

private:
    LogManager() = default;
    
    std::map<std::string, SubsystemState> states;
    std::map<std::string, std::unique_ptr<Logger>> loggers;
};

} // namespace wspr
