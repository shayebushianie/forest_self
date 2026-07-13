#ifndef STORAGE_ENVELOPE_H
#define STORAGE_ENVELOPE_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace StorageEnvelope {

enum class ReadState {
    Missing,
    Valid,
    RecoveredFromBackup,
    Legacy,
    Corrupt
};

struct ReadResult {
    ReadState state = ReadState::Missing;
    quint16 version = 0;
    QByteArray payload;
    QString error;

    bool hasPayload() const {
        return state == ReadState::Valid || state == ReadState::RecoveredFromBackup;
    }
};

ReadResult read(const QString& path, quint32 expectedMagic);
bool write(const QString& path, quint32 magic, quint16 version,
           const QByteArray& payload, QString* error = nullptr);

// Test-only failure injection for persistence recovery coverage. The next write
// to a registered path fails before modifying the target file.
void failNextWriteForTesting(const QString& path);
void clearWriteFailuresForTesting();

} // namespace StorageEnvelope

#endif // STORAGE_ENVELOPE_H
