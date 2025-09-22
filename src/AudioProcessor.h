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
  void setSampleRate(int sr) { _sr = sr; _cfg = nullptr; }  // forces re-init on next use

public slots:
  void start();
  void requestStop(); 
  void onFrames(const QVector<float>& left, const QVector<float>& right);   // <- L/R from AudioCapture::framesReady()

signals:
  // 16 bins, normalized 0..1 (per-frame), ready for UDP sender.
  void binsReady(const QVector<float>& bins16);
  //new 32 bins, raw linear RMS, ready for BarsWidget
  void bins32ReadyRaw(const QVector<float>& left, const QVector<float>& right);
  void levelsReady(float leftDb, float rightDb);  // peak levels in dBFS (smoothed)
  void stopped();

private:
  // FFT params (defaults; will re-init if _sr changes)
  int _sr   = 48000;
  int _N    = 1024;    // ~21.3 ms @ 48k
  int _hop  = 256;     // ~5.3 ms update cadence

  // Smoothing time constant (seconds) -> compute EMA alpha from hop
  float _tau = 0.10f;  // 100 ms default

  // FFT state
  kiss_fftr_cfg _cfg = nullptr;               // created by kiss_fftr_alloc()
  std::vector<float> _window;                 // Hann window
  std::vector<float> _frameL;
  std::vector<float> _frameR;                  // length N, windowed mono frame
  std::vector<kiss_fft_cpx> _specL;
  std::vector<kiss_fft_cpx> _specR;            // N/2+1 complex spectrum
  std::vector<float> _magL;
  std::vector<float> _magR;                        // magnitude spectrum (N/2+1)

  QVector<int>   _kLo32, _kHi32;     // 32-band bin ranges

  // Binning
  int _bins = 16;
  std::vector<int> _kLo, _kHi;                // inclusive k-ranges for each bin

  // FIFO of incoming mono samples from capture
  std::vector<float> _fifoL;
  std::vector<float> _fifoR;
  //std::vector<float> _fifo;  // mono interleaved (for _N samples)

  std::vector<float> _bands32L;   // size 32, left-channel magnitudes
  std::vector<float> _bands32R;   // size 32, right-channel magnitudes

  std::atomic<bool> _stop{false};

  // Helpers
  void ensureInit();
  void ensureLayout32();
  void computeWindow();
  void computeBinLayout();
  float emaAlpha() const;                     // alpha from _tau and _hop/_sr
  //void processAvailable();
  void processAvailableStereo();
  //void processOneFrame();
  void processOneFrameStereo();                       // assumes _fifo.size() >= _N
};
