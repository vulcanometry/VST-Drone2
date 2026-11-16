// SynthEngine.h - the Drone 2 engine: layers, vector mix, modulation and effects.
//
// SetParam, NoteOn/NoteOff and the display getters are safe from any thread;
// Prepare and Process belong to the audio thread.
#pragma once

#include "Effects.h"
#include "Layer.h"
#include "Params.h"

#include <array>
#include <atomic>

class SynthEngine
{
public:
    SynthEngine();

    void Prepare(double sampleRate);
    void Process(float* interleavedStereo, int frames);

    float GetParam(int id) const { return m_params[id].load(std::memory_order_relaxed); }
    void  SetParam(int id, float normalized);
    void  ResetToDefaults();

    void NoteOn(int note, float velocity = 0.8f);
    void NoteOff(int note);
    void AllNotesOff();
    void ResetPitch();   // forgets the last note, so the drone returns to ROOT
    void SetModWheel(float value) { m_wheel.store(value, std::memory_order_relaxed); }

    void StartFadeIn();
    void StartFadeOut();
    bool IsFadedOut() const { return m_fadeTarget.load() == 0.0f && m_fade <= 0.0001f; }

    // Display data for the editor.
    static constexpr int kScopeSize = 4096;
    void CopyScope(float* mono, int count) const;   // newest samples, oldest first
    float LfoValue(int i) const { return m_lfoDisplay[i].load(std::memory_order_relaxed); }
    float VectorX() const { return m_vecXDisplay.load(std::memory_order_relaxed); }
    float VectorY() const { return m_vecYDisplay.load(std::memory_order_relaxed); }
    float LayerMorph(int i) const { return m_morphDisplay[i].load(std::memory_order_relaxed); }
    float PeakL() const { return m_peakLDisplay.load(std::memory_order_relaxed); }
    float PeakR() const { return m_peakRDisplay.load(std::memory_order_relaxed); }
    float EnvValue() const { return m_envDisplay.load(std::memory_order_relaxed); }
    int   HeldNote() const { return m_lastNote.load(std::memory_order_relaxed); }

private:
    struct Lfo
    {
        float phase = 0.0f, value = 0.0f, held = 0.0f, from = 0.0f, to = 0.0f;
        float x = 0.1f, y = 0.0f, z = 20.0f;
    };

    void RenderBlock(float* left, float* right, int n);
    void UpdateLfo(int index, float dt);
    void UpdateNoteState();

    std::array<std::atomic<float>, P_COUNT> m_params;

    double m_sr = 48000.0;
    Layer m_layers[kNumLayers];
    StereoFilter m_filter;
    Ensemble m_ensemble;
    Phaser m_phaser;
    StereoDelay m_delay;
    Reverb m_reverb;
    Master m_master;

    // Notes: a bitmask of held keys, so any thread can press and release without locks.
    std::atomic<uint32_t> m_held[4] = {};
    std::atomic<int> m_lastNote{ -1 };
    std::atomic<float> m_velocity{ 0.8f };
    std::atomic<float> m_wheel{ 0.0f };
    float m_keyOffset = 0.0f;     // glided, in semitones from middle C
    float m_env = 0.0f, m_dryGain = 0.0f;
    bool  m_gate = false;

    Lfo m_lfos[kNumLfos];
    XorShift m_rng;
    float m_wander = 0.0f, m_wanderTarget = 0.0f;
    float m_scanPhase[kNumLayers] = {};
    float m_scanWalk[kNumLayers] = {}, m_scanWalkTarget[kNumLayers] = {};
    float m_orbitPhase = 0.0f, m_orbitDx = 0.0f, m_orbitDy = 0.0f, m_orbitTx = 0.0f, m_orbitTy = 0.0f;
    float m_morphSmooth[kNumLayers] = {}, m_timbreSmooth[kNumLayers] = {}, m_colorSmooth[kNumLayers] = {};
    float m_hzSmooth[kNumLayers] = {};
    float m_cutoffSmooth = -1.0f;

    std::atomic<float> m_fadeTarget{ 1.0f };
    float m_fade = 0.0f;

    std::array<std::atomic<float>, kScopeSize> m_scope;
    std::atomic<int> m_scopeWrite{ 0 };

    std::atomic<float> m_lfoDisplay[kNumLfos] = {};
    std::atomic<float> m_morphDisplay[kNumLayers] = {};
    std::atomic<float> m_vecXDisplay{ 0.5f }, m_vecYDisplay{ 0.5f };
    std::atomic<float> m_peakLDisplay{ 0.0f }, m_peakRDisplay{ 0.0f }, m_envDisplay{ 0.0f };
};
