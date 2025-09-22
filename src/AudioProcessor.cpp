#include "AudioProcessor.h"
#include <cmath>
#include <cstring>
#include <algorithm>

AudioProcessor::AudioProcessor(QObject* parent) : QObject(parent) {}
AudioProcessor::~AudioProcessor() {
  if (_cfg) { kiss_fftr_free(_cfg); _cfg = nullptr; }
}

void AudioProcessor::start() {
  _stop.store(false);
  _cfg = nullptr; // force re-init with current _sr
  _fifoL.clear();
  _fifoR.clear();
}
void AudioProcessor::requestStop() {
  _stop.store(true);
  if (_cfg) { kiss_fftr_free(_cfg); _cfg = nullptr; }
  _fifoL.clear(); _fifoR.clear();
}

// Receive LEFT/RIGHT samples from capture and append to their FIFOs.
void AudioProcessor::onFrames(const QVector<float>& left,
                              const QVector<float>& right)
{
  if (_stop.load()) return;
  if (left.isEmpty() || right.isEmpty()) return;

  // Enforce alignment: use the smaller of the two to avoid drift.
  int nL = left.size();
  int nR = right.size();
  int n  = (nL < nR) ? nL : nR;
  if (n <= 0) return;

  if (nL != nR) {
    // Optional: warn about mismatch, but still process the smaller chunk.
    qWarning("AudioProcessor::onFrames: left/right size mismatch %d != %d",
             nL, nR);
  }

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

void AudioProcessor::ensureInit() {
  if (_cfg) return;

  // Allocate FFT plan
  _cfg = kiss_fftr_alloc(_N, 0, nullptr, nullptr);
  if (!_cfg) { qWarning("kiss_fftr_alloc failed"); return; }

  // Buffers sized to FFT
  _window.assign(_N, 0.0f);
  _frameL.assign(_N, 0.0f);
  _specL.assign(_N/2 + 1, kiss_fft_cpx{0,0});
  _magL.assign(_N/2 + 1, 0.0f);
  _frameR.assign(_N, 0.0f);
  _specR.assign(_N/2 + 1, kiss_fft_cpx{0,0});
  _magR.assign(_N/2 + 1, 0.0f);

  computeWindow();
  //computeBinLayout();
  ensureLayout32();

}

void AudioProcessor::computeWindow() {
  // Hann window
  for (int n = 0; n < _N; ++n) {
    _window[n] = 0.5f * (1.0f - std::cos(2.0f * float(M_PI) * n / (_N - 1)));
  }
}

void AudioProcessor::processAvailableStereo() {
  ensureInit();
  if (!_cfg) return;  // if plan allocation failed, bail safely

  // Consume while both channels have at least N samples.
  while (!_stop.load() &&
         _fifoL.size() >= (int)_N &&
         _fifoR.size() >= (int)_N)
  {
    processOneFrameStereo();  // copies N from each FIFO, windows, FFTs L/R, bands, etc.

      // --- 32 (L/R) -> 16 mono control bins (simple & fast) ---
  QVector<float> bins16(16, 0.0f);

  if (_bands32L.size() == 32 && _bands32R.size() == 32) {
    // Pair adjacent 32-bins, average L/R then average the pair.
    for (int i = 0; i < 16; ++i) {
      const int k0 = 2*i;
      const int k1 = k0 + 1;

      float lPair = 0.5f * (_bands32L[k0] + _bands32L[k1]);
      float rPair = 0.5f * (_bands32R[k0] + _bands32R[k1]);
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

    // Slide both FIFOs forward by hop in lock-step.
    _fifoL.erase(_fifoL.begin(), _fifoL.begin() + _hop);
    _fifoR.erase(_fifoR.begin(), _fifoR.begin() + _hop);
  }
}


void AudioProcessor::ensureLayout32() {
  if (_kLo32.size() == 32 && _kHi32.size() == 32) return;

  _kLo32.resize(32);
  _kHi32.resize(32);

  const float fNyq = 0.5f * _sr;
  const float fMin = 20.0f;
  const float fMax = std::min(18000.0f, 0.98f * fNyq);

  // Log-spaced edges (33 points for 32 bands)
  float edges[33];
  const float ratio = fMax / fMin;
  for (int i = 0; i <= 32; ++i) {
    float t = float(i) / 32.0f;
    edges[i] = fMin * std::pow(ratio, t);
  }

  auto hzToK = [&](float f)->int {
    int k = int(std::floor(f * _N / _sr));
    // skip DC (k=0); clamp to valid [1, N/2]
    int kMin = 1;
    int kMax = std::max(2, _N/2);
    return std::clamp(k, kMin, kMax);
  };

  for (int i = 0; i < 32; ++i) {
    int k0 = hzToK(edges[i]);
    int k1 = hzToK(edges[i+1]);
    if (k1 <= k0) k1 = std::min(k0 + 1, _N/2);
    _kLo32[i] = k0;
    _kHi32[i] = k1;
  }

  _bands32L.resize(32);
  _bands32R.resize(32);
}


void AudioProcessor::processOneFrameStereo() {
  // 1) Copy first N from FIFOs
  std::memcpy(_frameL.data(), _fifoL.data(), _N * sizeof(float));
  std::memcpy(_frameR.data(), _fifoR.data(), _N * sizeof(float));


  // 2) Window (Hann precomputed)
  for (int i = 0; i < _N; ++i) {
    _frameL[i] *= _window[i];
    _frameR[i] *= _window[i];
  }

  // 3) FFT (sequential, single plan is fine)
  kiss_fftr(_cfg, _frameL.data(), _specL.data());
  kiss_fftr(_cfg, _frameR.data(), _specR.data());


  // 5) Sum simple magnitudes into 32 bands (no normalization yet)
  //    (Skip DC by construction of kLo/kHi)
  for (int b = 0; b < 32; ++b) {
    float sumL = 0.0f;
    float sumR = 0.0f;
    const int k0 = _kLo32[b];
    const int k1 = _kHi32[b]; // exclusive

    for (int k = k0; k < k1; ++k) {
      const kiss_fft_cpx &XL = _specL[k];
      const kiss_fft_cpx &XR = _specR[k];
      // pure magnitude (linear). Simple & fast.
      sumL += std::hypot((float)XL.r, (float)XL.i);
      sumR += std::hypot((float)XR.r, (float)XR.i);
    }

    _bands32L[b] = sumL;
    _bands32R[b] = sumR;

    QVector<float> qL = QVector<float>(static_cast<int>(_bands32L.size()));
    QVector<float> qR = QVector<float>(static_cast<int>(_bands32R.size()));
    std::memcpy(qL.data(), _bands32L.data(), _bands32L.size()*sizeof(float));
    std::memcpy(qR.data(), _bands32R.data(), _bands32R.size()*sizeof(float));

    emit bins32ReadyRaw(qL, qR);
  }
}





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