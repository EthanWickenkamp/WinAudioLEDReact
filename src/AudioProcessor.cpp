#include "AudioProcessor.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <QDebug>

AudioProcessor::AudioProcessor(QObject* parent) : QObject(parent) {}

AudioProcessor::~AudioProcessor() {
  cleanup();
}

void AudioProcessor::cleanup() {
  if (_cfg) { 
    kiss_fftr_free(_cfg); 
    _cfg = nullptr; 
  }
  _fifoL.clear(); 
  _fifoR.clear();
}

void AudioProcessor::requestStop() {
  if (!_running.exchange(false)) return;
  cleanup();
  emit stopped();
}

void AudioProcessor::start() {
  // Prevent double-start.
  if (_running.exchange(true)) return;
  _initialized = false;
  cleanup();
}

void AudioProcessor::initialize() {
  if (_initialized) return;

  // Allocate FFT plan
  _cfg = kiss_fftr_alloc(_N, 0, nullptr, nullptr);
  if (!_cfg) { 
    qWarning("kiss_fftr_alloc failed"); 
    return; 
  }

  // Buffers sized to FFT
  _window.assign(_N, 0.0f);
  _frameL.assign(_N, 0.0f);
  _specL.assign(_N/2 + 1, kiss_fft_cpx{0,0});
  _magL.assign(_N/2 + 1, 0.0f);
  _frameR.assign(_N, 0.0f);
  _specR.assign(_N/2 + 1, kiss_fft_cpx{0,0});
  _magR.assign(_N/2 + 1, 0.0f);

  // Setup all components
  computeWindow();
  computeDCBlockerCoeff();  // <-- ADD THIS LINE
  setupFrequencyBands();

  _initialized = true;
}

static int nearestPow2Clamped(int x, int lo = 1024, int hi = 4096) {
  int p = 1; while (p < x) p <<= 1;
  return std::clamp(p, lo, hi);
}

void AudioProcessor::setSampleRate(int sr) {
  if (sr <= 0) return;
  // If SR is unchanged, nothing to do.
  if (sr == _sr) return;
  _sr = sr;
  // Optional: keep a roughly constant time window (~43 ms)
  const int targetSamples = int(std::lround(_sr * 0.043));  // ~43 ms
  const int newN = nearestPow2Clamped(targetSamples, 1024, 4096);
  if (newN != _N) {
    _N   = newN;
    _hop = _N / 2;                  // 50% overlap
  }
  // Force a fresh DSP init on next frames
  _initialized = false;
  cleanup();                        // free old FFT plan/buffers so initialize() rebuilds with new SR/_N
}

void AudioProcessor::computeWindow() {
  // Hann window
  for (int n = 0; n < _N; ++n) {
    _window[n] = 0.5f * (1.0f - std::cos(2.0f * float(M_PI) * n / (_N - 1)));
  }
}

void AudioProcessor::setNumBands(int n) {
  // clamp to allowed values; expand later if you want.
  if (n != 16 && n != 32 && n != 64) return;
  if (n == _numBands) return;
  _numBands = n;
  _initialized = false;
  cleanup();              // rebuild edges on next initialize()
}

void AudioProcessor::setupFrequencyBands() {
  _kLo.resize(_numBands);
  _kHi.resize(_numBands);
  _bandsL.assign(_numBands, 0.0f);
  _bandsR.assign(_numBands, 0.0f);

  const float fNyq = 0.5f * _sr;
  const float fMin = 20.0f;
  const float fMax = std::min(18000.0f, 0.98f * fNyq);

  // Log-spaced edges: (_numBands + 1) points
  std::vector<float> edges(_numBands + 1);
  const float ratio = fMax / fMin;
  for (int i = 0; i <= _numBands; ++i) {
    float t = float(i) / float(_numBands);
    edges[i] = fMin * std::pow(ratio, t);
  }

  auto hzToK = [&](float f)->int {
    int k = int(std::floor(f * _N / _sr));
    int kMin = 1;
    int kMax = std::max(2, _N/2);
    return std::clamp(k, kMin, kMax);
  };

  for (int i = 0; i < _numBands; ++i) {
    int k0 = hzToK(edges[i]);
    int k1 = hzToK(edges[i+1]);
    if (k1 <= k0) k1 = std::min(k0 + 1, _N/2);
    _kLo[i] = k0;
    _kHi[i] = k1;
  }
}

// Receive LEFT/RIGHT samples from capture and append to their FIFOs.
void AudioProcessor::onFrames(const QVector<float>& left, const QVector<float>& right)
{
  if (!_running.load()) return;
  if (left.isEmpty() || right.isEmpty()) return;

  // Enforce alignment: use the smaller of the two to avoid drift.
  int nL = left.size();
  int nR = right.size();
  int n  = (nL < nR) ? nL : nR;
  if (n <= 0) return;

  if (nL != nR) {
    // Optional: warn about mismatch, but still process the smaller chunk.
    qWarning("AudioProcessor::onFrames: left/right size mismatch %d != %d", nL, nR);
  }

    // === LOG INCOMING AUDIO (Remove after debugging) ===
  static int logCounter = 0;
  if (logCounter++ % 50 == 0) {  // Log every 50th frame to avoid spam
    logAudioStats(left, "LEFT_IN ");
    logAudioStats(right, "RIGHT_IN");
  }
  // === END LOG ===

  // Append to L FIFO.
  const int prevL = _fifoL.size();
  _fifoL.reserve(prevL + n);          // no-op if already large enough
  _fifoL.resize(prevL + n);
  std::memcpy(_fifoL.data() + prevL, left.constData(), n * sizeof(float));

  // Append to R FIFO.
  const int prevR = _fifoR.size();
  _fifoR.reserve(prevR + n);
  _fifoR.resize(prevR + n);
  std::memcpy(_fifoR.data() + prevR, right.constData(), n * sizeof(float));

  // Drive stereo processing (will consume in lock-step by _hop).
  processAvailableStereo();
}

void AudioProcessor::processAvailableStereo() {
  initialize();
  if (!_initialized || !_cfg) return;  // if initialization failed, bail safely

  // Consume while both channels have at least N samples.
  while (_running.load() && (int)_fifoL.size() >= _N && (int)_fifoR.size() >= _N){
    processOneFrameStereo();  // copies N from each FIFO, windows, FFTs L/R, bands, etc.

    // --- TEMPORARY: 32 (L/R) -> 16 mono control bins (simple & fast) ---
    // TODO: This is a temporary solution, should be refactored
    QVector<float> bins16(16, 0.0f);

    if (_bandsL.size() == 32 && _bandsR.size() == 32) {
      // Pair adjacent 32-bins, average L/R then average the pair.
      for (int i = 0; i < 16; ++i) {
        const int k0 = 2*i;
        const int k1 = k0 + 1;

        float lPair = 0.5f * (_bandsL[k0] + _bandsL[k1]);
        float rPair = 0.5f * (_bandsR[k0] + _bandsR[k1]);
        float mono  = 0.5f * (lPair + rPair);   // simple LR average

        bins16[i] = mono;  // raw linear magnitude for now
      }

      // Per-frame normalize to 0..1 so UDP sender can map to 0..255 cleanly.
      float mx = 0.0f;
      for (float v : bins16) if (v > mx) mx = v;
      if (mx > 0.0f) {
        const float inv = 1.0f / mx;
        for (float &v : bins16) v = std::clamp(v * inv, 0.0f, 1.0f);
      }
    } else {
      // If 32s aren't ready for some reason, emit zeros (valid shape).
      // bins16 already zero-initialized.
    }

    emit binsReady(bins16);
    // --- END TEMPORARY BLOCK ---

    // Slide both FIFOs forward by hop in lock-step.
    _fifoL.erase(_fifoL.begin(), _fifoL.begin() + _hop);
    _fifoR.erase(_fifoR.begin(), _fifoR.begin() + _hop);
  }
}

void AudioProcessor::processOneFrameStereo() {
  // 1) Copy first N from FIFOs
  std::memcpy(_frameL.data(), _fifoL.data(), _N * sizeof(float));
  std::memcpy(_frameR.data(), _fifoR.data(), _N * sizeof(float));

    // === LOG BEFORE DC BLOCKER ===
  static int frameCounter = 0;
  bool shouldLog = (frameCounter++ % 50 == 0);
  
  if (shouldLog) {
    QVector<float> beforeL(_frameL.begin(), _frameL.end());
    logAudioStats(beforeL, "BEFORE_DC");
  }
  // === END LOG ===

    // 1.5) Apply DC blocker BEFORE windowing
  applyDCBlocker(_frameL, _dcBlockerXprevL, _dcBlockerYprevL);
  applyDCBlocker(_frameR, _dcBlockerXprevR, _dcBlockerYprevR);

    // === LOG AFTER DC BLOCKER ===
  if (shouldLog) {
    QVector<float> afterL(_frameL.begin(), _frameL.end());
    logAudioStats(afterL, "AFTER_DC ");
    
    // Also log DC blocker state
    qDebug() << "DC_STATE | xPrevL:" << _dcBlockerXprevL 
             << "| yPrevL:" << _dcBlockerYprevL
             << "| coeff:" << _dcBlockerCoeff;
  }
  // === END LOG ===

  // 2) Window (Hann precomputed)
  for (int i = 0; i < _N; ++i) {
    _frameL[i] *= _window[i];
    _frameR[i] *= _window[i];
  }

  // 3) FFT (sequential, single plan is fine)
  kiss_fftr(_cfg, _frameL.data(), _specL.data());
  kiss_fftr(_cfg, _frameR.data(), _specR.data());

  // 4) Compute frequency bands
  computeFrequencyBands();

  // 5) Emit results
  emitResults();
}

void AudioProcessor::computeFrequencyBands() {
  for (int b = 0; b < _numBands; ++b) {
    float sumL = 0.0f, sumR = 0.0f;
    const int k0 = _kLo[b];
    const int k1 = _kHi[b]; // exclusive
    for (int k = k0; k < k1; ++k) {
      const kiss_fft_cpx &XL = _specL[k];
      const kiss_fft_cpx &XR = _specR[k];
      sumL += std::hypot((float)XL.r, (float)XL.i);
      sumR += std::hypot((float)XR.r, (float)XR.i);
    }
    _bandsL[b] = sumL;
    _bandsR[b] = sumR;
  }
}

static QVector<float> downmixToN(const std::vector<float>& src, int dstN) {
  const int srcN = (int)src.size();
  if (srcN <= 0 || dstN <= 0) return QVector<float>(dstN, 0.0f);

  QVector<float> out(dstN, 0.0f);

  // Simple energy-preserving bucket average (contiguous groups).
  // Map each dst bin to a [a,z) range in src indices.
  for (int i = 0; i < dstN; ++i) {
    const float aF = (float(i)    / dstN) * srcN;
    const float zF = (float(i+1)  / dstN) * srcN;
    const int   a  = std::max(0, (int)std::floor(aF));
    const int   z  = std::min(srcN, (int)std::ceil (zF));
    float acc = 0.0f;
    int   cnt = 0;
    for (int k = a; k < z; ++k) { acc += src[k]; ++cnt; }
    out[i] = (cnt > 0) ? acc / float(cnt) : 0.0f;
  }
  return out;
}

void AudioProcessor::emitResults() {
  // Emit raw current-size bands to the widget
  QVector<float> qL(_bandsL.begin(), _bandsL.end());
  QVector<float> qR(_bandsR.begin(), _bandsR.end());
  emit binsReadyRaw(qL, qR);

  // Always provide 16 for UDP sender (from whatever _numBands is)
  QVector<float> bins16 = downmixToN(_bandsL, 16);
  // normalize 0..1 like you did
  float mx = 0.0f; for (float v : bins16) mx = std::max(mx, v);
  if (mx > 0.0f) {
    const float inv = 1.0f / mx;
    for (float &v : bins16) v = std::clamp(v * inv, 0.0f, 1.0f);
  }
  emit binsReady(bins16);

  // Levels (unchanged)
  float rmsL = computeRMS(_frameL);
  float rmsR = computeRMS(_frameR);
  float dbL = 20.0f * std::log10(std::max(rmsL, 1e-6f));
  float dbR = 20.0f * std::log10(std::max(rmsR, 1e-6f));
  emit levelsReady(dbL, dbR);
}

float AudioProcessor::computeRMS(const std::vector<float>& frame) {
  float rms = 0.0f;
  for (int n = 0; n < _N; ++n) {
    rms += frame[n] * frame[n];
  }
  return std::sqrt(rms / float(_N));
}

void AudioProcessor::computeDCBlockerCoeff() {
  // DC blocker is a 1st-order high-pass filter
  // Transfer function: H(z) = (1 - z^-1) / (1 - R*z^-1)
  // where R = 1 - (2*pi*fc/fs)
  // For fc = 20Hz cutoff
  
  const float fc = 20.0f;  // Cutoff frequency in Hz
  const float fs = static_cast<float>(_sr);
  
  // Calculate coefficient (closer to 1.0 = higher cutoff)
  _dcBlockerCoeff = 1.0f - (2.0f * float(M_PI) * fc / fs);
  
  // Clamp to safe range
  _dcBlockerCoeff = std::clamp(_dcBlockerCoeff, 0.9f, 0.999f);
}

// Apply DC blocker to a frame
void AudioProcessor::applyDCBlocker(std::vector<float>& frame, float& xPrev, float& yPrev) {
  // Process each sample in the frame
  for (int i = 0; i < _N; ++i) {
    const float x = frame[i];
    const float y = x - xPrev + _dcBlockerCoeff * yPrev;
    
    xPrev = x;
    yPrev = y;
    frame[i] = y;
  }
}

void AudioProcessor::logAudioStats(const QVector<float>& samples, const QString& label) {
  if (samples.isEmpty()) return;
  
  float minVal = samples[0];
  float maxVal = samples[0];
  float sumAbs = 0.0f;
  float sumSq = 0.0f;
  int zeroCount = 0;
  
  for (float s : samples) {
    minVal = std::min(minVal, s);
    maxVal = std::max(maxVal, s);
    sumAbs += std::abs(s);
    sumSq += s * s;
    if (s == 0.0f) zeroCount++;
  }
  
  float mean = sumAbs / samples.size();
  float rms = std::sqrt(sumSq / samples.size());
  
  qDebug() << label 
           << "| samples:" << samples.size()
           << "| min:" << minVal 
           << "| max:" << maxVal
           << "| mean(abs):" << mean
           << "| rms:" << rms
           << "| zeros:" << zeroCount;
}



// LEGACY CODE - COMMENTED OUT FOR REFERENCE
// void AudioProcessor::processAvailable() {
//   ensureInit();
//   while (!_stop.load() && (int)_fifo.size() >= _N) {
//     processOneFrame();
//     // Pop hop samples (slide window)
//     _fifo.erase(_fifo.begin(), _fifo.begin() + _hop);
//   }
// }

// void AudioProcessor::processOneFrame() {
//   // Copy N samples from FIFO into frame with window
//   for (int n = 0; n < _N; ++n) {
//     _frame[n] = _fifo[n] * _window[n];
//   }

//   // Real FFT
//   kiss_fftr(_cfg, _frame.data(), _spec.data());

//   // Magnitude (single-sided) with scale
//   const float scale = 2.0f / float(_N);
//   _mag[0] = std::hypot(_spec[0].r, _spec[0].i) * scale;          // DC (we'll ignore in bins)
//   for (int k = 1; k < _N/2; ++k) {
//     _mag[k] = std::hypot(_spec[k].r, _spec[k].i) * scale;
//   }
//   _mag[_N/2] = std::hypot(_spec[_N/2].r, _spec[_N/2].i) * scale; // Nyquist

//   // --- RAW BIN AGGREGATION: RMS (no AGC, no normalization, no compression) ---
//   QVector<float> binsRMS(_bins, 0.0f);
//   for (int b = 0; b < _bins; ++b) {
//     int a = _kLo[b], z = _kHi[b];
//     float acc2 = 0.0f;
//     int cnt = 0;
//     for (int k = a; k < z; ++k) { float m = _mag[k]; acc2 += m * m; ++cnt; }
//     binsRMS[b] = (cnt > 0) ? std::sqrt(acc2 / cnt) : 0.0f;
//   }

//   // Frequency edges for each bin (Hz)
//   QVector<float> fLo(_bins), fHi(_bins);
//   for (int b = 0; b < _bins; ++b) {
//     fLo[b] = float(_kLo[b]) * float(_sr) / float(_N);
//     fHi[b] = float(_kHi[b]) * float(_sr) / float(_N);
//   }

//   // Emit RAW for the BarsWidget (unscaled, linear RMS)
//   emit binsReadyRaw(binsRMS, fLo, fHi);

//   // If you still want to feed any legacy consumers, also emit binsReady as RAW:
//   emit binsReady(binsRMS);

//   // Optional: levels—duplicate mono as L/R for now (or compute RMS on _frame)
//   float rms = 0.f;
//   for (int n = 0; n < _N; ++n) rms += _frame[n] * _frame[n];
//   rms = std::sqrt(rms / float(_N));
//   float db = 20.0f * std::log10(std::max(rms, 1e-6f));
//   emit levelsReady(db, db);
// }


// void AudioProcessor::computeBinLayout() {
//   _bins = 16; // fixed

//   // ---------- A) Define 4 overlapping bass bins in Hz ----------
//   struct Band { float f0, f1; };
//   Band bass[4] = {
//     { 20.0f,  45.0f },  // Bin 1
//     { 35.0f,  70.0f },  // Bin 2 (overlaps B1)
//     { 55.0f, 100.0f },  // Bin 3 (overlaps B2)
//     { 80.0f, 140.0f }   // Bin 4 (overlaps B3)
//   };

//   // Effective analysis limits.
//   const float fMin = 20.0f;
//   const float fNyq = 0.5f * _sr;
//   const float fMax = std::min(18000.0f, 0.98f * fNyq);

//   // Clamp cutoff and bass bands to valid range.
//   const float cutoff = std::clamp(_bassCutoffHz, fMin, fMax);

//   // ---------- B) Build 12 non-overlapping bins above the cutoff (log spaced) ----------
//   const int hiBins = 12;                 // bins 5..16
//   std::vector<float> hiEdges(hiBins + 1);
//   // If cutoff >= fMax, fallback to tiny span to avoid div by zero.
//   const float fLoHi = std::min(cutoff, fMax - 1.0f);
//   const float fHiHi = fMax;

//   // Log edges from max(cutoff,fMin) to fMax
//   float startHz = std::max(fLoHi, fMin + 1.0f);
//   for (int i = 0; i <= hiBins; ++i) {
//     float t = float(i) / float(hiBins);
//     hiEdges[i] = startHz * std::pow(fHiHi / startHz, t);
//     if (i > 0) hiEdges[i] = std::max(hiEdges[i], hiEdges[i-1] + 1e-3f);
//   }

//   // ---------- C) Map Hz to FFT bin indices (allow overlap for bass) ----------
//   _kLo.assign(_bins, 0);
//   _kHi.assign(_bins, 0);

//   const int kMin = 1;                 // skip DC
//   const int kMax = std::max(2, _N/2);

//   auto hzToK = [&](float f)->int {
//     int k = int(std::floor(f * _N / _sr));
//     return std::clamp(k, kMin, kMax);
//   };

//   // Bass bins 1..4 (with overlap)
//   for (int b = 0; b < 4; ++b) {
//     int k0 = hzToK(std::clamp(bass[b].f0, fMin, fMax));
//     int k1 = hzToK(std::clamp(bass[b].f1, fMin, fMax));
//     if (k1 <= k0) k1 = std::min(k0 + 1, kMax);
//     _kLo[b] = k0;
//     _kHi[b] = k1;
//   }

//   // High bins 5..16 (non-overlapping from hiEdges)
//   for (int i = 0; i < hiBins; ++i) {
//     int b = 4 + i; // global bin index
//     int k0 = hzToK(hiEdges[i]);
//     int k1 = hzToK(hiEdges[i+1]);
//     if (k1 <= k0) k1 = std::min(k0 + 1, kMax);
//     _kLo[b] = k0;
//     _kHi[b] = k1;
//   }
// }