#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QLabel>
#include <QTableWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QGroupBox>
#include "Snapshot.h"
#include "SnapshotManager.h"

class SnapshotViewer : public QWidget {
    Q_OBJECT
    
public:
    explicit SnapshotViewer(SnapshotManager* manager, QWidget* parent = nullptr);
    
public slots:
    void updateView();
    void onSliderChanged(int value);
    void onSnapshotsChanged();
    
private:
    SnapshotManager* _manager;
    QSlider* _frameSlider;
    QLabel* _frameLabel;
    QLabel* _timestampLabel;
    QLabel* _centroidLabel;
    QTableWidget* _leftTable;
    QTableWidget* _rightTable;
    QSpinBox* _bufferDurationSpin;
    QPushButton* _clearButton;
    
    void setupUI();
    void displaySnapshot(const Snapshot& snapshot);
    void updateControls();
};