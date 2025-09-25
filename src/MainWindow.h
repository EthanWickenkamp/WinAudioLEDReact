#pragma once
#include <QMainWindow>
#include <QThread>
#include <QString>
#include <QVector>
#include "UdpSrSender.h"

// ── Forward declarations (no heavy headers here) ──
class QPushButton;
class QLabel;
class QProgressBar;

// ADD: forward declare your custom widget (only include its header in MainWindow.cpp)
class BarsWidget;

// Workers
class AudioCapture;
class AudioProcessor;

// forward declare the advanced processor and visualizer
class MultiResolutionVisualizerWidget;
class AdvancedAudioProcessor;

class UdpSrSender;

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

  // Level meters (0..100%)
  QProgressBar* _meterL{};     // Left RMS meter
  QProgressBar* _meterR{};     // Right RMS meter

  // ADD: the widgets
  BarsWidget*   _bars{};
  MultiResolutionVisualizerWidget* _visualizer{};

  // ── Threads & workers ──
  QThread        _audioThread;
  QThread        _dspThread;
  QThread        _adspThread;
  AudioCapture*  _audio{};
  AudioProcessor*_dsp{};
  AdvancedAudioProcessor* _adsp{};
  
  bool _running{false};

  UdpSrSender* _srSender{nullptr};

  // ── Internal helpers ──
  void wireUp();          // connect signals/slots across threads
  void teardownThreads(); // graceful shutdown

private slots:
  // ── Slots (keep these signatures; they're connected in MainWindow.cpp) ──
  void onStart();
  void onStop();
  void onAudioStatus(const QString& msg);
  void onLevels(float lDb, float rDb);          // updates meters
};
