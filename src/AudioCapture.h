#pragma once
#include <QObject>
#include <QVector>
#include <QString>
#include <atomic>
#include "miniaudio.h"

// Forward-declare to avoid pulling in the full miniaudio header.
struct ma_context;
struct ma_device;
//class QString;

class AudioCapture : public QObject {
    Q_OBJECT
public:
    explicit AudioCapture(QObject* parent = nullptr);
    ~AudioCapture();

public slots:
    // state management
    void start();        // create context, open WASAPI loopback, start stream
    void requestStop();  // stop & free everything

signals:
    // state management
    void status(const QString& msg);
    void stopped();
    // ---- Active path: emit R and L frames to processor ----
    void framesReady(const QVector<float>& left, const QVector<float>& right);
    void deviceSampleRateChanged(int sampleRate);

private:
    // state management
    std::atomic<bool> _running{false};
    void cleanup();
    // Audio callback (static because C API)
    static void dataCallback(ma_device* dev, void* pOutput, const void* pInput, ma_uint32 frameCount);

    void emitStereo(const float* interleaved, unsigned frames, unsigned channels);
    // Mono downmix and emit
    // void emitMono(const float* in, unsigned int frames, unsigned int channels);

    ma_context* _ctx{nullptr};
    ma_device*  _dev{nullptr};

    // Reuse this buffer to avoid allocating every callback
    // Preallocated memory to use each callback
    QVector<float> _scratchL;
    QVector<float> _scratchR;
    //QVector<float> _scratchMono; // if you want mono output




};
