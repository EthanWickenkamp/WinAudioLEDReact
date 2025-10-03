#pragma once
#include <QMainWindow>
#include <QThread>
#include <QString>
#include <QVector>
#include <QLineEdit>


// ── Forward declarations (no heavy headers here) ──
class QPushButton;
class QLabel;
class QProgressBar;
class QLineEdit;
class QPushButton;

// ADD: forward declare your custom widget (only include its header in MainWindow.cpp)
class BarsWidget;

// Workers
class AudioCapture;
class AudioProcessor;

//Snapshot
class Snapshot;
class SnapshotManager;
class SnapshotViewer;

class UdpSrSender;

// forward declare the advanced processor and visualizer
//class MultiResolutionVisualizerWidget;
//class AdvancedAudioProcessor;



class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow();

private:
  // ── UI members (ADD: meters + bars widget here) ──
  QPushButton*  _btnStart{};
  QPushButton*  _btnStop{};
  QLabel*       _status{};
  // new for variable bins
  QLineEdit*  _binsEdit{};
  QPushButton*_binsApply{};

  // Level meters (0..100%)
  QProgressBar* _meterL{};     // Left RMS meter
  QProgressBar* _meterR{};     // Right RMS meter

  // ADD: the widgets
  BarsWidget*   _bars{};
  //MultiResolutionVisualizerWidget* _visualizer{};

  // Snapshot manager + viewer
  SnapshotManager* _snapshotManager = nullptr;
  SnapshotViewer* _snapshotViewer = nullptr;
  QPushButton* _snapshotButton = nullptr;

  // ── Threads & workers ──
  QThread        _audioThread;
  QThread        _dspThread;
  //QThread        _adspThread;
  AudioCapture*  _audio{};
  AudioProcessor*_dsp{};
  //AdvancedAudioProcessor* _adsp{};
  
  bool _running{false};

  UdpSrSender* _srSender{nullptr};

  // ── Internal helpers ──
  void wireUp();          // connect signals/slots across threads
  void teardownThreads(); // graceful shutdown
  void openSnapshotViewer(); // show the snapshot viewer window

private slots:
  // ── Slots (keep these signatures; they're connected in MainWindow.cpp) ──
  void onStart();
  void onStop();
  void onAudioStatus(const QString& msg);
  void onLevels(float lDb, float rDb);          // updates meters
  void onApplyBins();                          // apply new # of bins from UI
};
