#include "utils/Logger.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <iostream>

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::~Logger()
{
    close();
}

void Logger::setLogPath(const std::string& path)
{
    logPath_ = path;
}

bool Logger::open()
{
    if (logPath_.empty()) return false;
    file_.open(logPath_, std::ios::out | std::ios::app);
    return file_.is_open();
}

void Logger::close()
{
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

void Logger::info(const std::string& message)
{
    write("INFO", message);
}

void Logger::warn(const std::string& message)
{
    write("WARN", message);
}

void Logger::error(const std::string& message)
{
    write("ERROR", message);
}

std::string Logger::timestamp() const
{
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tmBuf;
    localtime_s(&tmBuf, &timeT);

    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

void Logger::write(const std::string& level, const std::string& message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_.is_open()) return;

    std::string line = "[" + timestamp() + "] [" + level + "] " + message + "\n";
    file_ << line;
    file_.flush();

    std::cout << line;
}
