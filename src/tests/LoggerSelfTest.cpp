#include "utils/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>

namespace {

constexpr std::size_t kMaxLogBytes = 1024 * 1024;

class NullBuffer : public std::streambuf {
protected:
    int_type overflow(int_type character) override
    {
        return traits_type::not_eof(character);
    }

    std::streamsize xsputn(const char*, std::streamsize count) override
    {
        return count;
    }
};

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAILED: " << message << "\n";
    return condition;
}

bool readFile(const QString& path, std::string& contents)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = file.readAll();
    contents.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    return true;
}

bool configureLogger(const QString& path)
{
    Logger& logger = Logger::instance();
    logger.close();
    logger.setLogPath(path.toStdString());
    logger.setSessionId("selftest-session");
    return logger.open();
}

bool testSanitizationAndMetadata(const QString& root)
{
    if (!QDir().mkpath(root)) return expect(false, "create metadata temp directory");
    const QString path = root + QStringLiteral("/app.log");
    if (!expect(configureLogger(path), "open temporary metadata log")) return false;
    if (!expect(Logger::instance().runtimeStatus().state == Logger::RuntimeStatus::State::Writing,
                "logger reports active writes after opening")) return false;

    Logger::instance().info(
        "userId=9, password=top-secret, access_token=token-secret, "
        "secret=\"hidden-value\", keep=value\r\nnext\tfield\x01\xE2\x80\xA8",
        "auth\ninjected", "login");
    Logger::instance().warn("legacy-call\r\nnext\tfield\x01\xE2\x80\xA8");
    Logger::instance().close();
    if (!expect(Logger::instance().runtimeStatus().state == Logger::RuntimeStatus::State::NotEnabled,
                "closing an active logger reports it as not enabled")) return false;

    std::string contents;
    if (!expect(readFile(path, contents), "read temporary metadata log")) return false;
    if (!expect(contents.find("[session=selftest-session]") != std::string::npos,
                "session metadata remains present")) return false;
    if (!expect(contents.find("[module=auth_injected] [operation=login]") != std::string::npos,
                "optional module and operation identifiers are safe")) return false;
    if (!expect(contents.find("top-secret") == std::string::npos &&
                contents.find("token-secret") == std::string::npos &&
                contents.find("hidden-value") == std::string::npos,
                "sensitive field values are redacted")) return false;
    if (!expect(contents.find("[sensitive message redacted]") != std::string::npos &&
                contents.find("next field") != std::string::npos,
                "sensitive messages are fully removed and other messages retain useful context")) return false;
    if (!expect(contents.find("[WARN]") != std::string::npos &&
                contents.find("[module=app]") != std::string::npos &&
                contents.find("legacy-call") != std::string::npos,
                "existing one-argument calls remain compatible")) return false;
    if (!expect(contents.find('\r') == std::string::npos &&
                contents.find('\t') == std::string::npos &&
                std::count(contents.begin(), contents.end(), '\n') == 2,
                "control characters cannot create extra log lines")) return false;
    return true;
}

bool testStartupRotationAndUnicodePath(const QString& root)
{
    if (!QDir().mkpath(root)) return false;
    const QString path = root + QStringLiteral("/诊断日志.log");
    QFile existing(path);
    if (!existing.open(QIODevice::WriteOnly)) return false;
    if (existing.write(QByteArray(static_cast<int>(kMaxLogBytes + 1), 'x')) !=
        static_cast<qint64>(kMaxLogBytes + 1)) return false;
    existing.close();
    if (!expect(configureLogger(path), "startup rotation on a Unicode path failed")) return false;
    Logger::instance().info("after-startup-rotation");
    Logger::instance().close();
    std::string contents;
    return expect(readFile(path, contents) && contents.find("after-startup-rotation") != std::string::npos &&
                      QFileInfo(path + QStringLiteral(".1")).size() == static_cast<qint64>(kMaxLogBytes + 1),
                  "startup rotation lost old log data or reopened the stream twice");
}

bool testOversizedMessageIsBounded(const QString& root)
{
    if (!QDir().mkpath(root)) return expect(false, "create oversized-message temp directory");
    const QString path = root + QStringLiteral("/app.log");
    if (!expect(configureLogger(path), "open temporary oversized-message log")) return false;

    Logger::instance().info(std::string(2 * 1024 * 1024, 'x'));
    Logger::instance().info(std::string(2 * 1024 * 1024, static_cast<char>(0x80)));
    Logger::instance().close();

    std::string contents;
    if (!expect(readFile(path, contents), "read oversized-message log")) return false;
    return expect(contents.size() <= kMaxLogBytes, "oversized record stays within the file limit") &&
           expect(contents.find("[truncated]") != std::string::npos,
                  "oversized or malformed UTF-8 record lacks a truncation marker");
}

bool testRuntimeRotationAndHistoryLimit(const QString& root)
{
    if (!QDir().mkpath(root)) return expect(false, "create rotation temp directory");
    const QString path = root + QStringLiteral("/app.log");
    if (!expect(configureLogger(path), "open temporary rotation log")) return false;

    const std::string payload(600 * 1024, 'x');
    for (int index = 1; index <= 5; ++index) {
        Logger::instance().info("batch-" + std::to_string(index) + ":" + payload);
    }
    Logger::instance().close();

    std::string current;
    std::string first;
    std::string second;
    std::string third;
    if (!expect(readFile(path, current), "read current rotated log") ||
        !expect(readFile(path + QStringLiteral(".1"), first), "read first history file") ||
        !expect(readFile(path + QStringLiteral(".2"), second), "read second history file") ||
        !expect(readFile(path + QStringLiteral(".3"), third), "read third history file")) {
        return false;
    }

    for (const QString& filePath : {path, path + QStringLiteral(".1"),
                                    path + QStringLiteral(".2"), path + QStringLiteral(".3")}) {
        if (!expect(QFileInfo(filePath).size() <= static_cast<qint64>(kMaxLogBytes),
                    "every log file stays within one MiB")) return false;
    }

    return expect(current.find("batch-5:") != std::string::npos &&
                  first.find("batch-4:") != std::string::npos &&
                  second.find("batch-3:") != std::string::npos &&
                  third.find("batch-2:") != std::string::npos,
                  "runtime rotation retains the three newest history files") &&
           expect(!QFileInfo::exists(path + QStringLiteral(".4")),
                  "rotation never creates a fourth history file");
}

bool testRotationFailureStopsWrites(const QString& root)
{
    if (!QDir().mkpath(root)) return expect(false, "create rotation-failure temp directory");
    const QString path = root + QStringLiteral("/app.log");
    if (!expect(configureLogger(path), "open temporary failure-case log")) return false;

    Logger::instance().info("seed=" + std::string(900 * 1024, 's'));
    const qint64 originalSize = QFileInfo(path).size();
    const QString blockedHistory = path + QStringLiteral(".3");
    if (!QDir().mkpath(blockedHistory)) {
        Logger::instance().close();
        return expect(false, "create temporary rotation obstruction");
    }
    QFile marker(blockedHistory + QStringLiteral("/keep"));
    if (!marker.open(QIODevice::WriteOnly)) {
        Logger::instance().close();
        return expect(false, "create temporary obstruction marker");
    }
    marker.write("test");
    marker.close();

    std::ostringstream errors;
    std::streambuf* oldErrors = std::cerr.rdbuf(errors.rdbuf());
    Logger::instance().info("trigger=" + std::string(256 * 1024, 't'));
    Logger::instance().info("after-failed-rotation");
    std::cerr.rdbuf(oldErrors);
    Logger::instance().close();

    std::string contents;
    if (!expect(readFile(path, contents), "read log after failed rotation")) return false;
    if (!expect(errors.str().find("rotation failed") != std::string::npos,
                "rotation failure is reported")) return false;
    if (!expect(QFileInfo(path).size() == originalSize,
                "logger stops instead of appending after rotation failure")) return false;
    if (!expect(contents.find("trigger=") == std::string::npos &&
                contents.find("after-failed-rotation") == std::string::npos,
                "failed and subsequent records are not written")) return false;

    const auto failedStatus = Logger::instance().runtimeStatus();
    if (!expect(failedStatus.state == Logger::RuntimeStatus::State::FailedStopped &&
                failedStatus.failureReason == "rotation failed",
                "rotation failure exposes a fixed stopped-state reason")) return false;
    Logger::instance().close();
    if (!expect(Logger::instance().runtimeStatus().state == Logger::RuntimeStatus::State::FailedStopped,
                "closing after failure does not report normal logging")) return false;

    const QString recoveryPath = root + QStringLiteral("/recovered.log");
    Logger::instance().setLogPath(recoveryPath.toStdString());
    if (!expect(Logger::instance().runtimeStatus().state == Logger::RuntimeStatus::State::FailedStopped,
                "changing the path does not report normal logging")) return false;
    if (!expect(Logger::instance().open(), "reopen logger on a healthy path")) return false;
    const auto recoveredStatus = Logger::instance().runtimeStatus();
    return expect(recoveredStatus.state == Logger::RuntimeStatus::State::Writing &&
                  recoveredStatus.failureReason.empty(),
                  "a successful open clears the failure and restores active status");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporaryDirectory;
    if (!expect(temporaryDirectory.isValid(), "create isolated temporary test root")) return 1;

    NullBuffer nullOutput;
    std::streambuf* oldOutput = std::cout.rdbuf(&nullOutput);
    const QString root = temporaryDirectory.path();
    const bool passed =
        testSanitizationAndMetadata(root + QStringLiteral("/metadata")) &&
        testStartupRotationAndUnicodePath(root + QStringLiteral("/startup")) &&
        testOversizedMessageIsBounded(root + QStringLiteral("/oversized")) &&
        testRuntimeRotationAndHistoryLimit(root + QStringLiteral("/rotation")) &&
        testRotationFailureStopsWrites(root + QStringLiteral("/failure"));
    Logger::instance().close();
    std::cout.rdbuf(oldOutput);
    return passed ? 0 : 1;
}
