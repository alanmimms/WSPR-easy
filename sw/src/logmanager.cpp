#include "logmanager.hpp"

namespace wspr {

LogManager& LogManager::instance() {
    static LogManager inst;
    return inst;
}

} // namespace wspr
