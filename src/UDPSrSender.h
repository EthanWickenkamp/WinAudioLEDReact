#pragma once
#include <QObject>
#include <QVector>
#include <QUdpSocket>
#include <QHostAddress>
#include <QElapsedTimer>
#include <algorithm>  // std::clamp
#include <cmath>      // std::lround
#include <cstdint>    // uint8_t, uint16_t

#pragma pack(push, 1)
struct SrV2Packet {
  char    header[6] = "00002";   // 6 bytes, last is '\0'
  uint8_t pressure[2]{};         // unused -> 0
  float   sampleRaw{};           // 0..255 (float)
  float   sampleSmth{};          // 0..255 (float)
  uint8_t samplePeak{};          // 0/1
  uint8_t frameCounter{};        // rolls over
  uint8_t fftResult[16]{};       // 16 bins, 0..255
  uint16_t zeroCrossingCount{};  // optional
  float   FFT_Magnitude{};       // optional
  float   FFT_MajorPeak{};       // optional Hz
};
#pragma pack(pop)
static_assert(sizeof(SrV2Packet) == 44, "SR v2 packet must be 44 bytes");

class UdpSrSender : public QObject {
  Q_OBJECT
public:
  explicit UdpSrSender(QObject* parent=nullptr)
    : QObject(parent), _sock(new QUdpSocket(this)) {
    _throttle.start();           // default: 50 FPS throttle (>=20 ms)
  }

  void setTarget(const QHostAddress& ip, quint16 port=11988) {
    _dst = ip; _port = port;
  }
  // For multicast instead:
  //   setTarget(QHostAddress("239.0.0.1"), 11988);

public slots:
  // Call with normalized bins (0..1).
  void sendFromBins(const QVector<float>& bins) {
    if (_throttle.elapsed() < 20) return;   // send ≤ 50 FPS
    _throttle.restart();
    if (_dst.isNull()) return;

    SrV2Packet p{};  // header already "00002", rest zero

    // Simple overall level from mean of bins -> 0..255
    // --- overall energy from bins (mean 0..1) ---
    float mean = 0.0f;
    for (float v : bins) mean += v;
    mean = bins.isEmpty() ? 0.f : mean / float(bins.size());

    // Fast/slow AGC
    _fast = _fastA*_fast + (1.0f - _fastA)*mean;
    _slow = _slowA*_slow + (1.0f - _slowA)*mean;

    // Ratio -> dB, map -6..+12 dB -> 0..1
    float ratio = _fast / std::max(_slow, 1e-6f);
    float rdb   = 10.0f * std::log10(std::max(ratio, 1e-6f));
    float v01   = std::clamp( (rdb + 6.0f) / 18.0f, 0.0f, 1.0f );

    // Fill packet levels (0..255 float)
    p.sampleRaw  = std::clamp(mean, 0.0f, 1.0f) * 255.0f;  // raw mean (optional)
    p.sampleSmth = v01 * 255.0f;                           // AGC’d, stable
    p.samplePeak = (rdb > 9.0f) ? 1 : 0;                   // simple peak flag

    p.frameCounter  = _frame++;

    // Map N arbitrary bins -> 16 bins (average per segment), 0..255
    const int N = bins.size();
    for (int i = 0; i < 16; ++i) {
      if (N == 0) { p.fftResult[i] = 0; continue; }
      int k0 = (i * N) / 16;
      int k1 = ((i + 1) * N) / 16 - 1;
      if (k1 < k0) k1 = k0;
      double acc = 0.0; int cnt = 0;
      for (int k = k0; k <= k1; ++k) {
        acc += std::clamp(bins[k], 0.0f, 1.0f);
        ++cnt;
      }
      float avg = cnt ? float(acc / cnt) : 0.f;
      p.fftResult[i] = static_cast<uint8_t>(std::lround(avg * 255.0f));
    }

    _sock->writeDatagram(reinterpret_cast<const char*>(&p), sizeof(p), _dst, _port);
  }

private:
  QUdpSocket*   _sock;
  QHostAddress  _dst;
  quint16       _port{11988};
  quint8        _frame{0};
  QElapsedTimer _throttle;
  float _fast{0.0f}, _slow{1e-3f};
  float _fastA{0.4f}, _slowA{0.98f};
};
