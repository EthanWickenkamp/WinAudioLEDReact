#pragma once
#include <QObject>
#include <QVector>
#include <atomic>
#include <vector>

extern "C" {
  #include "kiss_fftr.h"
}

// class CircularBuffer {
//     QVector<float> _data;
//     int _writePos = 0;
//     int _readPos = 0;
//     int _size = 0;
    
// public:
//     void push(const float* samples, int count) {
//         // Write without shifting existing data
//     }
//     bool canRead(int samples) const {
//         return _size >= samples;
//     }
//     void read(float* out, int count, bool advance = true) {
//         // Read without erasing
//     }
// };

class AudioProcessor : public QObject {
  Q_OBJECT
public:
  explicit AudioProcessor(QObject* parent = nullptr);
  ~AudioProcessor();

public slots:
  // state management
  void start();
  void requestStop();
  // ---- Active path: receive R and L frames ----
  void onFrames(const QVector<float>& left, const QVector<float>& right);
  // receive sample rate from capture
  void setSampleRate(int sr);
  // set number of frequency bands (16, 32, 64)
  void setNumBands(int n);

signals:
  // state management
  void status(const QString& msg);
  void stopped();

  // 32 bins, raw linear magnitudes for BarsWidget
  void binsReadyRaw(const QVector<float>& left, const QVector<float>& right);
  // 16 bins, normalized 0..1 (per-frame), ready for UDP sender.
  void binsReady(const QVector<float>& bins16);

  // RMS levels in dBFS
  void levelsReady(float leftDb, float rightDb);  // RMS levels in dBFS

private:
  // state management
  std::atomic<bool> _running{false};
  bool _initialized = false;
  void initialize();                          // Main initialization
  void cleanup();                            // Resource cleanup


  // FFT parameters
  int _sr   = 48000;   // Sample rate
  int _N    = 1024;    // FFT size (~21.3 ms @ 48k)
  int _hop  = 512;     // Hop size (~5.3 ms update cadence)

  int _numBands = 16;                // << one knob: 16, 32, 64
  QVector<int> _kLo, _kHi;           // size = _numBands
  std::vector<float> _bandsL, _bandsR; // size = _numBands

  // FFT resources
  kiss_fftr_cfg _cfg = nullptr;
  std::vector<float> _window;                 // Hann window (length N)
  std::vector<float> _frameL;                 // Left channel frame (length N)
  std::vector<float> _frameR;                 // Right channel frame (length N)
  std::vector<kiss_fft_cpx> _specL;          // Left spectrum (N/2+1)
  std::vector<kiss_fft_cpx> _specR;          // Right spectrum (N/2+1)
  std::vector<float> _magL;                   // Left magnitudes (N/2+1) - unused but kept for compatibility
  std::vector<float> _magR;                   // Right magnitudes (N/2+1) - unused but kept for compatibility

  // Audio input buffers
  std::vector<float> _fifoL;                  // Left channel FIFO
  std::vector<float> _fifoR;                  // Right channel FIFO

  // Core processing methods
  void processAvailableStereo();             // Process available stereo samples
  void processOneFrameStereo();              // Process one frame of stereo audio
  
  // Setup methods
  void computeWindow();                      // Compute Hann window

  void setupFrequencyBands();                // Setup 32-band frequency layout
  
  // Analysis methods
  void computeFrequencyBands();              // Compute band magnitudes from FFT
  void emitResults();                        // Emit all signals
  float computeRMS(const std::vector<float>& frame);  // Compute RMS of frame


  float _dcBlockerCoeff = 0.995f;  // Will be recalculated based on sample rate
  float _dcBlockerXprevL = 0.0f;   // Previous input sample (Left)
  float _dcBlockerYprevL = 0.0f;   // Previous output sample (Left)
  float _dcBlockerXprevR = 0.0f;   // Previous input sample (Right)
  float _dcBlockerYprevR = 0.0f;   // Previous output sample (Right)
  
  // Helper method
  void computeDCBlockerCoeff();
  void applyDCBlocker(std::vector<float>& frame, float& xPrev, float& yPrev);

  void logAudioStats(const QVector<float>& samples, const QString& label);

    // Noise gate
  float _noiseGateThreshold = 0.001f;  // RMS threshold for silence detection
  
  // Helper methods
  QVector<float> resampleBandsTo16(const std::vector<float>& bandsL, const std::vector<float>& bandsR);
  bool isSignalAboveNoiseFloor(float rmsL, float rmsR);
};