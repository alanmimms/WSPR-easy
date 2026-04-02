#pragma once

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <string>
#include <vector>
#include <map>

namespace wspr {

struct SubsystemState {
    bool masterEnabled;
    std::map<std::string, bool> subtypes;
};

class LogManager {
public:
    static LogManager& instance();

    bool isEnabled(const std::string& sub, const std::string& type = "") {
        auto it = states.find(sub);
        if (it == states.end()) return true; // Default to on for unknown
        if (!it->second.masterEnabled) return false;
        if (type.empty()) return true;
        auto sit = it->second.subtypes.find(type);
        if (sit == it->second.subtypes.end()) return true;
        return sit->second;
    }

    void setSubsystem(const std::string& sub, bool enable) {
        if (states.count(sub)) {
            states[sub].masterEnabled = enable;
            // When enabling/disabling subsystem, also apply to all subtypes
            for (auto& st : states[sub].subtypes) {
                st.second = enable;
            }
        }
    }

    void setAll(bool enable) {
        for (auto& s : states) {
            s.second.masterEnabled = enable;
            for (auto& st : s.second.subtypes) {
                st.second = enable;
            }
        }
    }

    void listSubsystems(const struct shell* sh) {
        shell_print(sh, "%-12s | %s", "Subsystem", "State");
        shell_print(sh, "-------------|-------");
        for (const auto& s : states) {
            shell_print(sh, "%-12s | %s", s.first.c_str(), s.second.masterEnabled ? "ENABLED" : "disabled");
        }
    }

    void listSubtypes(const struct shell* sh, const std::string& sub) {
        if (states.count(sub)) {
            shell_print(sh, "Subsystem: %s (%s)", sub.c_str(), states[sub].masterEnabled ? "ENABLED" : "disabled");
            for (const auto& st : states[sub].subtypes) {
                shell_print(sh, "  + %-10s : %s", st.first.c_str(), st.second ? "ON" : "off");
            }
        } else {
            shell_error(sh, "Unknown subsystem: %s", sub.c_str());
        }
    }

private:
    LogManager() {
        // Initialize subsystems and subtypes
        states["fpga"] = {true, {{"spi", true}, {"pps", true}, {"config", true}}};
        states["gnss"] = {true, {{"raw", true}, {"fix", true}, {"time", true}}};
        states["wifi"] = {true, {{"mgmt", true}, {"dhcp", true}, {"rssi", true}}};
        states["web"]  = {true, {{"req", true}, {"api", true}}};
        states["fs"]   = {true, {{"mount", true}, {"ops", true}}};
        states["tx"]   = {true, {{"freq", true}, {"pwr", true}, {"sym", true}}};
        states["sys"]  = {true, {{"heap", true}, {"uptime", true}}};
    }

    std::map<std::string, SubsystemState> states;
};

// Convenience macro for logging
#define WSPR_LOG(sub, type, ...) \
    do { \
        if (wspr::LogManager::instance().isEnabled(sub, type)) { \
            LOG_INF(__VA_ARGS__); \
        } \
    } while (0)

} // namespace wspr
