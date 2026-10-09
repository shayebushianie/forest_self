#ifndef LOGGER_H
#define LOGGER_H

#include <cstddef>
#include <string>
#include <fstream>
#include <mutex>

class Logger {
public:
    struct RuntimeStatus {
        enum class State { NotEnabled, Writing, FailedStopped };

        State state = State::NotEnabled;
        std::string failureReason;
    };

    static Logger& instance();

    void setLogPath(const std::string& path);
    void setSessionId(const std::string& sessionId);
    bool open();
    void close();
    RuntimeStatus runtimeStatus() const;

    void info(const std::string& message,
              const std::string& module = "app",
              const std::string& operation = "");
    void warn(const std::string& message,
              const std::string& module = "app",
              const std::string& operation = "");
    void error(const std::string& message,
               const std::string& module = "app",
               const std::string& operation = "");

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;
    ~Logger();

    std::string timestamp() const;
    std::string sanitize(const std::string& message) const;
    bool rotateIfNeeded(std::size_t upcomingBytes);
    void reportFailure(const char* reason);
    void write(const std::string& level, const std::string& message,
               const std::string& module, const std::string& operation);

    std::string logPath_;
    std::string sessionId_;
    std::ofstream file_;
    mutable std::mutex mutex_;
    bool failureReported_ = false;
    RuntimeStatus runtimeStatus_;
};

#endif // LOGGER_H
