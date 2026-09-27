#include "audio-manager.h"
#include <iostream>
#include <algorithm>
#include <cmath>

// Enable native OGG Vorbis decoding in miniaudio via stb_vorbis declarations
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// stb_vorbis implementation
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

AudioManager::AudioManager() {
    init();
}

AudioManager::~AudioManager() {
    if (_initialized) {
        if (_musicLoaded && _musicSound) {
            ma_sound* snd = static_cast<ma_sound*>(_musicSound);
            ma_sound_uninit(snd);
            delete snd;
        }

        for (auto& pair : _sfxSounds) {
            ma_sound* sfx = static_cast<ma_sound*>(pair.second);
            ma_sound_uninit(sfx);
            delete sfx;
        }
        _sfxSounds.clear();

        if (_engine) {
            ma_engine* eng = static_cast<ma_engine*>(_engine);
            ma_engine_uninit(eng);
            delete eng;
        }
    }
}

void AudioManager::init() {
    if (_initialized) return;

    ma_engine* eng = new ma_engine();
    ma_result result = ma_engine_init(NULL, eng);
    if (result != MA_SUCCESS) {
        std::cerr << "[audio-manager] Failed to initialize miniaudio." << std::endl;
        delete eng;
        return;
    }
    _engine = eng;

    std::vector<std::string> sfxList = {
        "explode_11", "endStart_02", "playSound_01", "quitSound_01", "highscoreGet02"
    };

    for (const auto& key : sfxList) {
        std::string path = "assets/" + key + ".ogg";
        ma_sound* sfx = new ma_sound();

        ma_result res = ma_sound_init_from_file(eng, path.c_str(), MA_SOUND_FLAG_DECODE, NULL, NULL, sfx);
        if (res == MA_SUCCESS) {
            _sfxSounds[key] = sfx;
            std::cout << "[audio-manager] Loaded SFX: " << path << std::endl;
        } else {
            std::cerr << "[audio-manager] Failed to load SFX: " << path << " (Code: " << res << ")" << std::endl;
            delete sfx;
        }
    }

    ma_sound* snd = new ma_sound();
    result = ma_sound_init_from_file(eng, "assets/StereoMadness.mp3", MA_SOUND_FLAG_STREAM, NULL, NULL, snd);
    if (result == MA_SUCCESS) {
        ma_sound_set_looping(snd, MA_TRUE);
        _musicSound = snd;
        _musicLoaded = true;
    } else {
        std::cerr << "[audio-manager] Could not find assets/StereoMadness.mp3" << std::endl;
        delete snd;
        return;
    }

    _peakEnvelope.clear();
    ma_decoder_config decConfig = ma_decoder_config_init(ma_format_f32, 1, 44100);
    ma_decoder decoder;
    if (ma_decoder_init_file("assets/StereoMadness.mp3", &decConfig, &decoder) == MA_SUCCESS) {
        float temp[1024];
        ma_uint64 readCount = 0;
        while (ma_decoder_read_pcm_frames(&decoder, temp, 1024, &readCount) == MA_SUCCESS && readCount > 0) {
            float peak = 0.0f;
            for (ma_uint64 i = 0; i < readCount; ++i) {
                float val = std::abs(temp[i]);
                if (val > peak) peak = val;
            }
            _peakEnvelope.push_back(peak);
        }
        ma_decoder_uninit(&decoder);
        std::cout << "[audio-manager] Peak analysis completed: " << _peakEnvelope.size() << " blocks." << std::endl;
    }

    _initialized = true;
}

float AudioManager::_effectiveVolume() const {
    return 0.8f * _userMusicVol;
}

void AudioManager::startMusic() {
    if (!_initialized || !_musicLoaded || !_musicSound) return;
    ma_sound* snd = static_cast<ma_sound*>(_musicSound);
    ma_sound_seek_to_pcm_frame(snd, 0);
    ma_sound_set_volume(snd, _effectiveVolume());
    ma_sound_start(snd);
    _isFadingIn = false;
    _isFadingOut = false;
}

void AudioManager::stopMusic() {
    if (!_initialized || !_musicLoaded || !_musicSound) return;
    ma_sound* snd = static_cast<ma_sound*>(_musicSound);
    ma_sound_stop(snd);
    ma_sound_seek_to_pcm_frame(snd, 0);
}

void AudioManager::pauseMusic() {
    if (_initialized && _musicLoaded && _musicSound) {
        ma_sound_stop(static_cast<ma_sound*>(_musicSound));
    }
}

void AudioManager::resumeMusic() {
    if (_initialized && _musicLoaded && _musicSound) {
        ma_sound_start(static_cast<ma_sound*>(_musicSound));
    }
}

void AudioManager::fadeInMusic(float durationMs) {
    startMusic();
    if (_musicLoaded && _musicSound) {
        ma_sound_set_volume(static_cast<ma_sound*>(_musicSound), 0.0f);
    }
    _fadeDuration = durationMs / 1000.0f;
    _fadeTime = 0.0f;
    _isFadingIn = true;
    _isFadingOut = false;
}

void AudioManager::fadeOutMusic(float durationMs) {
    _fadeDuration = durationMs / 1000.0f;
    _fadeTime = 0.0f;
    _isFadingOut = true;
    _isFadingIn = false;
}

float AudioManager::getUserMusicVolume() const { return _userMusicVol; }

void AudioManager::setUserMusicVolume(float vol) {
    _userMusicVol = vol;
    if (_musicLoaded && _musicSound) {
        ma_sound_set_volume(static_cast<ma_sound*>(_musicSound), _effectiveVolume());
    }
}

float AudioManager::getMusicVolume() const { return _effectiveVolume(); }
void AudioManager::setMusicVolume(float vol) { setUserMusicVolume(vol / 0.8f); }

float AudioManager::getSfxVolume() const { return _sfxVolume; }
void AudioManager::setSfxVolume(float vol) { _sfxVolume = vol; }

void AudioManager::playEffect(const std::string& effectKey, float volume) {
    if (!_initialized) return;

    std::string key = effectKey;
    size_t extPos = key.find(".ogg");
    if (extPos != std::string::npos) key = key.substr(0, extPos);

    auto it = _sfxSounds.find(key);
    if (it != _sfxSounds.end()) {
        ma_sound* snd = static_cast<ma_sound*>(it->second);
        ma_sound_stop(snd);
        ma_sound_seek_to_pcm_frame(snd, 0);
        ma_sound_set_volume(snd, volume * _sfxVolume);
        ma_sound_start(snd);
    } else {
        std::string path = "assets/" + key + ".ogg";
        ma_engine_play_sound(static_cast<ma_engine*>(_engine), path.c_str(), NULL);
    }
}

void AudioManager::update(float dt) {
    if (_isFadingIn && _musicLoaded && _musicSound) {
        _fadeTime += dt;
        float t = std::min(_fadeTime / _fadeDuration, 1.0f);
        ma_sound_set_volume(static_cast<ma_sound*>(_musicSound), _effectiveVolume() * t);
        if (t >= 1.0f) _isFadingIn = false;
    } else if (_isFadingOut && _musicLoaded && _musicSound) {
        _fadeTime += dt;
        float t = std::min(_fadeTime / _fadeDuration, 1.0f);
        ma_sound_set_volume(static_cast<ma_sound*>(_musicSound), _effectiveVolume() * (1.0f - t));
        if (t >= 1.0f) {
            _isFadingOut = false;
            stopMusic();
        }
    }

    float num32 = 0.0f;
    if (_initialized && _musicLoaded && _musicSound && !_peakEnvelope.empty()) {
        ma_sound* snd = static_cast<ma_sound*>(_musicSound);
        if (ma_sound_is_playing(snd)) {
            float cursorSeconds = 0.0f;
            if (ma_sound_get_cursor_in_seconds(snd, &cursorSeconds) == MA_SUCCESS) {
                size_t idx = (size_t)(cursorSeconds * (44100.0f / 1024.0f));
                if (idx < _peakEnvelope.size()) {
                    num32 = _peakEnvelope[idx];
                }
            }
        }
    }

    float effVol = _effectiveVolume();
    if (effVol > 0.0f) num32 /= effVol;
    if (num32 > 1.0f) num32 = 1.0f;

    _meterValue = 0.1f + num32;

    float dt60 = 60.0f * dt;
    float p092, p146;
    if (std::abs(dt60 - 1.0f) < 0.02f) {
        p092 = 0.92f;
        p146 = 1.46f;
    } else if (dt60 < 0.1f) {
        p092 = 1.0f - 0.08338f * dt60;
        p146 = 1.0f + 0.37843f * dt60;
    } else {
        p092 = std::pow(0.92f, dt60);
        p146 = std::pow(1.46f, dt60);
    }

    if (_silenceCounter < 3 || _meterValue < 1.1f * _lastAudio ||
        (_meterValue < 0.95f * _lastPeak && _lastAudio > 0.2f * _lastPeak))
    {
        _meterValue = _lastAudio * p092;
    } else {
        _silenceCounter = 0;
        _lastPeak = _meterValue;
        _meterValue *= p146;
    }

    if (_meterValue <= 0.1f) {
        _meterValue = 0.1f;
        _lastPeak = 0.0f;
    }

    _lastAudio = _meterValue;
    _silenceCounter++;
}

float AudioManager::getMeteringValue() const { return _meterValue; }

void AudioManager::reset() {
    _meterValue = 0.1f;
    _lastAudio = 0.1f;
    _lastPeak = 0.0f;
    _silenceCounter = 0;
    stopMusic();
}
