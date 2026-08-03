#pragma once
#include <QString>

namespace masdr {

class Logger {
public:
    static void info (const QString& s);
    static void warn (const QString& s);
    static void error(const QString& s);
    static void debug(const QString& s);
};

} // namespace masdr
