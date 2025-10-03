#include "SnapshotViewer.h"
#include <QHeaderView>
#include <QDateTime>

SnapshotViewer::SnapshotViewer(SnapshotManager* manager, QWidget* parent)
    : QWidget(parent), _manager(manager)
{
    setWindowTitle("Audio Frame Snapshots");
    setMinimumSize(800, 600);
    setupUI();
    
    connect(_manager, &SnapshotManager::snapshotsChanged, 
            this, &SnapshotViewer::onSnapshotsChanged);
    connect(_frameSlider, &QSlider::valueChanged, 
            this, &SnapshotViewer::onSliderChanged);
    connect(_bufferDurationSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            _manager, &SnapshotManager::setBufferDuration);
    connect(_clearButton, &QPushButton::clicked, 
            _manager, &SnapshotManager::clear);
    
    updateControls();
}

void SnapshotViewer::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    
    // Controls section
    auto* controlsGroup = new QGroupBox("Controls");
    auto* controlsLayout = new QHBoxLayout(controlsGroup);
    
    controlsLayout->addWidget(new QLabel("Buffer Duration:"));
    _bufferDurationSpin = new QSpinBox();
    _bufferDurationSpin->setRange(10, 120);
    _bufferDurationSpin->setValue(30);
    _bufferDurationSpin->setSuffix(" seconds");
    controlsLayout->addWidget(_bufferDurationSpin);
    
    _clearButton = new QPushButton("Clear Buffer");
    controlsLayout->addWidget(_clearButton);
    controlsLayout->addStretch();
    
    // Frame navigation
    auto* navGroup = new QGroupBox("Frame Navigation");
    auto* navLayout = new QVBoxLayout(navGroup);
    
    _frameSlider = new QSlider(Qt::Horizontal);
    navLayout->addWidget(_frameSlider);
    
    auto* infoLayout = new QHBoxLayout();
    _frameLabel = new QLabel("Frame: -");
    _timestampLabel = new QLabel("Time: -");
    _centroidLabel = new QLabel("Centroids: L=- R=-");
    infoLayout->addWidget(_frameLabel);
    infoLayout->addWidget(_timestampLabel);
    infoLayout->addWidget(_centroidLabel);
    infoLayout->addStretch();
    navLayout->addLayout(infoLayout);
    
    // Tables for bar values
    auto* tablesLayout = new QHBoxLayout();
    
    auto* leftGroup = new QGroupBox("Left Channel");
    auto* leftLayout = new QVBoxLayout(leftGroup);
    _leftTable = new QTableWidget(32, 2);
    _leftTable->setHorizontalHeaderLabels({"Bin", "Value"});
    _leftTable->horizontalHeader()->setStretchLastSection(true);
    _leftTable->setMaximumWidth(200);
    leftLayout->addWidget(_leftTable);
    
    auto* rightGroup = new QGroupBox("Right Channel");
    auto* rightLayout = new QVBoxLayout(rightGroup);
    _rightTable = new QTableWidget(32, 2);
    _rightTable->setHorizontalHeaderLabels({"Bin", "Value"});
    _rightTable->horizontalHeader()->setStretchLastSection(true);
    _rightTable->setMaximumWidth(200);
    rightLayout->addWidget(_rightTable);
    
    tablesLayout->addWidget(leftGroup);
    tablesLayout->addWidget(rightGroup);
    tablesLayout->addStretch();
    
    // Add all sections to main layout
    layout->addWidget(controlsGroup);
    layout->addWidget(navGroup);
    layout->addLayout(tablesLayout);
}

void SnapshotViewer::onSnapshotsChanged()
{
    updateControls();
    updateView();
}

void SnapshotViewer::updateControls()
{
    const auto& snapshots = _manager->getSnapshots();
    
    _frameSlider->setEnabled(!snapshots.isEmpty());
    if (!snapshots.isEmpty()) {
        _frameSlider->setRange(0, snapshots.size() - 1);
        _frameSlider->setValue(snapshots.size() - 1);  // default to latest
    }
}

void SnapshotViewer::onSliderChanged(int value)
{
    updateView();
}

void SnapshotViewer::updateView()
{
    const auto& snapshots = _manager->getSnapshots();
    if (snapshots.isEmpty() || _frameSlider->value() >= snapshots.size()) {
        _frameLabel->setText("Frame: -");
        _timestampLabel->setText("Time: -");
        _centroidLabel->setText("Centroids: L=- R=-");
        return;
    }
    
    const auto& snapshot = snapshots[_frameSlider->value()];
    displaySnapshot(snapshot);
}

void SnapshotViewer::displaySnapshot(const Snapshot& snapshot)
{
    _frameLabel->setText(QString("Frame: %1").arg(snapshot.frameNumber));
    _timestampLabel->setText(QString("Time: %1").arg(
        snapshot.timestamp.toString("hh:mm:ss.zzz")));
    
    QString centroidText = QString("Centroids: L=%1 R=%2")
        .arg(snapshot.leftCentroid >= 0 ? QString::number(snapshot.leftCentroid, 'f', 2) : "-")
        .arg(snapshot.rightCentroid >= 0 ? QString::number(snapshot.rightCentroid, 'f', 2) : "-");
    _centroidLabel->setText(centroidText);
    
    for (int i = 0; i < snapshot.leftBars.size() && i < 32; ++i) {
        _leftTable->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
        _leftTable->setItem(i, 1, new QTableWidgetItem(
            QString::number(snapshot.leftBars[i], 'f', 4)));
    }
    
    for (int i = 0; i < snapshot.rightBars.size() && i < 32; ++i) {
        _rightTable->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
        _rightTable->setItem(i, 1, new QTableWidgetItem(
            QString::number(snapshot.rightBars[i], 'f', 4)));
    }
}