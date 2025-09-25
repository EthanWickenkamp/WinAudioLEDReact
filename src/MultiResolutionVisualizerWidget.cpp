// MultiResolutionVisualizerWidget.cpp
#include "MultiResolutionVisualizerWidget.h"
#include <QPaintEvent>
#include <QApplication>
#include <algorithm>
#include <cmath>

MultiResolutionVisualizerWidget::MultiResolutionVisualizerWidget(QWidget* parent) 
  : QWidget(parent) {
  
  setMinimumSize(1200, 800);
  setStyleSheet("background-color: #1a1a1a; color: #ffffff;");
  
  // Initialize data vectors
  _bassLevels.fill(0.0f, 16);
  _harmonicLevels.fill(0.0f, 32);
  _percLevels.fill(0.0f, 8);
  _chromaLevels.fill(0.0f, 12);
  
  setupLayout();
  
  // Update timer for smooth animation
  QTimer* updateTimer = new QTimer(this);
  connect(updateTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
  updateTimer->start(50); // 20fps
}

void MultiResolutionVisualizerWidget::setupLayout() {
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  
  // Top row: Real-time info displays
  QHBoxLayout* infoLayout = new QHBoxLayout();
  
  _spectralCentroidLabel = new QLabel("Brightness: 0 Hz");
  _spectralRolloffLabel = new QLabel("Rolloff: 0 Hz"); 
  _zeroCrossingLabel = new QLabel("Noisiness: 0.0");
  _beatBPMLabel = new QLabel("BPM: 0");
  _beatConfidenceLabel = new QLabel("Beat Conf: 0%");
  _harmonicPercRatioLabel = new QLabel("H/P Ratio: 0.0");
  
  _beatPhaseProgress = new QProgressBar();
  _beatPhaseProgress->setRange(0, 100);
  _beatPhaseProgress->setTextVisible(false);
  _beatPhaseProgress->setMaximumHeight(20);
  
  _onsetIndicator = new QFrame();
  _onsetIndicator->setFixedSize(20, 20);
  _onsetIndicator->setStyleSheet("background-color: #333; border-radius: 10px;");
  
  infoLayout->addWidget(_spectralCentroidLabel);
  infoLayout->addWidget(_spectralRolloffLabel);
  infoLayout->addWidget(_zeroCrossingLabel);
  infoLayout->addWidget(_beatBPMLabel);
  infoLayout->addWidget(_beatConfidenceLabel);
  infoLayout->addWidget(_harmonicPercRatioLabel);
  infoLayout->addWidget(new QLabel("Beat:"));
  infoLayout->addWidget(_beatPhaseProgress);
  infoLayout->addWidget(new QLabel("Onset:"));
  infoLayout->addWidget(_onsetIndicator);
  
  mainLayout->addLayout(infoLayout);
  
  // Main visualization area (will be drawn in paintEvent)
  mainLayout->addStretch();
}

void MultiResolutionVisualizerWidget::onMultiResolutionData(const MultiResolutionData& data) {
  _currentData = data;
  
  // Update individual vectors
  _bassLevels = data.bass;
  _harmonicLevels = data.harmonic;
  _percLevels = data.percussive; 
  _chromaLevels = data.chromagram;
  
  // Update onset state
  if (data.isOnset) {
    _isOnset = true;
    _onsetFlashTimer = 10; // Flash for 10 frames
  }
  if (_onsetFlashTimer > 0) _onsetFlashTimer--;
  
  // Update historical data
  _spectralCentroidHistory.push_back(data.spectralCentroid);
  _beatPhaseHistory.push_back(data.beatPhase);
  _onsetHistory.push_back(data.onsetStrength.isEmpty() ? 0.0f : data.onsetStrength[0]);
  _bassHistory.push_back(data.bass);
  
  // Limit history size
  while (_spectralCentroidHistory.size() > MAX_HISTORY) _spectralCentroidHistory.pop_front();
  while (_beatPhaseHistory.size() > MAX_HISTORY) _beatPhaseHistory.pop_front();
  while (_onsetHistory.size() > MAX_HISTORY) _onsetHistory.pop_front();
  while (_bassHistory.size() > MAX_HISTORY) _bassHistory.pop_front();
  
  updateLabels();
}

void MultiResolutionVisualizerWidget::updateLabels() {
  _spectralCentroidLabel->setText(QString("Brightness: %1 Hz").arg(int(_currentData.spectralCentroid)));
  _spectralRolloffLabel->setText(QString("Rolloff: %1 Hz").arg(int(_currentData.spectralRolloff)));
  _zeroCrossingLabel->setText(QString("Noisiness: %1").arg(_currentData.zeroCrossingRate, 0, 'f', 2));
  
  float bpm = (_currentData.beatPeriod > 0) ? (60.0f * 20.0f / _currentData.beatPeriod) : 0; // Assuming 20fps
  _beatBPMLabel->setText(QString("BPM: %1").arg(int(bpm)));
  _beatConfidenceLabel->setText(QString("Beat Conf: %1%").arg(int(_currentData.beatConfidence * 100)));
  _harmonicPercRatioLabel->setText(QString("H/P Ratio: %1").arg(_currentData.harmonicPercussiveRatio, 0, 'f', 1));
  
  _beatPhaseProgress->setValue(int(_currentData.beatPhase * 100 / std::max(1.0f, _currentData.beatPeriod)));
  
  // Update onset indicator
  if (_onsetFlashTimer > 0) {
    _onsetIndicator->setStyleSheet("background-color: #ff0000; border-radius: 10px;");
  } else {
    _onsetIndicator->setStyleSheet("background-color: #333; border-radius: 10px;");
  }
}

void MultiResolutionVisualizerWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  
  // Calculate visualization rectangles based on widget size
  int width = this->width();
  int height = this->height() - 100; // Reserve space for info widgets
  int startY = 80; // Below info widgets
  
  // Layout: 2x4 grid
  int cellW = width / 4;
  int cellH = height / 2;
  
  _bassRect = QRect(0, startY, cellW, cellH);
  _harmonicRect = QRect(cellW, startY, cellW, cellH);
  _percRect = QRect(cellW * 2, startY, cellW, cellH);
  _chromaRect = QRect(cellW * 3, startY, cellW, cellH);
  
  _evolutionRect = QRect(0, startY + cellH, cellW, cellH);
  _spectralRect = QRect(cellW, startY + cellH, cellW, cellH);
  _beatRect = QRect(cellW * 2, startY + cellH, cellW, cellH);
  _onsetRect = QRect(cellW * 3, startY + cellH, cellW, cellH);
}

void MultiResolutionVisualizerWidget::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  
  // Draw all visualization components
  drawBassSpectrum(painter, _bassRect);
  drawHarmonicSpectrum(painter, _harmonicRect);
  drawPercussiveSpectrum(painter, _percRect);
  drawChromagram(painter, _chromaRect);
  drawBassEvolution(painter, _evolutionRect);
  drawSpectralEvolution(painter, _spectralRect);
  drawBeatTracking(painter, _beatRect);
  drawOnsetDetection(painter, _onsetRect);
}

void MultiResolutionVisualizerWidget::drawBassSpectrum(QPainter& painter, const QRect& rect) {
  painter.fillRect(rect, QColor(40, 40, 40));
  painter.setPen(Qt::white);
  painter.drawText(rect.topLeft() + QPoint(5, 15), "Bass Spectrum (20-400Hz)");
  
  if (_bassLevels.isEmpty()) return;
  
  int barWidth = rect.width() / _bassLevels.size();
  int maxHeight = rect.height() - 30;
  
  for (int i = 0; i < _bassLevels.size(); ++i) {
    float level = std::clamp(_bassLevels[i] * 0.1f, 0.0f, 1.0f); // Scale down
    int barHeight = int(level * maxHeight);
    
    QRect barRect(rect.left() + i * barWidth, rect.bottom() - barHeight, 
                  barWidth - 1, barHeight);
    
    // Color based on frequency (red=low, yellow=high)
    QColor color = getFrequencyColor(i, _bassLevels.size());
    painter.fillRect(barRect, color);
  }
  
  // Frequency labels
  painter.setPen(Qt::lightGray);
  painter.drawText(rect.bottomLeft() + QPoint(5, -5), "20Hz");
  painter.drawText(rect.bottomRight() + QPoint(-40, -5), "400Hz");
}

void MultiResolutionVisualizerWidget::drawHarmonicSpectrum(QPainter& painter, const QRect& rect) {
  painter.fillRect(rect, QColor(40, 40, 40));
  painter.setPen(Qt::white);
  painter.drawText(rect.topLeft() + QPoint(5, 15), "Harmonic Spectrum (80Hz-18kHz)");
  
  if (_harmonicLevels.isEmpty()) return;
  
  int barWidth = rect.width() / _harmonicLevels.size();
  int maxHeight = rect.height() - 30;
  
  for (int i = 0; i < _harmonicLevels.size(); ++i) {
    float level = std::clamp(_harmonicLevels[i] * 0.05f, 0.0f, 1.0f); // Scale down
    int barHeight = int(level * maxHeight);
    
    QRect barRect(rect.left() + i * barWidth, rect.bottom() - barHeight, 
                  barWidth - 1, barHeight);
    
    // Rainbow spectrum colors
    QColor color = getFrequencyColor(i, _harmonicLevels.size());
    painter.fillRect(barRect, color);
  }
  
  painter.setPen(Qt::lightGray);
  painter.drawText(rect.bottomLeft() + QPoint(5, -5), "80Hz");
  painter.drawText(rect.bottomRight() + QPoint(-50, -5), "18kHz");
}

void MultiResolutionVisualizerWidget::drawPercussiveSpectrum(QPainter& painter, const QRect& rect) {
  painter.fillRect(rect, QColor(40, 40, 40));
  painter.setPen(Qt::white);
  painter.drawText(rect.topLeft() + QPoint(5, 15), "Percussive Bands");
  
  if (_percLevels.isEmpty()) return;
  
  int barWidth = rect.width() / _percLevels.size();
  int maxHeight = rect.height() - 30;
  
  for (int i = 0; i < _percLevels.size(); ++i) {
    float level = std::clamp(_percLevels[i] * 0.02f, 0.0f, 1.0f); // Scale down
    int barHeight = int(level * maxHeight);
    
    QRect barRect(rect.left() + i * barWidth, rect.bottom() - barHeight, 
                  barWidth - 2, barHeight);
    
    // Percussive colors (bright, punchy)
    QColor color = QColor(255, 100 + i * 15, 50, 200);
    painter.fillRect(barRect, color);
  }
}

void MultiResolutionVisualizerWidget::drawChromagram(QPainter& painter, const QRect& rect) {
  painter.fillRect(rect, QColor(40, 40, 40));
  painter.setPen(Qt::white);
  painter.drawText(rect.topLeft() + QPoint(5, 15), "Chromagram (Pitch Classes)");
  
  if (_chromaLevels.isEmpty()) return;
  
  static const QStringList noteNames = {"C", "C#", "D", "D#", "E", "F", 
                                        "F#", "G", "G#", "A", "A#", "B"};
  
  int barWidth = rect.width() / _chromaLevels.size();
  int maxHeight = rect.height() - 50;
  
  for (int i = 0; i < _chromaLevels.size(); ++i) {
    float level = std::clamp(_chromaLevels[i] * 0.1f, 0.0f, 1.0f);
    int barHeight = int(level * maxHeight);
    
    QRect barRect(rect.left() + i * barWidth, rect.bottom() - barHeight - 20, 
                  barWidth - 1, barHeight);
    
    // Pitch class colors (musical color wheel)
    QColor color = getPitchColor(i);
    painter.fillRect(barRect, color);
    
    // Note labels
    painter.setPen(Qt::white);
    painter.drawText(rect.left() + i * barWidth + 5, rect.bottom() - 5, noteNames[i]);
  }
}

void MultiResolutionVisualizerWidget::drawBassEvolution(QPainter& painter, const QRect& rect) {
  painter.fillRect(rect, QColor(40, 40, 40));
  painter.setPen(Qt::white);
  painter.drawText(rect.topLeft() + QPoint(5, 15), "Bass Evolution Over Time");
  
  if (_bassHistory.empty()) return;
  
  int width = rect.width();
  int height = rect.height() - 30;
  
  // Draw bass evolution as a spectrogram
  for (int t = 0; t < std::min(width, (int)_bassHistory.size()); ++t) {
    const QVector<float>& bassFrame = _bassHistory[_bassHistory.size() - width + t];
    
    const int bands = std::min(16, static_cast<int>(bassFrame.size()));
    for (int b = 0; b < bands; ++b) {
      float level = std::clamp(bassFrame[b] * 0.1f, 0.0f, 1.0f);
      int bandHeight = height / 16;
      
      QColor color = getFrequencyColor(b, 16);
      color.setAlpha(int(level * 255));
      
      QRect pixelRect(rect.left() + t, rect.top() + 20 + b * bandHeight, 
                      1, bandHeight);
      painter.fillRect(pixelRect, color);
    }
  }
}

void MultiResolutionVisualizerWidget::drawSpectralEvolution(QPainter& painter, const QRect& rect) {
  painter.fillRect(rect, QColor(40, 40, 40));
  painter.setPen(Qt::white);
  painter.drawText(rect.topLeft() + QPoint(5, 15), "Spectral Centroid");
  
  if (_spectralCentroidHistory.size() < 2) return;
  
  painter.setPen(QPen(QColor(100, 200, 255), 2));
  
  int width = rect.width();
  int height = rect.height() - 30;
  
  // Find min/max for scaling
  float minFreq = *std::min_element(_spectralCentroidHistory.begin(), _spectralCentroidHistory.end());
  float maxFreq = *std::max_element(_spectralCentroidHistory.begin(), _spectralCentroidHistory.end());
  if (maxFreq <= minFreq) maxFreq = minFreq + 1;
  
  // Draw line graph
  for (int i = 1; i < std::min(width, (int)_spectralCentroidHistory.size()); ++i) {
    float freq1 = _spectralCentroidHistory[_spectralCentroidHistory.size() - width + i - 1];
    float freq2 = _spectralCentroidHistory[_spectralCentroidHistory.size() - width + i];
    
    int y1 = rect.bottom() - int((freq1 - minFreq) / (maxFreq - minFreq) * height);
    int y2 = rect.bottom() - int((freq2 - minFreq) / (maxFreq - minFreq) * height);
    
    painter.drawLine(rect.left() + i - 1, y1, rect.left() + i, y2);
  }
}

void MultiResolutionVisualizerWidget::drawBeatTracking(QPainter& painter, const QRect& rect) {
  painter.fillRect(rect, QColor(40, 40, 40));
  painter.setPen(Qt::white);
  painter.drawText(rect.topLeft() + QPoint(5, 15), "Beat Tracking");
  
  // Draw beat phase as a circular indicator
  QPoint center(rect.center());
  int radius = std::min(rect.width(), rect.height()) / 3;
  
  // Background circle
  painter.setPen(QPen(Qt::gray, 2));
  painter.drawEllipse(center, radius, radius);
  
  // Beat phase indicator
  float angle = _currentData.beatPhase * 2 * M_PI / _currentData.beatPeriod;
  QPoint beatPos(center.x() + radius * cos(angle - M_PI/2),
                 center.y() + radius * sin(angle - M_PI/2));
  
  // Color based on confidence
  int confidence = int(_currentData.beatConfidence * 255);
  QColor beatColor(255, confidence, confidence/2);
  painter.setPen(QPen(beatColor, 4));
  painter.drawLine(center, beatPos);
  
  // Draw confidence as fill
  painter.setBrush(QColor(beatColor.red(), beatColor.green(), beatColor.blue(), 50));
  painter.drawEllipse(QPointF(center),
                    radius * _currentData.beatConfidence,
                    radius * _currentData.beatConfidence);
}

void MultiResolutionVisualizerWidget::drawOnsetDetection(QPainter& painter, const QRect& rect) {
  painter.fillRect(rect, QColor(40, 40, 40));
  painter.setPen(Qt::white);
  painter.drawText(rect.topLeft() + QPoint(5, 15), "Onset Detection");
  
  if (_onsetHistory.size() < 2) return;
  
  painter.setPen(QPen(QColor(255, 100, 100), 2));
  
  int width = rect.width();
  int height = rect.height() - 30;
  
  // Draw onset strength over time
  for (int i = 1; i < std::min(width, (int)_onsetHistory.size()); ++i) {
    float onset1 = _onsetHistory[_onsetHistory.size() - width + i - 1];
    float onset2 = _onsetHistory[_onsetHistory.size() - width + i];
    
    int y1 = rect.bottom() - int(std::clamp(onset1 * 0.1f, 0.0f, 1.0f) * height);
    int y2 = rect.bottom() - int(std::clamp(onset2 * 0.1f, 0.0f, 1.0f) * height);
    
    painter.drawLine(rect.left() + i - 1, y1, rect.left() + i, y2);
  }
  
  // Flash effect for current onset
  if (_onsetFlashTimer > 0) {
    painter.fillRect(rect, QColor(255, 0, 0, 50));
  }
}

QColor MultiResolutionVisualizerWidget::getFrequencyColor(int band, int totalBands) {
  // Rainbow spectrum from red (low) to violet (high)
  float hue = (360.0f * band) / totalBands;
  return QColor::fromHsv(int(hue), 200, 255);
}

QColor MultiResolutionVisualizerWidget::getPitchColor(int pitchClass) {
  // Musical color wheel (Scriabin's colors)
  static const QColor pitchColors[12] = {
    QColor(255, 0, 0),    // C - Red
    QColor(255, 128, 0),  // C# - Orange
    QColor(255, 255, 0),  // D - Yellow  
    QColor(128, 255, 0),  // D# - Yellow-green
    QColor(0, 255, 0),    // E - Green
    QColor(0, 255, 128),  // F - Blue-green
    QColor(0, 255, 255),  // F# - Cyan
    QColor(0, 128, 255),  // G - Blue
    QColor(0, 0, 255),    // G# - Blue-violet
    QColor(128, 0, 255),  // A - Violet
    QColor(255, 0, 255),  // A# - Magenta  
    QColor(255, 0, 128)   // B - Rose
  };
  
  return pitchColors[pitchClass % 12];
}

// Slot implementations for individual signals (if you want to use them separately)
void MultiResolutionVisualizerWidget::onBassAnalysis(const QVector<float>& bands) {
  _bassLevels = bands;
  update();
}

void MultiResolutionVisualizerWidget::onHarmonicAnalysis(const QVector<float>& bands) {
  _harmonicLevels = bands;  
  update();
}

void MultiResolutionVisualizerWidget::onPercussiveAnalysis(const QVector<float>& bands) {
  _percLevels = bands;
  update();
}

void MultiResolutionVisualizerWidget::onChromagram(const QVector<float>& chroma) {
  _chromaLevels = chroma;
  update();
}

void MultiResolutionVisualizerWidget::onSpectralFeatures(float centroid, float rolloff, float zcr) {
  // These are handled in the comprehensive data update
  update();
}

void MultiResolutionVisualizerWidget::onBeatTracking(float phase, float period, float confidence) {
  // These are handled in the comprehensive data update
  update();
}

void MultiResolutionVisualizerWidget::onOnsetDetection(const QVector<float>& strength, bool isOnset) {
  if (isOnset) {
    _isOnset = true;
    _onsetFlashTimer = 10;
  }
  update();
}

//#include "MultiResolutionVisualizerWidget.moc"