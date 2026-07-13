#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>

class Logger {
public:
    static Logger& instance();

    void setLogPath(const std::string& path);
    void setSessionId(const std::string& sessionId);
    bool open();
    void close();

    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;
    ~Logger();

    std::string timestamp() const;
    std::string sanitize(const std::string& message) const;
    void rotateIfNeeded();
    void write(const std::string& level, const std::string& message);

    std::string logPath_;
    std::string sessionId_;
    std::ofstream file_;
    std::mutex mutex_;
};

#endif // LOGGER_H
