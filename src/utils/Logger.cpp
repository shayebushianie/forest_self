#include "utils/Logger.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <cctype>
#include <QFile>
#include <QFileInfo>

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

void Logger::setSessionId(const std::string& sessionId)
{
    sessionId_ = sessionId;
}

bool Logger::open()
{
    if (logPath_.empty()) return false;
    rotateIfNeeded();
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

    const std::string session = sessionId_.empty() ? std::string() : " [session=" + sessionId_ + "]";
    std::string line = "[" + timestamp() + "] [" + level + "]" + session + " " + sanitize(message) + "\n";
    file_ << line;
    file_.flush();

    std::cout << line;
}

std::string Logger::sanitize(const std::string& message) const
{
    const std::string lower = [&message] {
        std::string value = message;
        for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value;
    }();
    if (lower.find("password") != std::string::npos || lower.find("token") != std::string::npos ||
        lower.find("secret") != std::string::npos) {
        return "[sensitive message redacted]";
    }
    return message;
}

void Logger::rotateIfNeeded()
{
    constexpr qint64 kMaxBytes = 1024 * 1024;
    const QString path = QString::fromStdString(logPath_);
    if (!QFileInfo::exists(path) || QFileInfo(path).size() < kMaxBytes) return;
    for (int index = 2; index >= 1; --index) {
        const QString from = path + QStringLiteral(".%1").arg(index);
        const QString to = path + QStringLiteral(".%1").arg(index + 1);
        QFile::remove(to);
        if (QFileInfo::exists(from)) QFile::rename(from, to);
    }
    QFile::remove(path + QStringLiteral(".1"));
    QFile::rename(path, path + QStringLiteral(".1"));
}
