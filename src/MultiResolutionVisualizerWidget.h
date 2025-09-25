// MultiResolutionVisualizerWidget.h
#pragma once
#include <QWidget>
#include <QPainter>
#include <QTimer>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QProgressBar>
#include <QFrame>
#include <deque>
#include "AdvancedAudioProcessor.h" // For MultiResolutionData struct

class MultiResolutionVisualizerWidget : public QWidget {
  Q_OBJECT
public:
  explicit MultiResolutionVisualizerWidget(QWidget* parent = nullptr);

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

public slots:
  void onMultiResolutionData(const MultiResolutionData& data);
  void onBassAnalysis(const QVector<float>& bands);
  void onHarmonicAnalysis(const QVector<float>& bands);  
  void onPercussiveAnalysis(const QVector<float>& bands);
  void onChromagram(const QVector<float>& chroma);
  void onSpectralFeatures(float centroid, float rolloff, float zcr);
  void onBeatTracking(float phase, float period, float confidence);
  void onOnsetDetection(const QVector<float>& strength, bool isOnset);

private:
  // Data storage
  MultiResolutionData _currentData;
  QVector<float> _bassLevels;         // 16 bass bands
  QVector<float> _harmonicLevels;     // 32 harmonic bands
  QVector<float> _percLevels;         // 8 percussive bands
  QVector<float> _chromaLevels;       // 12 pitch classes
  
  // Historical data for waveform displays
  std::deque<float> _spectralCentroidHistory;   // Brightness over time
  std::deque<float> _beatPhaseHistory;          // Beat tracking over time
  std::deque<float> _onsetHistory;              // Onset strength over time
  std::deque<QVector<float>> _bassHistory;      // Bass evolution over time
  static constexpr int MAX_HISTORY = 200;      // ~10 seconds at 20fps
  
  // Visual parameters
  QRect _bassRect, _harmonicRect, _percRect, _chromaRect;
  QRect _spectralRect, _beatRect, _onsetRect, _evolutionRect;
  
  // UI Elements
  QLabel* _spectralCentroidLabel;
  QLabel* _spectralRolloffLabel;
  QLabel* _zeroCrossingLabel;
  QLabel* _beatBPMLabel;
  QLabel* _beatConfidenceLabel;
  QLabel* _harmonicPercRatioLabel;
  QProgressBar* _beatPhaseProgress;
  QFrame* _onsetIndicator;
  
  // Visual state
  bool _isOnset = false;
  int _onsetFlashTimer = 0;
  QColor _onsetColor = QColor(255, 0, 0, 200);
  
  // Drawing methods
  void drawBassSpectrum(QPainter& painter, const QRect& rect);
  void drawHarmonicSpectrum(QPainter& painter, const QRect& rect);
  void drawPercussiveSpectrum(QPainter& painter, const QRect& rect);
  void drawChromagram(QPainter& painter, const QRect& rect);
  void drawSpectralEvolution(QPainter& painter, const QRect& rect);
  void drawBeatTracking(QPainter& painter, const QRect& rect);
  void drawOnsetDetection(QPainter& painter, const QRect& rect);
  void drawBassEvolution(QPainter& painter, const QRect& rect);
  
  void setupLayout();
  void updateLabels();
  QColor getFrequencyColor(int band, int totalBands);
  QColor getPitchColor(int pitchClass);
  
  // Utility
  float clampAndSmooth(float value, float& smoothed, float smoothing = 0.1f);
};

