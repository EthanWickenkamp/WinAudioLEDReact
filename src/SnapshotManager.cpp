#include "SnapshotManager.h"

SnapshotManager::SnapshotManager(QObject* parent) : QObject(parent)
{
    _snapshots.reserve(_bufferSeconds * ASSUMED_FPS);
}

void SnapshotManager::setBufferDuration(int seconds)
{
    _bufferSeconds = qBound(10, seconds, 120);  // 10 sec to 2 min
    trimBuffer();
    emit snapshotsChanged();
}

void SnapshotManager::addSnapshot(const Snapshot& snapshot)
{
    _snapshots.append(snapshot);
    trimBuffer();
    emit snapshotsChanged();
}

void SnapshotManager::trimBuffer()
{
    if (_snapshots.isEmpty()) return;
    
    // Calculate how many frames to keep based on time
    const QDateTime cutoffTime = QDateTime::currentDateTime().addSecs(-_bufferSeconds);
    
    // Remove old snapshots
    while (!_snapshots.isEmpty() && _snapshots.first().timestamp < cutoffTime) {
        _snapshots.removeFirst();
    }
}

void SnapshotManager::clear()
{
    _snapshots.clear();
    emit snapshotsChanged();
}
