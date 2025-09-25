#pragma once
#include <QObject>
#include <QVector>
#include <atomic>
#include <vector>

extern "C" {
  #include "kiss_fftr.h"
}

// Comprehensive data structure for multi-resolution analysis
struct MultiResolutionData {
  QVector<float> bass;                    // 16 ultra-high-res bass bands (20-400Hz)
  QVector<float> harmonic;                // 32 musical bands (80Hz-18kHz)
  QVector<float> percussive;              // 8 transient detection bands
  QVector<float> macro;                   // 12 long-term evolution bands
  QVector<float> chromagram;              // 12 pitch classes (C, C#, D, ...)
  QVector<float> onsetStrength;           // 4 onset detection features
  bool isOnset;                           // Onset detected this frame
  float spectralCentroid;                 // Brightness (Hz)
  float spectralRolloff;                  // 90% energy cutoff (Hz)
  float zeroCrossingRate;                 // Noisiness measure
  float harmonicPercussiveRatio;          // Tonal vs rhythmic content
  float beatPhase;                        // Current beat phase (0-1)
  float beatPeriod;                       // Beat period (frames)
  float beatConfidence;                   // Beat tracking confidence (0-1)
  int frameCount;                         // Frame number
};

class AdvancedAudioProcessor : public QObject {
  Q_OBJECT
public:
  explicit AdvancedAudioProcessor(QObject* parent = nullptr);
  ~AdvancedAudioProcessor();

  // Configuration
  void setSampleRate(int sr) { 
    if (sr != _sr) {
      _sr = sr; 
      _initialized = false;
    }
  }

public slots:
  void start();
  void requestStop();
  void onFrames(const QVector<float>& left, const QVector<float>& right);

signals:
  // Individual analysis outputs
  void bassAnalysisReady(const QVector<float>& bands);           // 16 bass bands (20-400Hz)
  void harmonicAnalysisReady(const QVector<float>& bands);       // 32 harmonic bands (80Hz-18kHz)
  void percussiveAnalysisReady(const QVector<float>& bands);     // 8 percussive bands
  void macroAnalysisReady(const QVector<float>& bands);          // 12 macro bands
  void chromagramReady(const QVector<float>& chroma);            // 12 pitch classes
  void onsetDetectionReady(const QVector<float>& strength, bool isOnset);
  
  // Spectral features
  void spectralFeaturesReady(float centroid, float rolloff, float zcr);
  
  // Musical features  
  void musicalFeaturesReady(float hpRatio, float beatConf, float beatPeriod);
  
  // Beat tracking
  void beatTrackingReady(float phase, float period, float confidence);
  
  // Comprehensive output
  void multiResolutionAnalysisReady(const MultiResolutionData& data);
  
  void stopped();

private:
  // Core parameters
  int _sr = 48000;                        // Sample rate
  std::atomic<bool> _stop{false};
  bool _initialized = false;
  int _frameCount = 0;

  // Multi-resolution FFT sizes
  static constexpr int _bassN = 4096;     // Ultra-high freq resolution (~85ms)
  static constexpr int _harmonicN = 1024; // Musical resolution (~21ms)  
  static constexpr int _percN = 256;      // High time resolution (~5ms)
  static constexpr int _macroN = 8192;    // Long-term analysis (~170ms)

  // Audio input buffers
  std::vector<float> _fifoL;              // Left channel FIFO
  std::vector<float> _fifoR;              // Right channel FIFO

  // === BASS ANALYSIS (Ultra-high frequency resolution) ===
  kiss_fftr_cfg _bassCfg = nullptr;
  std::vector<float> _bassFrameL;         // Bass analysis frame L
  std::vector<float> _bassFrameR;         // Bass analysis frame R
  std::vector<kiss_fft_cpx> _bassSpecL;   // Bass spectrum L
  std::vector<kiss_fft_cpx> _bassSpecR;   // Bass spectrum R
  std::vector<float> _bassWindow;         // Bass window function
  std::vector<int> _bassKLo, _bassKHi;    // Bass frequency bin ranges
  std::vector<float> _bassBands;          // 16 bass bands (20-400Hz)

  // === HARMONIC ANALYSIS (Musical resolution) ===
  kiss_fftr_cfg _harmonicCfg = nullptr;
  std::vector<float> _harmonicFrameL;     // Harmonic analysis frame L
  std::vector<float> _harmonicFrameR;     // Harmonic analysis frame R  
  std::vector<kiss_fft_cpx> _harmonicSpecL; // Harmonic spectrum L
  std::vector<kiss_fft_cpx> _harmonicSpecR; // Harmonic spectrum R
  std::vector<float> _harmonicWindow;     // Harmonic window function
  std::vector<int> _harmonicKLo, _harmonicKHi; // Harmonic frequency bin ranges
  std::vector<float> _harmonicBands;      // 32 harmonic bands (80Hz-18kHz)

  // === PERCUSSIVE ANALYSIS (High time resolution) ===
  kiss_fftr_cfg _percCfg = nullptr;
  std::vector<float> _percFrameL;         // Percussive analysis frame L
  std::vector<float> _percFrameR;         // Percussive analysis frame R
  std::vector<kiss_fft_cpx> _percSpecL;   // Percussive spectrum L
  std::vector<kiss_fft_cpx> _percSpecR;   // Percussive spectrum R
  std::vector<float> _percWindow;         // Percussive window function
  std::vector<int> _percKLo, _percKHi;    // Percussive frequency bin ranges
  std::vector<float> _percBands;          // 8 percussive bands

  // === MACRO ANALYSIS (Long-term features) ===
  kiss_fftr_cfg _macroCfg = nullptr;
  std::vector<float> _macroFrameL;        // Macro analysis frame L
  std::vector<float> _macroFrameR;        // Macro analysis frame R
  std::vector<kiss_fft_cpx> _macroSpecL;  // Macro spectrum L
  std::vector<kiss_fft_cpx> _macroSpecR;  // Macro spectrum R
  std::vector<float> _macroWindow;        // Macro window function
  std::vector<int> _macroKLo, _macroKHi;  // Macro frequency bin ranges
  std::vector<float> _macroBands;         // 12 macro bands

  // === MUSICAL FEATURE EXTRACTION ===
  std::vector<float> _chromagram;         // 12 pitch classes (C, C#, D, ...)
  std::vector<int> _chromaKLo, _chromaKHi; // Chromagram frequency mappings
  float _spectralCentroid = 0.0f;         // Brightness measure
  float _spectralRolloff = 0.0f;          // 90% energy cutoff frequency  
  float _zeroCrossingRate = 0.0f;         // Zero crossing rate
  float _harmonicPercussiveRatio = 0.0f;  // Harmonic vs percussive content

  // === ONSET DETECTION ===
  std::vector<float> _onsetStrength;      // Onset strength per band
  std::vector<float> _prevSpecFlux;       // Previous spectral flux
  std::vector<float> _prevPercMagnitudes; // Previous percussive magnitudes
  std::vector<float> _prevHarmonicMagnitudes; // Previous harmonic magnitudes
  bool _isOnset = false;                  // Onset detected this frame
  float _fluxThreshold = 0.1f;            // Onset detection threshold
  int _onsetCooldown = 10;                // Frames between onsets
  int _onsetTimer = 0;                    // Cooldown timer

  // === RHYTHM TRACKING ===
  std::vector<float> _beatHistory;        // Beat strength history
  float _beatPhase = 0.0f;                // Current beat phase
  float _beatPeriod = 120.0f;             // Estimated beat period (frames)
  float _beatConfidence = 0.0f;           // Beat tracking confidence

  // === CORE PROCESSING METHODS ===
  void initialize();                      // Main initialization
  void cleanup();                         // Resource cleanup
  void processMultiResolution();          // Main processing loop

  // === SETUP METHODS ===
  void setupBuffers();                    // Allocate all buffers
  void setupWindows();                    // Create window functions
  void setupFrequencyBands();             // Setup all frequency mappings
  void setupBassFrequencyMapping();       // Bass band mapping (20-400Hz)
  void setupHarmonicFrequencyMapping();   // Harmonic band mapping (80Hz-18kHz)
  void setupPercussiveFrequencyMapping(); // Percussive band mapping
  void setupMacroFrequencyMapping();      // Macro band mapping
  void setupChromagramMapping();          // Pitch class mapping
  void setupOnsetDetection();             // Onset detection initialization
  void setupRhythmTracking();             // Rhythm tracking initialization

  // === ANALYSIS METHODS ===
  void analyzePercussive();               // High time-resolution analysis
  void analyzeHarmonic();                 // Musical content analysis
  void analyzeBass();                     // Ultra-high frequency resolution
  void analyzeMacro();                    // Long-term spectral evolution

  // === FEATURE EXTRACTION ===
  void extractMusicalFeatures();          // Musical feature computation
  void computeSpectralFeatures();         // Spectral characteristics
  void computeHarmonicPercussiveRatio();  // H/P ratio computation
  void detectOnsets();                    // Onset detection algorithm
  void trackRhythm();                     // Beat tracking algorithm

  // === UTILITY METHODS ===
  void applyDCRemoval(std::vector<float>& frame); // DC removal filter
  void emitAdvancedResults();             // Emit all analysis results
};