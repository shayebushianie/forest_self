#include "utils/Logger.h"
#include <chrono>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <iostream>

#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <filesystem>

namespace {

constexpr std::size_t kMaxLogBytes = 1024 * 1024;
constexpr std::size_t kMaxMetadataBytes = 512;
constexpr std::size_t kMaxMessageBytes = kMaxLogBytes - kMaxMetadataBytes - 1;
constexpr std::size_t kMaxSessionIdBytes = 128;
constexpr std::size_t kMaxIdentifierBytes = 64;
const std::string kTruncationMarker = "...[truncated]";

std::size_t utf8PrefixLength(const std::string& value, std::size_t maxBytes)
{
    std::size_t end = std::min(value.size(), maxBytes);
    while (end > 0 && end < value.size() &&
           (static_cast<unsigned char>(value[end]) & 0xC0) == 0x80) --end;
    return end;
}

std::string sanitizeIdentifier(const std::string& value, std::size_t maxBytes)
{
    std::string result;
    result.reserve(std::min(value.size(), maxBytes));
    for (unsigned char character : value) {
        if (result.size() == maxBytes) break;
        const bool allowed = (character >= 'a' && character <= 'z') ||
                             (character >= 'A' && character <= 'Z') ||
                             (character >= '0' && character <= '9') ||
                             character == '_' || character == '-' || character == '.';
        result.push_back(allowed ? static_cast<char>(character) : '_');
    }
    return result;
}

} // namespace

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
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
        if (!file_) reportFailure("flush failed");
        file_.close();
    }
    file_.clear();
    if (runtimeStatus_.state == RuntimeStatus::State::Writing) {
        runtimeStatus_.state = RuntimeStatus::State::NotEnabled;
    }
    logPath_ = path;
}

void Logger::setSessionId(const std::string& sessionId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sessionId_ = sanitizeIdentifier(sessionId, kMaxSessionIdBytes);
}

bool Logger::open()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
    file_.clear();
    failureReported_ = false;
    runtimeStatus_.state = RuntimeStatus::State::NotEnabled;
    runtimeStatus_.failureReason.clear();

    if (logPath_.empty()) {
        reportFailure("empty log path");
        return false;
    }
    if (!rotateIfNeeded(0)) {
        reportFailure("rotation failed");
        return false;
    }

    file_.clear();
    file_.open(std::filesystem::path(QString::fromStdString(logPath_).toStdWString()),
               std::ios::out | std::ios::app | std::ios::binary);
    if (!file_.is_open()) {
        reportFailure("open failed");
        return false;
    }
    runtimeStatus_.state = RuntimeStatus::State::Writing;
    return true;
}

void Logger::close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
        if (!file_) reportFailure("flush failed");
        file_.close();
        if (file_.fail()) reportFailure("close failed");
    }
    if (runtimeStatus_.state == RuntimeStatus::State::Writing) {
        runtimeStatus_.state = RuntimeStatus::State::NotEnabled;
    }
}

Logger::RuntimeStatus Logger::runtimeStatus() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return runtimeStatus_;
}

void Logger::info(const std::string& message, const std::string& module,
                  const std::string& operation)
{
    write("INFO", message, module, operation);
}

void Logger::warn(const std::string& message, const std::string& module,
                  const std::string& operation)
{
    write("WARN", message, module, operation);
}

void Logger::error(const std::string& message, const std::string& module,
                   const std::string& operation)
{
    write("ERROR", message, module, operation);
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

void Logger::write(const std::string& level, const std::string& message,
                   const std::string& module, const std::string& operation)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_.is_open()) return;

    std::string metadata;
    if (!sessionId_.empty()) metadata += " [session=" + sessionId_ + "]";
    const std::string safeModule = sanitizeIdentifier(module, kMaxIdentifierBytes);
    const std::string safeOperation = sanitizeIdentifier(operation, kMaxIdentifierBytes);
    metadata += " [module=" + (safeModule.empty() ? std::string("app") : safeModule) + "]";
    metadata += " [operation=" + (safeOperation.empty() ? std::string("event") : safeOperation) + "]";

    std::string line = "[" + timestamp() + "] [" + level + "]" +
                       metadata + " " + sanitize(message) + "\n";
    if (line.size() > kMaxLogBytes) {
        line.resize(utf8PrefixLength(line, kMaxLogBytes - 1));
        line.push_back('\n');
    }

    if (!rotateIfNeeded(line.size())) {
        if (file_.is_open()) file_.close();
        reportFailure("rotation failed");
        return;
    }

    file_ << line;
    file_.flush();
    if (!file_) {
        if (file_.is_open()) file_.close();
        reportFailure("write failed");
        return;
    }

    std::cout << line;
}

std::string Logger::sanitize(const std::string& message) const
{
    std::string bounded;
    if (message.size() > kMaxMessageBytes) {
        const std::size_t prefixLength =
            utf8PrefixLength(message, kMaxMessageBytes - kTruncationMarker.size());
        bounded.assign(message.data(), prefixLength);
        bounded += kTruncationMarker;
    } else {
        bounded = message;
    }

    QString text = QString::fromUtf8(bounded.data(), static_cast<int>(bounded.size()));
    for (qsizetype index = 0; index < text.size(); ++index) {
        const QChar character = text.at(index);
        if (character.category() == QChar::Other_Control ||
            character.category() == QChar::Separator_Line ||
            character.category() == QChar::Separator_Paragraph ||
            character.category() == QChar::Other_Format) {
            text[index] = QLatin1Char(' ');
        }
    }

    static const QStringList sensitiveKeywords = {
        QStringLiteral("password"), QStringLiteral("passwd"), QStringLiteral("token"),
        QStringLiteral("secret"), QStringLiteral("authorization"), QStringLiteral("credential"),
        QStringLiteral("api_key"), QStringLiteral("api-key"), QStringLiteral("apikey"),
        QStringLiteral("密码"), QStringLiteral("密钥"), QStringLiteral("口令")
    };
    const QString lower = text.toLower();
    for (const QString& keyword : sensitiveKeywords) {
        if (lower.contains(keyword)) return "[sensitive message redacted]";
    }
    std::string result = text.toUtf8().toStdString();
    if (result.size() > kMaxMessageBytes) {
        const std::size_t prefixLength =
            utf8PrefixLength(result, kMaxMessageBytes - kTruncationMarker.size());
        result.resize(prefixLength);
        result += kTruncationMarker;
    }
    return result;
}

bool Logger::rotateIfNeeded(std::size_t upcomingBytes)
{
    if (upcomingBytes > kMaxLogBytes) return false;

    const QString path = QString::fromStdString(logPath_);
    if (!QFileInfo::exists(path)) return true;

    const qint64 currentSize = QFileInfo(path).size();
    if (currentSize < 0) return false;
    const qint64 allowedCurrentSize = static_cast<qint64>(kMaxLogBytes - upcomingBytes);
    if (currentSize <= allowedCurrentSize) return true;

    const bool reopen = file_.is_open();
    if (reopen) {
        file_.flush();
        if (!file_) return false;
        file_.close();
        if (file_.fail()) return false;
    }

    const auto removeIfExists = [](const QString& filePath) {
        return !QFileInfo::exists(filePath) || QFile::remove(filePath);
    };
    if (!removeIfExists(path + QStringLiteral(".3"))) return false;

    for (int index = 2; index >= 1; --index) {
        const QString from = path + QStringLiteral(".%1").arg(index);
        const QString to = path + QStringLiteral(".%1").arg(index + 1);
        if (QFileInfo::exists(from) && !QFile::rename(from, to)) return false;
    }
    if (!QFile::rename(path, path + QStringLiteral(".1"))) return false;

    if (!reopen) return true;
    file_.clear();
    file_.open(std::filesystem::path(QString::fromStdString(logPath_).toStdWString()),
               std::ios::out | std::ios::app | std::ios::binary);
    return file_.is_open();
}

void Logger::reportFailure(const char* reason)
{
    runtimeStatus_.state = RuntimeStatus::State::FailedStopped;
    if (failureReported_) return;
    runtimeStatus_.failureReason = reason;
    std::cerr << "Logger disabled: " << reason << ".\n";
    failureReported_ = true;
}
