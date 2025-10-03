#pragma once
#include <QObject>
#include <QVector>
#include <QTimer>
#include "Snapshot.h"

class SnapshotManager : public QObject {
    Q_OBJECT
    
public:
    explicit SnapshotManager(QObject* parent = nullptr);
    
    void setBufferDuration(int seconds);  // how many seconds to keep
    int getBufferDuration() const { return _bufferSeconds; }
    
    const QVector<Snapshot>& getSnapshots() const { return _snapshots; }
    int getCurrentIndex() const { return _snapshots.size() - 1; }
    
    bool isEmpty() const { return _snapshots.isEmpty(); }
    
public slots:
    void addSnapshot(const Snapshot& snapshot);
    void clear();
    
signals:
    void snapshotsChanged();  // emitted when buffer changes
    
private:
    QVector<Snapshot> _snapshots;
    int _bufferSeconds = 30;  // default 30 seconds
    static constexpr int ASSUMED_FPS = 60;  // estimate for buffer size
    
    void trimBuffer();
};