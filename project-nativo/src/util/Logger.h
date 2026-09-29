#pragma once
#include <string>

namespace masdr {

class Logger {
public:
    static void info (const std::string& s);
    static void warn (const std::string& s);
    static void error(const std::string& s);
    static void debug(const std::string& s);
};

} // namespace masdr
