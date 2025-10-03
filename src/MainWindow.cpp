#include "MainWindow.h"
#include "AudioCapture.h"
#include "AudioProcessor.h"
#include "AdvancedAudioProcessor.h"
#include "MultiResolutionVisualizerWidget.h"
#include "BarsWidget.h"
#include "UdpSrSender.h"
#include "SnapshotManager.h"
#include "SnapshotViewer.h"

#include <QtNetwork/QHostAddress>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QWidget>

//local helper: dB to 0..100%
namespace {
// Map dBFS (-60..0 dB) -> 0..100% for the meters
constexpr int dbToPct(float dB) noexcept{
  if (dB <= -60.0f) return 0;
  if (dB >=   0.0f) return 100;
  return int((dB + 60.0f) / 60.0f * 100.0f + 0.5f);
}
}

// constructor sets up UI, threads, workers, connections, inherit QMainWindow
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  // --- UI ---
  // central widget with vertical layout
  auto* central = new QWidget(this);
  auto* layout  = new QVBoxLayout(central);

  // Buttons and status under window
  _btnStart = new QPushButton("Start", this);
  _btnStop  = new QPushButton("Stop", this);
  _status   = new QLabel("Idle", this);
    // --- Bins control row ---
  auto* binsRow = new QHBoxLayout();
  _binsEdit  = new QLineEdit(this);
  _binsApply = new QPushButton("Apply bins", this);

  _binsEdit->setPlaceholderText("16, 32, or 64");
  _binsEdit->setFixedWidth(100);

  // (Optional) pre-fill current
  _binsEdit->setText(QString::number(32));

  binsRow->addWidget(new QLabel("Bands:", this));
  binsRow->addWidget(_binsEdit);
  binsRow->addWidget(_binsApply);
  binsRow->addStretch();

  // add to main layout (e.g., after meters)
  layout->addLayout(binsRow);

  // meters under that
  _meterL = new QProgressBar(this);
  _meterR = new QProgressBar(this);
  _meterL->setRange(0, 100);
  _meterR->setRange(0, 100);
  _meterL->setFormat("L: %p%");
  _meterR->setFormat("R: %p%");

  // Widgets
  _bars = new BarsWidget(this);
  //_visualizer = new MultiResolutionVisualizerWidget(this);

  //snapshot manager + button
  _snapshotManager = new SnapshotManager(this);
  _snapshotButton = new QPushButton("View Snapshots", this);

  // add to layout top down
  layout->addWidget(_btnStart);
  layout->addWidget(_btnStop);
  layout->addWidget(_status);
  layout->addWidget(_meterL);
  layout->addWidget(_meterR);
  layout->addWidget(_bars);
  //layout->addWidget(_visualizer);

  setCentralWidget(central);
  setWindowTitle("WLED Audio Processor Beta");

  // --- Workers (created on UI thread, then moved to their threads) ---
  _audio = new AudioCapture;
  _dsp   = new AudioProcessor;
  //_adsp = new AdvancedAudioProcessor;
  _audio->moveToThread(&_audioThread);
  _dsp->moveToThread(&_dspThread);
  //_adsp->moveToThread(&_adspThread);

  _srSender = new UdpSrSender(this);
  // Unicast to a specific device:
  _srSender->setTarget(QHostAddress("192.168.50.165"), 11988);
  // If you want multicast instead, do: setTarget(QHostAddress("239.0.0.1"), 11988);

  wireUp();


}

MainWindow::~MainWindow() {
  teardownThreads();
}

void MainWindow::wireUp() {
    // Button actions
  connect(_btnStart, &QPushButton::clicked, this, &MainWindow::onStart);
  connect(_btnStop,  &QPushButton::clicked, this, &MainWindow::onStop);
  connect(_snapshotButton, &QPushButton::clicked, this, &MainWindow::openSnapshotViewer);
  connect(_binsApply, &QPushButton::clicked, this, &MainWindow::onApplyBins);
  // Start workers when threads start
  connect(&_audioThread, &QThread::started, _audio, &AudioCapture::start);
  connect(&_dspThread,   &QThread::started, _dsp,   &AudioProcessor::start);
  //connect(&_adspThread,  &QThread::started, _adsp,  &AdvancedAudioProcessor::start);

  // When workers signal 'stopped', quit their threads
  connect(_audio, &AudioCapture::stopped, &_audioThread, &QThread::quit);
  connect(_dsp,   &AudioProcessor::stopped, &_dspThread, &QThread::quit);
  //connect(_adsp,  &AdvancedAudioProcessor::stopped, &_adspThread, &QThread::quit);

  // Pipeline: audio -> dsp (queued across threads safely)
  connect(_audio, &AudioCapture::deviceSampleRateChanged, _dsp,   &AudioProcessor::setSampleRate, Qt::QueuedConnection);
  connect(_audio, &AudioCapture::framesReady, _dsp,   &AudioProcessor::onFrames, Qt::QueuedConnection);
  //connect(_audio, &AudioCapture::framesReady, _adsp,  &AdvancedAudioProcessor::onFrames, Qt::QueuedConnection);

  // UI feedback
  connect(_audio, &AudioCapture::status,   this, &MainWindow::onAudioStatus);
  connect(_dsp,   &AudioProcessor::levelsReady, this, &MainWindow::onLevels);
  connect(_dsp,  &AudioProcessor::binsReadyRaw, _bars, &BarsWidget::setBinsRawStereo, Qt::QueuedConnection); //crosses threads?
  // Send UDP whenever bins are ready (sender throttles to 50 FPS).
  connect(_dsp, &AudioProcessor::binsReady, _srSender, &UdpSrSender::sendFromBins);
  //connect(_adsp, &AdvancedAudioProcessor::multiResolutionAnalysisReady, _visualizer, &MultiResolutionVisualizerWidget::onMultiResolutionData, Qt::QueuedConnection);
  connect(_bars, &BarsWidget::snapshotReady, _snapshotManager, &SnapshotManager::addSnapshot);

}

void MainWindow::openSnapshotViewer()
{
    if (!_snapshotViewer) {
        _snapshotViewer = new SnapshotViewer(_snapshotManager, this);
        
        // Make it a separate window that can be closed
        _snapshotViewer->setWindowFlags(Qt::Window);
        _snapshotViewer->setAttribute(Qt::WA_DeleteOnClose);
        
        // Reset pointer when window closes
        connect(_snapshotViewer, &QObject::destroyed, [this]() {
            _snapshotViewer = nullptr;
        });
    }
    
    _snapshotViewer->show();
    _snapshotViewer->raise();
    _snapshotViewer->activateWindow();
}

void MainWindow::onApplyBins() {
  bool ok = false;
  const int n = _binsEdit->text().trimmed().toInt(&ok);
  if (!ok || (n != 16 && n != 32 && n != 64)) {
    _status->setText("Bands must be 16, 32, or 64");
    return;
  }

  // Make sure setNumBands is a slot (or Q_INVOKABLE)
  QMetaObject::invokeMethod(_dsp, "setNumBands",
                            Qt::QueuedConnection,
                            Q_ARG(int, n));

  _status->setText(QString("Bands set to %1").arg(n));
}

void MainWindow::teardownThreads() {
  if (_running) {
    _audio->requestStop();
    _dsp->requestStop();
    //_adsp->requestStop();
  }
  _audioThread.quit(); _audioThread.wait();
  _dspThread.quit();   _dspThread.wait();
  //_adspThread.quit();  _adspThread.wait();

  // Delete workers on UI thread
  if (_audio) { _audio->deleteLater(); _audio = nullptr; }
  if (_dsp)   { _dsp->deleteLater();   _dsp   = nullptr; }
  //if (_adsp)  { _adsp->deleteLater();  _adsp  = nullptr; }
}

// --- Slots ---

void MainWindow::onStart() {
  if (_running) return;
  _running = true;
  _status->setText("Starting…");
  _audioThread.start();
  _dspThread.start();
  //_adspThread.start();
}

void MainWindow::onStop() {
  if (!_running) return;
  _running = false;
  _status->setText("Stopping…");
  _audioThread.quit();
  _dspThread.quit();
  _audio->requestStop();
  _dsp->requestStop();
  //_adsp->requestStop();
}

void MainWindow::onAudioStatus(const QString& msg) {
  _status->setText(msg);
}

void MainWindow::onLevels(float lDb, float rDb) {
  _meterL->setValue(dbToPct(lDb));
  _meterR->setValue(dbToPct(rDb));
}
