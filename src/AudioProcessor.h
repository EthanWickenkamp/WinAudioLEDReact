#pragma once
#include <QObject>
#include <QVector>
#include <atomic>
#include <vector>

extern "C" {
  #include "kiss_fftr.h"
}

class AudioProcessor : public QObject {
  Q_OBJECT
public:
  explicit AudioProcessor(QObject* parent = nullptr);
  ~AudioProcessor();

  // Use actual sample rate from capture (call once after capture reports it)
  void setSampleRate(int sr) { 
    if (sr != _sr) {
      _sr = sr; 
      _initialized = false;  // Force re-initialization
    }
  }

public slots:
  void start();
  void requestStop(); 
  void onFrames(const QVector<float>& left, const QVector<float>& right);

signals:
  // 16 bins, normalized 0..1 (per-frame), ready for UDP sender.
  void binsReady(const QVector<float>& bins16);
  // 32 bins, raw linear magnitudes for BarsWidget
  void bins32ReadyRaw(const QVector<float>& left, const QVector<float>& right);
  void levelsReady(float leftDb, float rightDb);  // RMS levels in dBFS
  void stopped();

private:
  // FFT parameters
  int _sr   = 48000;   // Sample rate
  int _N    = 1024;    // FFT size (~21.3 ms @ 48k)
  int _hop  = 256;     // Hop size (~5.3 ms update cadence)

  // State management
  std::atomic<bool> _stop{false};
  bool _initialized = false;

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

  // Frequency band analysis (32 bands)
  QVector<int> _kLo32, _kHi32;               // Frequency bin ranges for 32 bands
  std::vector<float> _bands32L;               // Left channel band magnitudes (32)
  std::vector<float> _bands32R;               // Right channel band magnitudes (32)

  // Core processing methods
  void initialize();                          // Main initialization
  void cleanup();                            // Resource cleanup
  void processAvailableStereo();             // Process available stereo samples
  void processOneFrameStereo();              // Process one frame of stereo audio
  
  // Setup methods
  void computeWindow();                      // Compute Hann window
  void setupFrequencyBands();                // Setup 32-band frequency layout
  
  // Analysis methods
  void computeFrequencyBands();              // Compute band magnitudes from FFT
  void emitResults();                        // Emit all signals
  float computeRMS(const std::vector<float>& frame);  // Compute RMS of frame
};