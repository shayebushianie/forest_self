#include "storage/BackupSnapshot.h"
#include "storage/DataPathSafety.h"
#include "storage/DatabaseManager.h"
#include "storage/StorageEnvelope.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {
const QString manifestName = QStringLiteral("backup_manifest.txt");
struct StateFormat { const char* name; quint32 magic; quint16 version; };
constexpr StateFormat formats[] = {
    {"users.dat", 0x46555331u, 2}, {"coins.dat", 0x46574C31u, 3},
    {"friend_reqs.dat", 0x46525131u, 1}, {"friends.dat", 0x46465231u, 1},
    {"challenges.dat", 0x46434831u, 1}, {"variants.dat", 0x46564131u, 2},
    {"forest_layout.dat", 0x46464C31u, 1}, {"tags.dat", 0x46544731u, 1},
    {"guardian.dat", 0x46474431u, 3}, {"focus_settlements.dat", 0x46535431u, 2},
    {"sessions.dat", 0x46534631u, 1}
};
bool fail(QString* error, const QString& detail)
{
    if (error) *error = detail;
    return false;
}
QString baseName(QString name)
{
    for (const auto& suffix : {QStringLiteral(".bak.previous"), QStringLiteral(".sqlite-migration.bak"),
                               QStringLiteral(".bak"), QStringLiteral(".previous")}) {
        if (name.endsWith(suffix)) return name.left(name.size() - suffix.size());
    }
    return name;
}
QByteArray digest(const QString& path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return {};
    return hash.result().toHex();
}
bool safeFile(const QString& root, const QString& relative)
{
    const QString path = QDir(root).filePath(relative);
    return !DataPathSafety::hasLinkedComponent(root, path) &&
           DataPathSafety::contains(root, path) && QFileInfo(path).isFile();
}
bool listFiles(const QString& directory, QStringList* files, bool managedOnly, QString* error)
{
    QDirIterator it(directory, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const auto path = it.next();
        const auto relative = QDir(directory).relativeFilePath(path);
        if (DataPathSafety::hasLinkedComponent(directory, path) || !DataPathSafety::contains(directory, path))
            return fail(error, QStringLiteral("备份目录包含不安全链接：%1").arg(relative));
        if (it.fileInfo().isDir()) continue;
        if (!it.fileInfo().isFile()) return fail(error, QStringLiteral("备份中包含非普通文件：%1").arg(relative));
        if ((!managedOnly || BackupSnapshot::isManagedPath(relative)) && relative != manifestName)
            files->append(relative);
    }
    files->sort();
    return true;
}
bool checkSqlite(const QString& source, const QString& destination, QString* error)
{
    const QString connection = QStringLiteral("forest_snapshot_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(source);
        db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (db.open()) {
            QSqlQuery query(db);
            ok = query.exec("PRAGMA quick_check") && query.next() && query.value(0).toString() == "ok";
            query.finish();
            ok = ok && query.exec("SELECT version FROM schema_version") && query.next();
            bool validVersion = false;
            const qlonglong version = ok ? query.value(0).toString().toLongLong(&validVersion) : 0;
            ok = ok && validVersion && version >= 1 && version <= DatabaseManager::schemaVersion() && !query.next();
            query.finish();
            QString schemaError;
            if (ok && !DatabaseManager::validateRecordSchema(db, static_cast<int>(version), &schemaError)) {
                fail(error, schemaError);
                ok = false;
            }
            if (ok && !destination.isEmpty()) {
                ok = query.exec("PRAGMA synchronous=FULL");
                query.finish();
                if (ok) {
                    ok = query.prepare("VACUUM INTO :destination");
                    query.bindValue(":destination", destination);
                    ok = ok && query.exec();
                }
                query.finish();
            }
            if (!ok && schemaError.isEmpty()) fail(error, QStringLiteral("SQLite 损坏、版本不受支持或快照失败：%1\n%2")
                                   .arg(source, query.lastError().text()));
            db.close();
        } else {
            fail(error, QStringLiteral("无法只读打开专注数据库：%1\n%2").arg(source, db.lastError().text()));
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return ok;
}
bool checkFormat(const QString& path, QString* error)
{
    const auto name = baseName(QFileInfo(path).fileName());
    if (name == "sessions.sqlite") return checkSqlite(path, {}, error);
    if (name.endsWith(".ini")) {
        QSettings settings(path, QSettings::IniFormat);
        bool validVersion = true;
        const int version = settings.value("schema/version", 1).toInt(&validVersion);
        if (settings.status() != QSettings::NoError || !validVersion || version != 1)
            return fail(error, QStringLiteral("设置文件损坏或版本不受支持：%1").arg(path));
        return true;
    }
    for (const auto& format : formats) {
        if (name != QLatin1String(format.name)) continue;
        const auto result = StorageEnvelope::inspect(path, format.magic);
        if (result.state == StorageEnvelope::ReadState::Valid && result.version >= 1 &&
            result.version <= format.version) return true;
        if (result.state == StorageEnvelope::ReadState::Legacy) {
            const qint64 size = QFileInfo(path).size();
            if ((name == "users.dat" || name == "sessions.dat") && size % 64 == 0) return true;
            if (name == "coins.dat" && (size == 4 || size == 64)) return true;
            if (name == "variants.dat" && size == 4) return true;
            return fail(error, QStringLiteral("旧状态无法只读验证，请先登录对应账户迁移后再备份：%1").arg(path));
        }
        return fail(error, QStringLiteral("状态文件损坏或版本不受支持：%1\n%2").arg(path, result.error));
    }
    return fail(error, QStringLiteral("不支持的备份文件：%1").arg(path));
}
}

bool BackupSnapshot::isManagedPath(const QString& relative)
{
    if (relative.isEmpty() || relative.contains('\\') || relative.contains(':') || relative.startsWith('/') ||
        QDir::cleanPath(relative) != relative) return false;
    const auto parts = relative.split('/');
    const auto name = baseName(parts.last());
    if (parts.size() == 1)
        return name == "users.dat" || name == "friend_reqs.dat" || name == "challenges.dat" ||
               name == "preferences.ini" || name == "ui_settings.ini";
    static const QRegularExpression userDirectory(QStringLiteral("^user_[0-9]+$"));
    if (parts.size() != 2 || !userDirectory.match(parts[0]).hasMatch()) return false;
    return name == "sessions.sqlite" || name == "sessions.dat" || name == "coins.dat" || name == "variants.dat" ||
           name == "forest_layout.dat" || name == "friends.dat" || name == "tags.dat" || name == "guardian.dat" ||
           name == "focus_settlements.dat" || name == "preferences.ini" || name == "ui_settings.ini";
}

bool BackupSnapshot::copyFile(const QString& source, const QString& target, QString* error)
{
    if (QFileInfo(source).isSymLink() || QFileInfo(source).isJunction() || !QFileInfo(source).isFile() ||
        QFileInfo(target).isSymLink() || QFileInfo(target).isJunction() ||
        !QDir().mkpath(QFileInfo(target).absolutePath()))
        return fail(error, QStringLiteral("无法安全复制文件：%1").arg(source));
    QFile input(source);
    QSaveFile output(target);
    output.setDirectWriteFallback(false);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("无法打开复制文件：%1").arg(source));
    while (!input.atEnd()) {
        const auto bytes = input.read(1024 * 1024);
        if (bytes.isEmpty() || output.write(bytes) != bytes.size())
            return fail(error, QStringLiteral("文件复制未完成：%1").arg(source));
    }
    if (input.error() != QFileDevice::NoError || !output.commit())
        return fail(error, QStringLiteral("无法提交复制文件：%1").arg(target));
    return true;
}

bool BackupSnapshot::create(const QString& source, const QString& target, QString* error)
{
    if (source.isEmpty() || target.isEmpty() || !QFileInfo(source).isDir() || QFileInfo::exists(target) ||
        DataPathSafety::overlaps(source, target) || DataPathSafety::hasLinkedComponent(source, source))
        return fail(error, QStringLiteral("备份来源或新目标目录不安全"));
    QStringList files;
    if (!listFiles(source, &files, true, error)) return false;
    if (files.isEmpty()) return fail(error, QStringLiteral("没有可备份的 Forest 数据文件"));
    if (!QDir().mkpath(target)) return fail(error, QStringLiteral("无法创建备份目录"));
    QJsonArray inventory;
    for (const auto& name : files) {
        const QString from = QDir(source).filePath(name);
        const QString to = QDir(target).filePath(name);
        if (!safeFile(source, name) || !checkFormat(from, error)) return false;
        if (baseName(QFileInfo(name).fileName()) == "sessions.sqlite") {
            if (!QDir().mkpath(QFileInfo(to).absolutePath()) || !checkSqlite(from, to, error)) return false;
        } else if (!copyFile(from, to, error)) return false;
        if (!checkFormat(to, error)) return false;
        const auto hash = digest(to);
        if (hash.isEmpty()) return fail(error, QStringLiteral("无法校验备份文件：%1").arg(name));
        inventory.append(QJsonObject{{"path", name}, {"size", QString::number(QFileInfo(to).size())},
                                     {"sha256", QString::fromLatin1(hash)}});
    }
    const auto bytes = QJsonDocument(QJsonObject{{"format", "ForestBackup"}, {"version", 2},
        {"created", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}, {"files", inventory}})
        .toJson(QJsonDocument::Compact);
    QSaveFile manifest(QDir(target).filePath(manifestName));
    if (!manifest.open(QIODevice::WriteOnly) || manifest.write(bytes) != bytes.size() || !manifest.commit())
        return fail(error, QStringLiteral("无法提交备份清单，备份尚未完成"));
    BackupManifest checked;
    return validate(target, &checked, error);
}

bool BackupSnapshot::validate(const QString& directory, BackupManifest* result, QString* error)
{
    if (result) result->files.clear();
    if (!QFileInfo(directory).isDir() || !safeFile(directory, manifestName))
        return fail(error, QStringLiteral("备份清单不存在或路径不安全"));
    QFile manifest(QDir(directory).filePath(manifestName));
    if (!manifest.open(QIODevice::ReadOnly) || manifest.size() > 16 * 1024 * 1024)
        return fail(error, QStringLiteral("无法读取备份清单"));
    const auto bytes = manifest.readAll();
    if (bytes.startsWith("ForestBackup\n") || bytes.startsWith("ForestBackup\r\n"))
        return fail(error, QStringLiteral("旧 v1 备份没有文件清单和校验，不能自动恢复；原备份已保留，请先人工核对。"));
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    const auto object = document.object();
    if (parse.error != QJsonParseError::NoError || !document.isObject() || object.value("format") != "ForestBackup" ||
        object.value("version").toDouble() != 2 || !object.value("files").isArray() ||
        !QDateTime::fromString(object.value("created").toString(), Qt::ISODate).isValid())
        return fail(error, QStringLiteral("备份清单损坏或来自不支持的版本"));
    QSet<QString> unique;
    QStringList expected;
    for (const auto& value : object.value("files").toArray()) {
        const auto item = value.toObject();
        const auto name = item.value("path").toString();
        const auto path = QDir(directory).filePath(name);
        if (!isManagedPath(name) || unique.contains(name) || !safeFile(directory, name) ||
            QString::number(QFileInfo(path).size()) != item.value("size").toString())
            return fail(error, QStringLiteral("备份文件缺失、内容损坏或路径无效：%1").arg(name));
        const auto hash = digest(path);
        if (hash.isEmpty() || hash != item.value("sha256").toString().toLatin1())
            return fail(error, QStringLiteral("备份文件缺失、内容损坏或路径无效：%1").arg(name));
        if (!checkFormat(path, error)) return false;
        unique.insert(name);
        expected.append(name);
    }
    expected.sort();
    QStringList actual;
    if (expected.isEmpty() || !listFiles(directory, &actual, false, error) || actual != expected)
        return fail(error, QStringLiteral("备份文件集合不完整或包含清单之外的文件"));
    if (result) result->files = expected;
    return true;
}
