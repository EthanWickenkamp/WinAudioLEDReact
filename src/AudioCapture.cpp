#include "AudioCapture.h"
#include <QString>
#include <cstring>
#include <algorithm>     // std::max
#include "miniaudio.h"

AudioCapture::AudioCapture(QObject* parent) : QObject(parent) {}

AudioCapture::~AudioCapture() {
  // Defensive cleanup.
  if (_dev) { ma_device_uninit(_dev); delete _dev; _dev = nullptr; }
  if (_ctx) { ma_context_uninit(_ctx); delete _ctx; _ctx = nullptr; }
}

void AudioCapture::requestStop() {
  if (!_running.exchange(false)) { emit stopped(); return; }
  if (_dev) {
    ma_device_stop(_dev);                // polite stop (optional but nice)
    ma_device_uninit(_dev); delete _dev; _dev = nullptr;
  }
  if (_ctx) { ma_context_uninit(_ctx); delete _ctx; _ctx = nullptr; }
  emit status("Audio stopped");
  emit stopped();
}

void AudioCapture::start() {
  // Prevent double-start.
  if (_running.load()) { emit status("Audio already running"); return; }

  // 1) Context
  _ctx = new ma_context{};
  if (ma_context_init(nullptr, 0, nullptr, _ctx) != MA_SUCCESS) {
    emit status("miniaudio: context init failed");
    delete _ctx; _ctx = nullptr;
    emit stopped();
    return;
  }

  // 2) Loopback device config
  ma_device_config cfg = ma_device_config_init(ma_device_type_loopback);

  // Inherit rate/channels, but force f32 so callback casting is safe.
  cfg.sampleRate         = 0;                   // inherit device rate
  cfg.capture.channels   = 2;                   // inherit channel count or force to 2
  cfg.capture.format     = ma_format_f32;       // force float32 to avoid ambiguity
  cfg.capture.shareMode  = ma_share_mode_shared;

  // WASAPI specifics: predictable behavior, avoid redundant SRC.
  cfg.wasapi.noAutoConvertSRC      = MA_TRUE;
  cfg.wasapi.noHardwareOffloading  = MA_TRUE;

  // Performance / callback sizing (request, not guaranteed).
  cfg.performanceProfile     = ma_performance_profile_low_latency;
  cfg.periodSizeInFrames     = 480;     // ~10 ms @ 48 kHz (tune to taste)
  cfg.periods                = 3;       // triple buffering
  cfg.noFixedSizedCallback   = MA_FALSE; // prefer fixed-size callbacks

  // Callbacks
  // give miniaudio a pointer to this object and a function to call when a period is ready
  cfg.dataCallback = &AudioCapture::dataCallback;
  cfg.pUserData    = this;

  // 3) Create and bind device to context and config, catch failures.
  _dev = new ma_device{};
  ma_result res = ma_device_init(_ctx, &cfg, _dev);
  if (res != MA_SUCCESS) {
    emit status(QString("miniaudio: loopback device init failed (%1)").arg((int)res));
    ma_context_uninit(_ctx); delete _ctx; _ctx = nullptr;
    delete _dev; _dev = nullptr;
    emit stopped();
    return;
  }

  // After ma_device_init(_ctx, &cfg, _dev) == MA_SUCCESS
  const ma_uint32 sr       = _dev->sampleRate;             // actual sample rate
  const ma_uint32 ch       = _dev->capture.channels;       // actual channels
  const ma_format fmt      = _dev->capture.format;         // actual format
  const ma_uint32 periodF  = cfg.periodSizeInFrames;     // actual period (frames)
  const ma_uint32 periods  = cfg.periods;                  // actual periods (buffers)

  QString info = QStringLiteral(
    "Loopback actual: %1 Hz, %2 ch, fmt=%3 (period = %4 frames x %5)")
    .arg(sr)
    .arg(ch)
    .arg(int(fmt))       // cast enum to int for printing
    .arg(periodF)
    .arg(periods);

  emit status(info);

  // Preallocate scratch buffer for max expected frames per callback
  // this gets written with each callbacks section of audio
  ma_uint32 reserveFrames = cfg.periodSizeInFrames;
  if (reserveFrames == 0) {
    // Fallback: a safe upper bound for common WASAPI/ASIO/ALSA periods
    // 2048 is plenty for 48 kHz with ~40 ms periods.
    reserveFrames = 2048;
  }
  _scratchL.reserve(static_cast<int>(reserveFrames));
  _scratchR.reserve(static_cast<int>(reserveFrames));

  // 4) Start device catch failures to clear and return cleanly.
  if (ma_device_start(_dev) != MA_SUCCESS) {
    emit status("miniaudio: device start failed");
    ma_device_uninit(_dev); delete _dev; _dev = nullptr;
    ma_context_uninit(_ctx); delete _ctx; _ctx = nullptr;
    emit stopped();
    return;
  }

  _running.store(true);
}

static inline void zeroStereo(QVector<float>& L, QVector<float>& R, int frames) {
  L.resize(frames); R.resize(frames);
  std::fill(L.begin(), L.end(), 0.0f);
  std::fill(R.begin(), R.end(), 0.0f);
}


// static audio thread callback
// args(device that triggers callback, unused output buffer, input frames, number of frames)
void AudioCapture::dataCallback(ma_device* dev, void* pOutput, const void* pInput, ma_uint32 frameCount) {
  (void)pOutput; // capture-only
  // retrieve "this" pointer from device user data
  auto* self = static_cast<AudioCapture*>(dev ? dev->pUserData : nullptr);
  if (!self || !self->_running.load() || frameCount == 0) return;

  // We forced f32 above, so this cast is safe.
  const ma_uint32 ch = (dev->capture.channels > 0) ? dev->capture.channels : 2;

  if (pInput == nullptr) {
    // Keep cadence stable on silence/glitch.
    zeroStereo(self->_scratchL, self->_scratchR, int(frameCount));
    emit self->framesReady(self->_scratchL, self->_scratchR);
    return;
  }

  const float* in = static_cast<const float*>(pInput);
  self->emitStereo(in, frameCount, ch);
}



void AudioCapture::emitStereo(const float* interleaved,
                                    unsigned frames, unsigned channels)
{
  if (!_running.load()) return;

  _scratchL.resize(int(frames));
  _scratchR.resize(int(frames));

  if (channels == 1) {
    // Duplicate mono → L and R (fast path).
    std::memcpy(_scratchL.data(), interleaved, sizeof(float)*frames);
    std::memcpy(_scratchR.data(), interleaved, sizeof(float)*frames);
  } else {
    // Take first two channels as L/R. (For >2ch you can change policy later.)
    const unsigned stride = channels;
    const float* src = interleaved;
    float* dstL = _scratchL.data();
    float* dstR = _scratchR.data();

    for (unsigned i = 0; i < frames; ++i, src += stride) {
      dstL[i] = src[0];
      dstR[i] = (channels >= 2) ? src[1] : src[0]; // safety net
    }
  }

  emit framesReady(_scratchL, _scratchR); // queued to processing thread
}

// void AudioCapture::emitMono(const float* interleaved, unsigned int frames, unsigned int channels) {
//   if (!_running.load()) return;

//   // Prepare mono buffer (one sample per frame).
//   _scratch.resize(int(frames));

//   if (channels == 1) {
//     // Fast path: already mono
//     std::memcpy(_scratch.data(), interleaved, sizeof(float) * frames);
//   } else {
//     // Average all channels per frame -> mono
//     for (unsigned int i = 0; i < frames; ++i) {
//       const float* f = interleaved + i * channels;
//       float acc = 0.0f;
//       for (unsigned int c = 0; c < channels; ++c) acc += f[c];
//       _scratch[int(i)] = acc / float(channels);
//     }
//   }

//   // Emit to processing thread (Qt queues across threads automatically).
//   emit framesReady(_scratch);  // QVector<float> copy-on-write -> copied to receiver
// }
