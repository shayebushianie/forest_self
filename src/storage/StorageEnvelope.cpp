#include "storage/StorageEnvelope.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

namespace StorageEnvelope {
namespace {

QSet<QString> writeFailuresForTesting;

constexpr quint32 kMaxPayloadBytes = 64u * 1024u * 1024u;

quint32 checksum(const QByteArray& bytes)
{
    quint32 value = 2166136261u;
    for (const char byte : bytes) {
        value ^= static_cast<quint8>(byte);
        value *= 16777619u;
    }
    return value;
}

ReadResult readOne(const QString& path, quint32 expectedMagic)
{
    QFile file(path);
    if (!file.exists()) {
        return {ReadState::Missing, 0, {}, {}};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return {ReadState::Corrupt, 0, {}, QStringLiteral("无法读取数据文件")};
    }
    if (file.size() == 0) {
        return {ReadState::Legacy, 0, {}, {}};
    }

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_5);
    quint32 magic = 0;
    quint16 version = 0;
    quint32 size = 0;
    quint32 storedChecksum = 0;
    stream >> magic >> version >> size >> storedChecksum;
    if (stream.status() != QDataStream::Ok) {
        if (file.size() == static_cast<qint64>(sizeof(quint32))) {
            return {ReadState::Legacy, 0, {}, {}};
        }
        return {ReadState::Corrupt, 0, {}, QStringLiteral("数据文件头不完整")};
    }
    if (magic != expectedMagic) {
        return {ReadState::Legacy, 0, {}, {}};
    }
    if (size > kMaxPayloadBytes) {
        return {ReadState::Corrupt, version, {}, QStringLiteral("数据文件大小异常")};
    }

    const QByteArray payload = file.read(size);
    if (payload.size() != static_cast<qsizetype>(size) || !file.atEnd()) {
        return {ReadState::Corrupt, version, {}, QStringLiteral("数据文件内容不完整")};
    }
    if (checksum(payload) != storedChecksum) {
        return {ReadState::Corrupt, version, {}, QStringLiteral("数据校验失败")};
    }
    return {ReadState::Valid, version, payload, {}};
}

bool restorePrimary(const QString& primaryPath, const QString& backupPath)
{
    QFile backup(backupPath);
    if (!backup.open(QIODevice::ReadOnly)) {
        return false;
    }
    QSaveFile output(primaryPath);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray bytes = backup.readAll();
    return output.write(bytes) == bytes.size() && output.commit();
}

} // namespace

ReadResult read(const QString& path, quint32 expectedMagic)
{
    ReadResult primary = readOne(path, expectedMagic);
    const QString backupPath = path + QStringLiteral(".bak");
    if (primary.state == ReadState::Legacy) {
        ReadResult backup = readOne(backupPath, expectedMagic);
        if (backup.state == ReadState::Valid && restorePrimary(path, backupPath)) {
            backup.state = ReadState::RecoveredFromBackup;
            return backup;
        }
        return primary;
    }
    if (primary.state != ReadState::Corrupt) {
        return primary;
    }

    ReadResult backup = readOne(backupPath, expectedMagic);
    if (backup.state == ReadState::Valid && restorePrimary(path, backupPath)) {
        backup.state = ReadState::RecoveredFromBackup;
        return backup;
    }
    return primary;
}

bool write(const QString& path, quint32 magic, quint16 version,
           const QByteArray& payload, QString* error)
{
    if (writeFailuresForTesting.remove(path)) {
        if (error) *error = QStringLiteral("测试注入的保存失败");
        return false;
    }
    if (payload.size() > static_cast<qsizetype>(kMaxPayloadBytes)) {
        if (error) *error = QStringLiteral("数据文件超过安全大小限制");
        return false;
    }

    const QFileInfo info(path);
    if (!QDir().mkpath(info.dir().absolutePath())) {
        if (error) *error = QStringLiteral("无法创建数据目录");
        return false;
    }

    const QString backupPath = path + QStringLiteral(".bak");
    // Never replace a known-good backup with a damaged primary file.
    const ReadResult primary = readOne(path, magic);
    if (primary.state == ReadState::Valid || primary.state == ReadState::Legacy) {
        const QString previousBackupPath = backupPath + QStringLiteral(".previous");
        if (QFile::exists(backupPath) && !QFile::exists(previousBackupPath)) {
            QFile::copy(backupPath, previousBackupPath);
        }
        QFile::remove(backupPath);
        if (!QFile::copy(path, backupPath)) {
            if (error) *error = QStringLiteral("无法创建数据备份");
            return false;
        }
    }

    QByteArray content;
    QDataStream stream(&content, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_5);
    stream << magic << version << static_cast<quint32>(payload.size()) << checksum(payload);
    content.append(payload);

    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(content) != content.size() || !output.commit()) {
        if (error) *error = QStringLiteral("原子保存失败");
        return false;
    }
    return true;
}

void failNextWriteForTesting(const QString& path)
{
    writeFailuresForTesting.insert(path);
}

void clearWriteFailuresForTesting()
{
    writeFailuresForTesting.clear();
}

} // namespace StorageEnvelope
