#include "storage/RestoreTransaction.h"
#include "storage/BackupSnapshot.h"
#include "storage/DataPathSafety.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace {
const QString journalName = QStringLiteral("restore_transaction.json");

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

QString transactionRoot(const QString& data, const QString& id)
{
    return QFileInfo(data).dir().filePath(QStringLiteral("ForestRestoreSnapshots/Restore-") + id);
}

bool safeFile(const QString& root, const QString& relative)
{
    const QString path = QDir(root).filePath(relative);
    return !DataPathSafety::hasLinkedComponent(root, path) &&
           DataPathSafety::contains(root, path);
}

QByteArray digest(const QString& path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return {};
    return hash.result().toHex();
}

bool writeJson(const QString& path, const QJsonObject& object, QString* error)
{
    QSaveFile file(path);
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, QStringLiteral("无法保存恢复事务：%1").arg(path));
    return true;
}

bool readJson(const QString& path, QJsonObject* object, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024)
        return fail(error, QStringLiteral("无法读取恢复事务：%1").arg(path));
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("恢复事务清单损坏：%1").arg(path));
    *object = document.object();
    return true;
}

bool managedFile(const QString& relative)
{
    if (BackupSnapshot::isManagedPath(relative)) return true;
    for (const QString& suffix : {QStringLiteral("-wal"), QStringLiteral("-shm")}) {
        if (relative.endsWith(suffix) && relative.endsWith(QStringLiteral("sessions.sqlite") + suffix))
            return BackupSnapshot::isManagedPath(relative.left(relative.size() - suffix.size()));
    }
    return false;
}

bool listManaged(const QString& root, QStringList* result, QString* error)
{
    QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const auto path = it.next();
        const auto relative = QDir(root).relativeFilePath(path);
        if (it.fileInfo().isDir()) {
            if (DataPathSafety::hasLinkedComponent(root, path))
                return fail(error, QStringLiteral("恢复目录包含不安全链接：%1").arg(relative));
            continue;
        }
        if (!managedFile(relative)) continue;
        if (!safeFile(root, relative) || !QFileInfo(path).isFile())
            return fail(error, QStringLiteral("恢复目标包含不安全的数据文件：%1").arg(relative));
        result->append(relative);
    }
    result->sort();
    return true;
}

bool verifyInstalled(const QString& data, const QString& stage, const QStringList& files, QString* error)
{
    QStringList current;
    if (!listManaged(data, &current, error)) return false;
    if (current != files) return fail(error, QStringLiteral("恢复后的受管理文件集合不一致，未加载账户"));
    for (const auto& name : files) {
        if (!safeFile(data, name) || !safeFile(stage, name))
            return fail(error, QStringLiteral("恢复后文件路径不安全：%1").arg(name));
        const auto wanted = digest(QDir(stage).filePath(name));
        if (wanted.isEmpty() || digest(QDir(data).filePath(name)) != wanted)
            return fail(error, QStringLiteral("恢复后文件校验失败：%1；请保留恢复快照与日志").arg(name));
    }
    return true;
}

bool archiveFile(const QString& data, const QString& relative, const QString& root, QString* error)
{
    const QString path = QDir(data).filePath(relative);
    if (!QFileInfo::exists(path)) return true;
    const QString archive = QDir(root).filePath(QStringLiteral("quarantine/") +
        QUuid::createUuid().toString(QUuid::WithoutBraces) + '/' + relative);
    if (!safeFile(data, relative) || !QFileInfo(path).isFile() ||
        !DataPathSafety::contains(root, archive) || !QDir().mkpath(QFileInfo(archive).absolutePath()) ||
        !QFile::rename(path, archive))
        return fail(error, QStringLiteral("无法隔离恢复过程中替换的文件：%1").arg(relative));
    return true;
}

bool finish(const QString& data, const QString& root, QString* error)
{
    const QString request = QDir(data).filePath(QStringLiteral("restore_request.txt"));
    if (QFileInfo::exists(request) && !archiveFile(data, QStringLiteral("restore_request.txt"), root, error))
        return false;
    // Only the completed journal is removed; original data and snapshots remain available.
    if (!QFile::remove(QDir(data).filePath(journalName)))
        return fail(error, QStringLiteral("无法清除已完成的恢复事务，请检查权限后重启"));
    return true;
}

bool rollback(const QString& data, const QString& root, QJsonObject journal, QString* error,
              const RestoreTransaction::Fault* fault = nullptr)
{
    QJsonObject before;
    const QString beforeDirectory = QDir(root).filePath("before");
    const QString beforeManifest = QDir(root).filePath("before.json");
    if (QFileInfo(beforeDirectory).isSymLink() || QFileInfo(beforeDirectory).isJunction() ||
        !DataPathSafety::contains(root, beforeDirectory) || !safeFile(root, "before.json") ||
        digest(beforeManifest).isEmpty() ||
        digest(beforeManifest) != journal.value("beforeSha256").toString().toLatin1())
        return fail(error, QStringLiteral("恢复前清单校验失败，未加载账户；请保留快照"));
    if (!readJson(beforeManifest, &before, error)) return false;
    if (before.value("version").toInt() != 1 || !before.value("files").isArray())
        return fail(error, QStringLiteral("恢复前快照版本不受支持，未加载账户"));
    QSet<QString> originals;
    for (const auto& value : before.value("files").toArray()) {
        const auto item = value.toObject();
        const QString name = item.value("path").toString();
        const QString path = QDir(root).filePath(QStringLiteral("before/") + name);
        if (!managedFile(name) || originals.contains(name) || !safeFile(QDir(root).filePath("before"), name))
            return fail(error, QStringLiteral("恢复前快照路径校验失败：%1").arg(name));
        const auto hash = digest(path);
        if (QString::number(QFileInfo(path).size()) != item.value("size").toString() ||
            hash != item.value("sha256").toString().toLatin1() || hash.isEmpty())
            return fail(error, QStringLiteral("恢复前快照校验失败：%1；请保留日志与快照").arg(name));
        originals.insert(name);
    }
    journal.insert("phase", "rolling-back");
    if (!writeJson(QDir(data).filePath(journalName), journal, error)) return false;
    QStringList current;
    if (!listManaged(data, &current, error)) return false;
    for (const auto& name : current)
        if (!originals.contains(name) && !archiveFile(data, name, root, error)) return false;
    int copied = 0;
    for (const auto& name : originals) {
        if (!safeFile(data, name)) return fail(error, QStringLiteral("回滚目标路径不安全：%1").arg(name));
        if (!BackupSnapshot::copyFile(
            QDir(root).filePath(QStringLiteral("before/") + name), QDir(data).filePath(name), error)) return false;
        if (fault && fault->point == RestoreTransaction::Fault::Point::RollbackInterruption &&
            ++copied >= fault->afterFiles)
            return fail(error, QStringLiteral("测试：回滚中断"));
    }
    QStringList rolledBack;
    if (!listManaged(data, &rolledBack, error) ||
        QSet<QString>(rolledBack.begin(), rolledBack.end()) != originals)
        return fail(error, QStringLiteral("回滚后的文件集合不完整，未加载账户"));
    for (const auto& value : before.value("files").toArray()) {
        const auto item = value.toObject();
        const QString path = QDir(data).filePath(item.value("path").toString());
        if (digest(path) != item.value("sha256").toString().toLatin1())
            return fail(error, QStringLiteral("回滚后的文件校验失败，未加载账户：%1").arg(path));
    }
    return finish(data, root, error);
}

bool loadJournal(const QString& data, QJsonObject* journal, QString* root, QString* error)
{
    if (!safeFile(data, journalName))
        return fail(error, QStringLiteral("恢复事务路径不安全，未加载账户"));
    if (!readJson(QDir(data).filePath(journalName), journal, error)) return false;
    const QString id = journal->value("id").toString();
    const QUuid uuid(id);
    const QString phase = journal->value("phase").toString();
    if (journal->value("version").toInt() != 1 || uuid.isNull() ||
        uuid.toString(QUuid::WithoutBraces) != id ||
        (phase != "applying" && phase != "rolling-back" && phase != "committed"))
        return fail(error, QStringLiteral("恢复事务损坏或版本不受支持，未加载账户"));
    *root = transactionRoot(data, id);
    const auto parent = QFileInfo(data).dir().filePath(QStringLiteral("ForestRestoreSnapshots"));
    if (DataPathSafety::overlaps(data, *root) || !DataPathSafety::contains(parent, *root) ||
        QFileInfo(parent).isSymLink() || QFileInfo(parent).isJunction() ||
        QFileInfo(*root).isSymLink() || QFileInfo(*root).isJunction())
        return fail(error, QStringLiteral("恢复快照路径不安全，未加载账户"));
    return true;
}
}

bool RestoreTransaction::recover(const QString& data, QString* restoredFrom, QString* error,
                                 QString* recoveryNotice)
{
    if (restoredFrom) restoredFrom->clear();
    if (recoveryNotice) recoveryNotice->clear();
    if (!QFileInfo::exists(QDir(data).filePath(journalName))) return true;
    QJsonObject journal;
    QString root;
    if (!loadJournal(data, &journal, &root, error)) return false;
    if (journal.value("phase").toString() == "committed") {
        const QString stage = QDir(root).filePath("stage");
        BackupManifest manifest;
        if (!safeFile(stage, "backup_manifest.txt") ||
            digest(QDir(stage).filePath("backup_manifest.txt")) != journal.value("stageSha256").toString().toLatin1() ||
            !BackupSnapshot::validate(stage, &manifest, error) || !verifyInstalled(data, stage, manifest.files, error))
            return fail(error, QStringLiteral("已提交恢复的完整性复核失败，未加载账户。\n%1\n快照：%2")
                                   .arg(error ? *error : QString(), root));
        if (!finish(data, root, error)) return false;
        if (restoredFrom) *restoredFrom = journal.value("source").toString();
        return true;
    }
    if (!rollback(data, root, journal, error)) return false;
    if (recoveryNotice) *recoveryNotice = QStringLiteral("上次恢复被中断，现已回滚至恢复前的数据。\n"
        "安全快照已保留：\n%1").arg(root);
    return true;
}

bool RestoreTransaction::apply(const QString& data, const QString& source, QString* restoredFrom,
                               QString* error, const Fault* fault)
{
    if (restoredFrom) restoredFrom->clear();
    if (DataPathSafety::overlaps(data, source) || QFileInfo::exists(QDir(data).filePath(journalName)))
        return fail(error, QStringLiteral("恢复路径不安全或存在未完成事务"));
    BackupManifest manifest;
    if (!BackupSnapshot::validate(source, &manifest, error)) return false;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString root = transactionRoot(data, id);
    const QString stage = QDir(root).filePath(QStringLiteral("stage"));
    const QString parent = QFileInfo(root).absolutePath();
    if (DataPathSafety::overlaps(data, root) || DataPathSafety::overlaps(source, root) ||
        QFileInfo(parent).isSymLink() || QFileInfo(parent).isJunction() ||
        QFileInfo::exists(root) || !QDir().mkpath(stage))
        return fail(error, QStringLiteral("无法创建独立恢复暂存目录"));
    for (const QString& name : manifest.files)
        if (!BackupSnapshot::copyFile(QDir(source).filePath(name), QDir(stage).filePath(name), error)) return false;
    if (!BackupSnapshot::copyFile(QDir(source).filePath("backup_manifest.txt"),
                                 QDir(stage).filePath("backup_manifest.txt"), error) ||
        !BackupSnapshot::validate(stage, &manifest, error)) return false;

    QStringList originals;
    if (!listManaged(data, &originals, error)) return false;
    QJsonArray inventory;
    for (const auto& name : originals) {
        const auto target = QDir(root).filePath(QStringLiteral("before/") + name);
        if (!BackupSnapshot::copyFile(QDir(data).filePath(name), target, error)) return false;
        const auto hash = digest(target);
        if (hash.isEmpty()) return fail(error, QStringLiteral("无法校验恢复前文件：%1").arg(name));
        inventory.append(QJsonObject{{"path", name}, {"size", QString::number(QFileInfo(target).size())},
                                     {"sha256", QString::fromLatin1(hash)}});
    }
    if (!writeJson(QDir(root).filePath("before.json"), QJsonObject{{"version", 1}, {"files", inventory}}, error))
        return false;
    QJsonObject journal{{"version", 1}, {"id", id}, {"source", source}, {"phase", "applying"},
                        {"beforeSha256", QString::fromLatin1(digest(QDir(root).filePath("before.json")))},
                        {"stageSha256", QString::fromLatin1(digest(QDir(stage).filePath("backup_manifest.txt")))}};
    if (!writeJson(QDir(data).filePath(journalName), journal, error)) return false;
    auto abort = [&]() {
        const QString reason = error ? *error : QString();
        QString rollbackError;
        if (!rollback(data, root, journal, &rollbackError, fault))
            return fail(error, reason + QStringLiteral("\n回滚未完成：") + rollbackError +
                        QStringLiteral("\n未加载账户；下次启动将重试回滚。快照：") + root);
        return fail(error, reason + QStringLiteral("\n已恢复原数据；快照保留在：") + root);
    };
    const QSet<QString> wanted(manifest.files.begin(), manifest.files.end());
    for (const auto& name : originals) {
        if (!wanted.contains(name) && !archiveFile(data, name, root, error)) return abort();
    }
    int copied = 0;
    for (const auto& name : manifest.files) {
        if (!safeFile(data, name)) {
            fail(error, QStringLiteral("恢复目标路径不安全：%1").arg(name));
            return abort();
        }
        if (!BackupSnapshot::copyFile(QDir(stage).filePath(name),
                                                            QDir(data).filePath(name), error)) return abort();
        ++copied;
        if (fault && (copied >= fault->afterFiles ||
                      fault->point == Fault::Point::RollbackInterruption)) {
            if (fault->point == Fault::Point::ApplyInterruption)
                return fail(error, QStringLiteral("测试：恢复提交中断"));
            if (fault->point == Fault::Point::ApplyFailure || fault->point == Fault::Point::RollbackInterruption) {
                fail(error, QStringLiteral("测试：恢复写入失败"));
                return abort();
            }
        }
    }
    if (!verifyInstalled(data, stage, manifest.files, error)) return abort();
    journal.insert("phase", "committed");
    if (!writeJson(QDir(data).filePath(journalName), journal, error)) return abort();
    if (fault && fault->point == Fault::Point::AfterCommit)
        return fail(error, QStringLiteral("测试：提交后中断"));
    if (!finish(data, root, error)) return false;
    if (restoredFrom) *restoredFrom = source;
    return true;
}
