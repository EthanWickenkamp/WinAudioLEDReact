#include "AdvancedAudioProcessor.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <numeric>

AdvancedAudioProcessor::AdvancedAudioProcessor(QObject* parent) : QObject(parent) {}

AdvancedAudioProcessor::~AdvancedAudioProcessor() {
  cleanup();
}

void AdvancedAudioProcessor::cleanup() {
  if (_harmonicCfg) { kiss_fftr_free(_harmonicCfg); _harmonicCfg = nullptr; }
  if (_bassCfg) { kiss_fftr_free(_bassCfg); _bassCfg = nullptr; }
  if (_percCfg) { kiss_fftr_free(_percCfg); _percCfg = nullptr; }
  if (_macroCfg) { kiss_fftr_free(_macroCfg); _macroCfg = nullptr; }
}

void AdvancedAudioProcessor::start() {
  _stop.store(false);
  _initialized = false;
  cleanup();
  _fifoL.clear();
  _fifoR.clear();
  _frameCount = 0;
}

void AdvancedAudioProcessor::requestStop() {
  _stop.store(true);
  cleanup();
  _fifoL.clear();
  _fifoR.clear();
}

void AdvancedAudioProcessor::onFrames(const QVector<float>& left, const QVector<float>& right) {
  if (_stop.load() || left.isEmpty() || right.isEmpty()) return;

  int n = std::min(left.size(), right.size());
  if (n <= 0) return;

  // Append to FIFOs
  const int prevL = _fifoL.size();
  _fifoL.reserve(prevL + n);
  _fifoL.resize(prevL + n);
  std::memcpy(_fifoL.data() + prevL, left.constData(), n * sizeof(float));

  const int prevR = _fifoR.size();
  _fifoR.reserve(prevR + n);
  _fifoR.resize(prevR + n);
  std::memcpy(_fifoR.data() + prevR, right.constData(), n * sizeof(float));

  processMultiResolution();
}

void AdvancedAudioProcessor::initialize() {
  if (_initialized) return;

  // Initialize all FFT configurations
  _harmonicCfg = kiss_fftr_alloc(_harmonicN, 0, nullptr, nullptr);
  _bassCfg = kiss_fftr_alloc(_bassN, 0, nullptr, nullptr);
  _percCfg = kiss_fftr_alloc(_percN, 0, nullptr, nullptr);
  _macroCfg = kiss_fftr_alloc(_macroN, 0, nullptr, nullptr);

  if (!_harmonicCfg || !_bassCfg || !_percCfg || !_macroCfg) {
    qWarning("FFT allocation failed");
    return;
  }

  // Initialize all buffers
  setupBuffers();
  setupWindows();
  setupFrequencyBands();
  setupOnsetDetection();
  setupRhythmTracking();

  _initialized = true;
}

void AdvancedAudioProcessor::setupBuffers() {
  // Harmonic analysis (current system)
  _harmonicFrameL.assign(_harmonicN, 0.0f);
  _harmonicFrameR.assign(_harmonicN, 0.0f);
  _harmonicSpecL.assign(_harmonicN/2 + 1, kiss_fft_cpx{0,0});
  _harmonicSpecR.assign(_harmonicN/2 + 1, kiss_fft_cpx{0,0});
  _harmonicWindow.assign(_harmonicN, 0.0f);

  // Bass analysis (high frequency resolution)
  _bassFrameL.assign(_bassN, 0.0f);
  _bassFrameR.assign(_bassN, 0.0f);
  _bassSpecL.assign(_bassN/2 + 1, kiss_fft_cpx{0,0});
  _bassSpecR.assign(_bassN/2 + 1, kiss_fft_cpx{0,0});
  _bassWindow.assign(_bassN, 0.0f);

  // Percussive analysis (high time resolution)
  _percFrameL.assign(_percN, 0.0f);
  _percFrameR.assign(_percN, 0.0f);
  _percSpecL.assign(_percN/2 + 1, kiss_fft_cpx{0,0});
  _percSpecR.assign(_percN/2 + 1, kiss_fft_cpx{0,0});
  _percWindow.assign(_percN, 0.0f);

  // Macro analysis (long-term features)
  _macroFrameL.assign(_macroN, 0.0f);
  _macroFrameR.assign(_macroN, 0.0f);
  _macroSpecL.assign(_macroN/2 + 1, kiss_fft_cpx{0,0});
  _macroSpecR.assign(_macroN/2 + 1, kiss_fft_cpx{0,0});
  _macroWindow.assign(_macroN, 0.0f);
}

void AdvancedAudioProcessor::setupWindows() {
  // Hann windows for all analysis types
  auto createHannWindow = [](std::vector<float>& window, int N) {
    for (int n = 0; n < N; ++n) {
      window[n] = 0.5f * (1.0f - std::cos(2.0f * float(M_PI) * n / (N - 1)));
    }
  };

  createHannWindow(_harmonicWindow, _harmonicN);
  createHannWindow(_bassWindow, _bassN);
  createHannWindow(_percWindow, _percN);
  createHannWindow(_macroWindow, _macroN);
}

void AdvancedAudioProcessor::setupFrequencyBands() {
  // Bass bands: Ultra-high resolution 20-400Hz (16 bands)
  _bassBands.resize(16);
  setupBassFrequencyMapping();

  // Harmonic bands: Musical resolution 80Hz-18kHz (32 bands)
  _harmonicBands.resize(32);
  setupHarmonicFrequencyMapping();

  // Percussive bands: Transient detection (8 broad bands)
  _percBands.resize(8);
  setupPercussiveFrequencyMapping();

  // Macro bands: Long-term spectral evolution (12 bands)
  _macroBands.resize(12);
  setupMacroFrequencyMapping();

  // Chromagram: 12 pitch classes (C, C#, D, ...)
  _chromagram.resize(12);
  setupChromagramMapping();
}

void AdvancedAudioProcessor::setupBassFrequencyMapping() {
  // Ultra-high resolution bass: 20-400Hz split into 16 linear bands
  _bassKLo.resize(16);
  _bassKHi.resize(16);
  
  const float fMin = 20.0f;
  const float fMax = 400.0f;
  const float fStep = (fMax - fMin) / 16.0f;

  for (int i = 0; i < 16; ++i) {
    float f0 = fMin + i * fStep;
    float f1 = fMin + (i + 1) * fStep;
    _bassKLo[i] = std::max(1, int(f0 * _bassN / _sr));
    _bassKHi[i] = std::min(_bassN/2, int(f1 * _bassN / _sr));
  }
}

void AdvancedAudioProcessor::setupHarmonicFrequencyMapping() {
  // Logarithmic spacing for musical content
  _harmonicKLo.resize(32);
  _harmonicKHi.resize(32);
  
  const float fMin = 80.0f;
  const float fMax = 18000.0f;
  const float ratio = fMax / fMin;

  for (int i = 0; i < 32; ++i) {
    float t0 = float(i) / 32.0f;
    float t1 = float(i + 1) / 32.0f;
    float f0 = fMin * std::pow(ratio, t0);
    float f1 = fMin * std::pow(ratio, t1);
    _harmonicKLo[i] = std::max(1, int(f0 * _harmonicN / _sr));
    _harmonicKHi[i] = std::min(_harmonicN/2, int(f1 * _harmonicN / _sr));
  }
}

void AdvancedAudioProcessor::setupPercussiveFrequencyMapping() {
  // Broad bands for transient detection
  _percKLo.resize(8);
  _percKHi.resize(8);
  
  std::vector<float> edges = {0, 200, 500, 1000, 2000, 4000, 8000, 12000, 20000};
  
  for (int i = 0; i < 8; ++i) {
    float f0 = edges[i];
    float f1 = edges[i + 1];
    _percKLo[i] = std::max(1, int(f0 * _percN / _sr));
    _percKHi[i] = std::min(_percN/2, int(f1 * _percN / _sr));
  }
}

void AdvancedAudioProcessor::setupMacroFrequencyMapping() {
  // Coarse bands for long-term spectral evolution
  _macroKLo.resize(12);
  _macroKHi.resize(12);
  
  const float fMin = 50.0f;
  const float fMax = 16000.0f;
  const float ratio = fMax / fMin;

  for (int i = 0; i < 12; ++i) {
    float t0 = float(i) / 12.0f;
    float t1 = float(i + 1) / 12.0f;
    float f0 = fMin * std::pow(ratio, t0);
    float f1 = fMin * std::pow(ratio, t1);
    _macroKLo[i] = std::max(1, int(f0 * _macroN / _sr));
    _macroKHi[i] = std::min(_macroN/2, int(f1 * _macroN / _sr));
  }
}

void AdvancedAudioProcessor::setupChromagramMapping() {
  // Map harmonic spectrum to 12 pitch classes
  _chromaKLo.resize(12);
  _chromaKHi.resize(12);
  
  // A4 = 440Hz, 12-tone equal temperament
  const float A4 = 440.0f;
  
  for (int i = 0; i < 12; ++i) {
    // Collect all octaves of this pitch class
    _chromaKLo[i] = _harmonicN/2; // Will find minimum
    _chromaKHi[i] = 1;            // Will find maximum
    
    // Check multiple octaves
    for (int octave = 1; octave <= 7; ++octave) {
      float semitone = i - 9; // A=0, so C=-9, C#=-8, etc.
      float freq = A4 * std::pow(2.0f, octave + semitone/12.0f);
      if (freq > _sr/2) break;
      
      int k = int(freq * _harmonicN / _sr);
      if (k >= 1 && k < _harmonicN/2) {
        _chromaKLo[i] = std::min(_chromaKLo[i], k);
        _chromaKHi[i] = std::max(_chromaKHi[i], k + 2); // Include nearby bins
      }
    }
  }
}

void AdvancedAudioProcessor::setupOnsetDetection() {
  // Initialize onset detection buffers
  _prevSpecFlux.assign(4, 0.0f); // Track flux for different frequency bands
  _onsetStrength.assign(4, 0.0f);
  _prevPercMagnitudes.assign(_percN/2 + 1, 0.0f);
  _prevHarmonicMagnitudes.assign(_harmonicN/2 + 1, 0.0f);
  
  // Onset detection parameters
  _fluxThreshold = 0.1f;
  _onsetCooldown = 10; // Frames between onsets
  _onsetTimer = 0;
}

void AdvancedAudioProcessor::setupRhythmTracking() {
  // Simple beat tracking setup
  _beatHistory.assign(64, 0.0f); // 64 frames of beat strength
  _beatPhase = 0.0f;
  _beatPeriod = 120.0f; // BPM estimate
  _beatConfidence = 0.0f;
}

void AdvancedAudioProcessor::processMultiResolution() {
  initialize();
  if (!_initialized) return;

  // Only process when we have enough samples for the largest analysis
  while (!_stop.load() && _fifoL.size() >= _macroN && _fifoR.size() >= _macroN) {
    
    // 1. PERCUSSIVE ANALYSIS (highest time resolution)
    if (_frameCount % 1 == 0) { // Every frame
      analyzePercussive();
    }
    
    // 2. HARMONIC ANALYSIS (medium resolution)
    if (_frameCount % 2 == 0) { // Every 2nd frame
      analyzeHarmonic();
    }
    
    // 3. BASS ANALYSIS (high frequency resolution)
    if (_frameCount % 4 == 0) { // Every 4th frame
      analyzeBass();
    }
    
    // 4. MACRO ANALYSIS (long-term features)
    if (_frameCount % 8 == 0) { // Every 8th frame
      analyzeMacro();
    }
    
    // 5. MUSICAL FEATURE EXTRACTION
    extractMusicalFeatures();
    
    // 6. ONSET AND RHYTHM DETECTION
    detectOnsets();
    trackRhythm();
    
    // 7. EMIT ALL RESULTS
    emitAdvancedResults();
    
    // Advance FIFOs
    int hop = _harmonicN / 4; // 256 samples hop
    _fifoL.erase(_fifoL.begin(), _fifoL.begin() + hop);
    _fifoR.erase(_fifoR.begin(), _fifoR.begin() + hop);
    
    _frameCount++;
  }
}

void AdvancedAudioProcessor::analyzePercussive() {
  // Copy and window percussive frame
  std::memcpy(_percFrameL.data(), _fifoL.data(), _percN * sizeof(float));
  std::memcpy(_percFrameR.data(), _fifoR.data(), _percN * sizeof(float));
  
  applyDCRemoval(_percFrameL);
  applyDCRemoval(_percFrameR);
  
  for (int i = 0; i < _percN; ++i) {
    _percFrameL[i] *= _percWindow[i];
    _percFrameR[i] *= _percWindow[i];
  }
  
  // FFT
  kiss_fftr(_percCfg, _percFrameL.data(), _percSpecL.data());
  kiss_fftr(_percCfg, _percFrameR.data(), _percSpecR.data());
  
  // Compute percussive bands (sum both channels)
  for (int b = 0; b < 8; ++b) {
    float sum = 0.0f;
    for (int k = _percKLo[b]; k < _percKHi[b]; ++k) {
      sum += std::hypot(_percSpecL[k].r, _percSpecL[k].i);
      sum += std::hypot(_percSpecR[k].r, _percSpecR[k].i);
    }
    _percBands[b] = sum * 0.5f; // Average L/R
  }
}

void AdvancedAudioProcessor::analyzeHarmonic() {
  // Copy and window harmonic frame  
  std::memcpy(_harmonicFrameL.data(), _fifoL.data(), _harmonicN * sizeof(float));
  std::memcpy(_harmonicFrameR.data(), _fifoR.data(), _harmonicN * sizeof(float));
  
  applyDCRemoval(_harmonicFrameL);
  applyDCRemoval(_harmonicFrameR);
  
  for (int i = 0; i < _harmonicN; ++i) {
    _harmonicFrameL[i] *= _harmonicWindow[i];
    _harmonicFrameR[i] *= _harmonicWindow[i];
  }
  
  // FFT
  kiss_fftr(_harmonicCfg, _harmonicFrameL.data(), _harmonicSpecL.data());
  kiss_fftr(_harmonicCfg, _harmonicFrameR.data(), _harmonicSpecR.data());
  
  // Compute harmonic bands
  for (int b = 0; b < 32; ++b) {
    float sum = 0.0f;
    for (int k = _harmonicKLo[b]; k < _harmonicKHi[b]; ++k) {
      sum += std::hypot(_harmonicSpecL[k].r, _harmonicSpecL[k].i);
      sum += std::hypot(_harmonicSpecR[k].r, _harmonicSpecR[k].i);
    }
    _harmonicBands[b] = sum * 0.5f;
  }
}

void AdvancedAudioProcessor::analyzeBass() {
  // Copy and window bass frame
  std::memcpy(_bassFrameL.data(), _fifoL.data(), _bassN * sizeof(float));
  std::memcpy(_bassFrameR.data(), _fifoR.data(), _bassN * sizeof(float));
  
  applyDCRemoval(_bassFrameL);
  applyDCRemoval(_bassFrameR);
  
  for (int i = 0; i < _bassN; ++i) {
    _bassFrameL[i] *= _bassWindow[i];
    _bassFrameR[i] *= _bassWindow[i];
  }
  
  // FFT
  kiss_fftr(_bassCfg, _bassFrameL.data(), _bassSpecL.data());
  kiss_fftr(_bassCfg, _bassFrameR.data(), _bassSpecR.data());
  
  // Compute ultra-high resolution bass bands
  for (int b = 0; b < 16; ++b) {
    float sum = 0.0f;
    for (int k = _bassKLo[b]; k < _bassKHi[b]; ++k) {
      sum += std::hypot(_bassSpecL[k].r, _bassSpecL[k].i);
      sum += std::hypot(_bassSpecR[k].r, _bassSpecR[k].i);
    }
    _bassBands[b] = sum * 0.5f;
  }
}

void AdvancedAudioProcessor::analyzeMacro() {
  // Copy and window macro frame
  std::memcpy(_macroFrameL.data(), _fifoL.data(), _macroN * sizeof(float));
  std::memcpy(_macroFrameR.data(), _fifoR.data(), _macroN * sizeof(float));
  
  applyDCRemoval(_macroFrameL);
  applyDCRemoval(_macroFrameR);
  
  for (int i = 0; i < _macroN; ++i) {
    _macroFrameL[i] *= _macroWindow[i];
    _macroFrameR[i] *= _macroWindow[i];
  }
  
  // FFT
  kiss_fftr(_macroCfg, _macroFrameL.data(), _macroSpecL.data());
  kiss_fftr(_macroCfg, _macroFrameR.data(), _macroSpecR.data());
  
  // Compute macro bands for long-term evolution
  for (int b = 0; b < 12; ++b) {
    float sum = 0.0f;
    for (int k = _macroKLo[b]; k < _macroKHi[b]; ++k) {
      sum += std::hypot(_macroSpecL[k].r, _macroSpecL[k].i);
      sum += std::hypot(_macroSpecR[k].r, _macroSpecR[k].i);
    }
    _macroBands[b] = sum * 0.5f;
  }
}

void AdvancedAudioProcessor::extractMusicalFeatures() {
  // 1. CHROMAGRAM - Map harmonic content to 12 pitch classes
  std::fill(_chromagram.begin(), _chromagram.end(), 0.0f);
  
  for (int c = 0; c < 12; ++c) {
    for (int k = _chromaKLo[c]; k < _chromaKHi[c]; ++k) {
      if (k < _harmonicSpecL.size()) {
        _chromagram[c] += std::hypot(_harmonicSpecL[k].r, _harmonicSpecL[k].i);
        _chromagram[c] += std::hypot(_harmonicSpecR[k].r, _harmonicSpecR[k].i);
      }
    }
    _chromagram[c] *= 0.5f; // Average L/R
  }
  
  // 2. SPECTRAL FEATURES
  computeSpectralFeatures();
  
  // 3. HARMONIC-PERCUSSIVE RATIO
  computeHarmonicPercussiveRatio();
}

void AdvancedAudioProcessor::computeSpectralFeatures() {
  // Spectral Centroid (brightness)
  float weightedSum = 0.0f;
  float magnitudeSum = 0.0f;
  
  for (int k = 1; k < _harmonicN/2; ++k) {
    float freq = float(k) * _sr / _harmonicN;
    float magL = std::hypot(_harmonicSpecL[k].r, _harmonicSpecL[k].i);
    float magR = std::hypot(_harmonicSpecR[k].r, _harmonicSpecR[k].i);
    float mag = (magL + magR) * 0.5f;
    
    weightedSum += freq * mag;
    magnitudeSum += mag;
  }
  
  _spectralCentroid = (magnitudeSum > 0) ? weightedSum / magnitudeSum : 0.0f;
  
  // Spectral Rolloff (90% of energy cutoff)
  float energySum = 0.0f;
  for (int k = 1; k < _harmonicN/2; ++k) {
    float magL = std::hypot(_harmonicSpecL[k].r, _harmonicSpecL[k].i);
    float magR = std::hypot(_harmonicSpecR[k].r, _harmonicSpecR[k].i);
    energySum += (magL + magR) * 0.5f;
  }
  
  float targetEnergy = energySum * 0.9f;
  float runningEnergy = 0.0f;
  _spectralRolloff = 0.0f;
  
  for (int k = 1; k < _harmonicN/2; ++k) {
    float magL = std::hypot(_harmonicSpecL[k].r, _harmonicSpecL[k].i);
    float magR = std::hypot(_harmonicSpecR[k].r, _harmonicSpecR[k].i);
    runningEnergy += (magL + magR) * 0.5f;
    
    if (runningEnergy >= targetEnergy) {
      _spectralRolloff = float(k) * _sr / _harmonicN;
      break;
    }
  }
  
  // Zero Crossing Rate (on time domain)
  int crossings = 0;
  for (int n = 1; n < _harmonicN; ++n) {
    if ((_harmonicFrameL[n-1] >= 0) != (_harmonicFrameL[n] >= 0) ||
        (_harmonicFrameR[n-1] >= 0) != (_harmonicFrameR[n] >= 0)) {
      crossings++;
    }
  }
  _zeroCrossingRate = float(crossings) / float(_harmonicN);
}

void AdvancedAudioProcessor::computeHarmonicPercussiveRatio() {
  // Simple ratio of harmonic vs percussive energy
  float harmonicEnergy = std::accumulate(_harmonicBands.begin(), _harmonicBands.end(), 0.0f);
  float percussiveEnergy = std::accumulate(_percBands.begin(), _percBands.end(), 0.0f);
  
  _harmonicPercussiveRatio = (percussiveEnergy > 0) ? harmonicEnergy / percussiveEnergy : 1.0f;
}

void AdvancedAudioProcessor::detectOnsets() {
  if (_onsetTimer > 0) _onsetTimer--;
  
  // Spectral Flux calculation for onset detection
  float totalFlux = 0.0f;
  
  // High-frequency flux (good for detecting hi-hats, cymbals)
  float hfFlux = 0.0f;
  for (int k = _percN/4; k < _percN/2; ++k) { // Upper half of spectrum
    float currentMag = std::hypot(_percSpecL[k].r, _percSpecL[k].i) + 
                       std::hypot(_percSpecR[k].r, _percSpecR[k].i);
    float prevMag = (k < _prevPercMagnitudes.size()) ? _prevPercMagnitudes[k] : 0.0f;
    float diff = currentMag - prevMag;
    if (diff > 0) hfFlux += diff; // Only positive differences
  }
  
  // Low-frequency flux (good for kicks, bass)
  float lfFlux = 0.0f;
  for (int k = 1; k < _percN/8; ++k) { // Lower portion of spectrum
    float currentMag = std::hypot(_percSpecL[k].r, _percSpecL[k].i) + 
                       std::hypot(_percSpecR[k].r, _percSpecR[k].i);
    float prevMag = (k < _prevPercMagnitudes.size()) ? _prevPercMagnitudes[k] : 0.0f;
    float diff = currentMag - prevMag;
    if (diff > 0) lfFlux += diff;
  }
  
  totalFlux = hfFlux + lfFlux;
  
  // Update previous magnitudes
  _prevPercMagnitudes.resize(_percN/2 + 1);
  for (int k = 0; k < _percN/2 + 1; ++k) {
    _prevPercMagnitudes[k] = std::hypot(_percSpecL[k].r, _percSpecL[k].i) + 
                             std::hypot(_percSpecR[k].r, _percSpecR[k].i);
  }
  
  // Store flux values
  _onsetStrength[0] = totalFlux;
  _onsetStrength[1] = hfFlux;
  _onsetStrength[2] = lfFlux;
  _onsetStrength[3] = 0.0f; // Reserved for additional onset types
  
  // Simple onset detection
  _isOnset = false;
  if (_onsetTimer == 0 && totalFlux > _fluxThreshold) {
    _isOnset = true;
    _onsetTimer = _onsetCooldown;
  }
}

void AdvancedAudioProcessor::trackRhythm() {
  // Simple beat tracking using onset strength
  float currentBeat = _onsetStrength[0] + _onsetStrength[2]; // Total + bass emphasis
  
  // Update beat history (circular buffer)
  static int beatIndex = 0;
  _beatHistory[beatIndex] = currentBeat;
  beatIndex = (beatIndex + 1) % _beatHistory.size();
  
  // Simple tempo estimation (very basic)
  // Look for peaks in autocorrelation of beat history
  float maxCorr = 0.0f;
  int bestPeriod = 30; // Default ~120 BPM at current frame rate
  
  for (int period = 20; period < 60; ++period) { // 60-200 BPM range
    float corr = 0.0f;
    for (int i = 0; i < _beatHistory.size() - period; ++i) {
      corr += _beatHistory[i] * _beatHistory[i + period];
    }
    if (corr > maxCorr) {
      maxCorr = corr;
      bestPeriod = period;
    }
  }
  
  // Update beat tracking
  _beatPeriod = bestPeriod;
  _beatPhase = fmod(_beatPhase + 1.0f, _beatPeriod);
  _beatConfidence = maxCorr / (_beatHistory.size() * 0.1f); // Normalize roughly
  _beatConfidence = std::clamp(_beatConfidence, 0.0f, 1.0f);
}

void AdvancedAudioProcessor::applyDCRemoval(std::vector<float>& frame) {
  // Simple DC removal: subtract the mean
  float mean = std::accumulate(frame.begin(), frame.end(), 0.0f) / frame.size();
  for (float &sample : frame) {
    sample -= mean;
  }
}

void AdvancedAudioProcessor::emitAdvancedResults() {
  // Convert std::vectors to QVectors for Qt signals
  QVector<float> bassBands(_bassBands.begin(), _bassBands.end());
  QVector<float> harmonicBands(_harmonicBands.begin(), _harmonicBands.end());
  QVector<float> percBands(_percBands.begin(), _percBands.end());
  QVector<float> macroBands(_macroBands.begin(), _macroBands.end());
  QVector<float> chromagram(_chromagram.begin(), _chromagram.end());
  QVector<float> onsetStrength(_onsetStrength.begin(), _onsetStrength.end());
  
  // Emit all the different analysis results
  emit bassAnalysisReady(bassBands);           // 16 ultra-high-res bass bands
  emit harmonicAnalysisReady(harmonicBands);   // 32 musical bands
  emit percussiveAnalysisReady(percBands);     // 8 transient detection bands
  emit macroAnalysisReady(macroBands);         // 12 long-term evolution bands
  emit chromagramReady(chromagram);            // 12 pitch classes (musical notes)
  emit onsetDetectionReady(onsetStrength, _isOnset); // Onset detection results
  
  // Spectral features
  emit spectralFeaturesReady(_spectralCentroid, _spectralRolloff, _zeroCrossingRate);
  
  // Musical analysis
  emit musicalFeaturesReady(_harmonicPercussiveRatio, _beatConfidence, _beatPeriod);
  
  // Beat tracking
  emit beatTrackingReady(_beatPhase, _beatPeriod, _beatConfidence);
  
  // Comprehensive multi-band analysis (for visualizers)
  MultiResolutionData data;
  data.bass = bassBands;
  data.harmonic = harmonicBands;
  data.percussive = percBands;
  data.macro = macroBands;
  data.chromagram = chromagram;
  data.onsetStrength = onsetStrength;
  data.isOnset = _isOnset;
  data.spectralCentroid = _spectralCentroid;
  data.spectralRolloff = _spectralRolloff;
  data.zeroCrossingRate = _zeroCrossingRate;
  data.harmonicPercussiveRatio = _harmonicPercussiveRatio;
  data.beatPhase = _beatPhase;
  data.beatPeriod = _beatPeriod;
  data.beatConfidence = _beatConfidence;
  data.frameCount = _frameCount;
  
  emit multiResolutionAnalysisReady(data);
}