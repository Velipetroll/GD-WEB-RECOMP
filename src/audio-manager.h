#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <unordered_map>

class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    void init();

    void startMusic();
    void stopMusic();
    void pauseMusic();
    void resumeMusic();

    void fadeInMusic(float durationMs = 1000.0f);
    void fadeOutMusic(float durationMs = 1500.0f);

    float getUserMusicVolume() const;
    void setUserMusicVolume(float vol);
    float getMusicVolume() const;
    void setMusicVolume(float vol);

    float getSfxVolume() const;
    void setSfxVolume(float vol);

    void playEffect(const std::string& effectKey, float volume = 1.0f);

    void update(float dt);
    float getMeteringValue() const;
    void reset();

private:
    float _effectiveVolume() const;

    float _userMusicVol = 1.0f;
    float _sfxVolume = 1.0f;

    bool _isFadingIn = false;
    bool _isFadingOut = false;
    float _fadeTime = 0.0f;
    float _fadeDuration = 1.0f;

    float _meterValue = 0.1f;
    float _lastAudio = 0.1f;
    float _lastPeak = 0.0f;
    int _silenceCounter = 0;

    std::vector<float> _peakEnvelope;

    bool _initialized = false;
    bool _musicLoaded = false;

    void* _engine = nullptr;
    void* _musicSound = nullptr;

    std::unordered_map<std::string, void*> _sfxSounds;
};
