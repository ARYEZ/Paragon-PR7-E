#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "LevelMeterSource.h"
#include "Parameters.h"
#include "dsp/InputTap.h"
#include "dsp/TapTempo.h"
#include "dsp/Tuner.h"
#include "dsp/oracle/Oracle.h"
#include "dsp/effects/MultibandComp.h"
#include "dsp/dominion/Dominion.h"
#include "rigs/CompPresets.h"
#include "rigs/DominionPresets.h"
#include "rigs/LimiterPresets.h"
#include "rigs/MarsPresets.h"
#include "dsp/effects/Compressor.h"
#include "dsp/effects/MasterLimiter.h"
#include "dsp/effects/Mars.h"
#include "dsp/effects/StereoTools.h"
#include "dsp/eq/EqMatchSession.h"
#include "dsp/eq/ParametricEq.h"
#include "dsp/effects/Presence.h"
#include "rigs/RigLibrary.h"
#include "rigs/UndoHistory.h"

// Which rig is loaded, for the header display and the Save button.
struct CurrentRig
{
    juce::String name { "Untitled" };
    juce::File file;              // set for the user's own rigs
    juce::String factoryName;     // set for factory rigs
    bool isUserRig() const { return file != juce::File(); }

    bool operator== (const CurrentRig& other) const
    {
        return name == other.name && file == other.file && factoryName == other.factoryName;
    }
};

class ParagonX1AudioProcessor final : public juce::AudioProcessor,
                                      private juce::Timer,
                                      private juce::AudioProcessorValueTreeState::Listener,
                                      private juce::AudioProcessorParameter::Listener
{
public:
    ParagonX1AudioProcessor();
    ~ParagonX1AudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }   // kept as it was (hosts remember a plugin's MIDI input); nothing uses MIDI since 0.12.0
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // The DAW's own bypass button drives our BYPASS parameter, so the two
    // can't disagree, and bypassing keeps the plugin's latency.
    juce::AudioProcessorParameter* getBypassParameter() const override;

    // Presets live in the rig browser (Phase 4), so the host's program list is a single entry.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Rigs (message thread only)
    RigLibrary& getRigLibrary() noexcept { return *rigLibrary; }

    // Points the library at another folder. For tests.
    void useRigFolder (const juce::File& folder);

    // The current sound as a rig (not saved anywhere yet).
    Rig captureRig() const;

    void loadRig (const RigLibrary::Entry& entry);

    // Saves over the current rig if it's one of the user's own and returns
    // true; returns false if it isn't (a factory rig or never saved), in
    // which case the caller should ask for a name and use saveCurrentRigAs.
    bool saveCurrentRig();
    bool saveCurrentRigAs (const juce::String& name);

    // Loads the next (+1) or previous (-1) rig in the library.
    void stepRig (int direction);

    // Rename, re-file or re-tag a user rig from the browser.
    bool updateRigDetails (const RigLibrary::Entry& entry, const Rig& newDetails);
    bool deleteRig (const RigLibrary::Entry& entry);

    CurrentRig getCurrentRig() const { return currentRig; }
    const RigLibrary::Entry* findCurrentRigEntry() const;

    // True once anything in the rig has changed since it was loaded or saved.
    bool isRigModified() const noexcept { return rigModified.load(); }

    // Tells listeners (the editor) that the rig list or current rig changed.
    juce::ChangeBroadcaster rigBroadcaster;

    //==========================================================================
    // UNDO / REDO (message thread only)
    bool canUndo() const { return history.canUndo(); }
    bool canRedo() const { return history.canRedo(); }
    void undo();
    void redo();

    // Records a finished edit now instead of on the next timer tick. For tests.
    void flushUndoStep() { recordCheckpointIfReady(); }

    //==========================================================================
    // Tempo

    // The DAW's tempo, as last seen by the audio thread (120 if it hasn't said).
    double getHostBpm() const noexcept { return hostBpm.load(); }

    // TAP: each call is one tap (message thread). Once tapped, the tapped
    // tempo is used instead of the DAW's until clearTappedTempo().
    void tapTempo();
    void clearTappedTempo();
    double getTappedBpm() const noexcept { return tappedBpm.load(); }   // 0 when following the DAW
    double getTempoBpm() const noexcept;                                 // whichever is in use

    //==========================================================================
    // Tuner: what's coming in, and the TUNER stomp's reading, trace and
    // average (message thread). It updates itself while it's on; RESET empties the trace.
    const InputTap& getInputTap() const noexcept { return inputTap; }
    const Tuner& getTuner() const noexcept { return tuner; }

    // ORACLE (stomp 1, with the tuner): melody to MIDI. While it's on and the
    // DAW plays, it transcribes what comes in (message thread: its page edits
    // the notes, and its timer listens).
    oracle::Transcriber& getOracle() noexcept { return oracle; }
    void updateOracle();   // the timer calls this; tests call it to step ORACLE
    // Where the MIDI files to drag onto a track are written.
    static juce::File getOracleMidiFolder();

    //==========================================================================
    // The EQ (stomp 4). Its settings as they are now, read from the
    // parameters (any thread), the audio going into and out of it (for the
    // analyser), and SOLO, which isn't a parameter: it's held while listening.
    eq::ParametricEq::Settings getEqSettings() const noexcept;
    const InputTap& getEqInputTap() const noexcept { return eqInputTap; }
    const InputTap& getEqOutputTap() const noexcept { return eqOutputTap; }
    void setEqSolo (int band) noexcept { eqSoloBand = band; }
    int getEqSolo() const noexcept { return eqSoloBand.load(); }
    double getEqAutoGainDb() const noexcept { return parametricEq.getAutoGainDb(); }
    eq::ParametricEq::DynamicReading getEqDynamics (int band) const noexcept { return parametricEq.getDynamics (band); }
    eq::match::Session& getEqMatch() noexcept { return eqMatch; }

    // DOMINION (stomp 5): its settings, what it's cutting (for its page), and
    // listening to one of its depth-curve bands (-1: none) while held.
    dominion::Settings getDominionSettings() const noexcept;
    const dominion::Processor& getDominion() const noexcept { return dominionProcessor; }
    void setDominionListen (int band) noexcept { dominionListen = band; }
    int getDominionListen() const noexcept { return dominionListen.load(); }

    // DOMINION's own presets (message thread): the one loaded last (and
    // whether its settings have been changed since), loading one, and yours.
    DominionPresets& getDominionPresets() noexcept { return dominionPresets; }
    void loadDominionPreset (const DominionPreset&);
    juce::String getDominionPresetName() const { return dominionPreset.name; }
    bool isDominionPresetModified() { return ! DominionPresets::matches (dominionPreset, parameters); }
    void rememberDominionPreset (const DominionPreset& preset) { dominionPreset = preset; }   // after saving it

    // COMP's own presets (0.14.0), as DOMINION's.
    CompPresets& getCompPresets() noexcept { return compPresets; }
    void loadCompPreset (const CompPreset& preset) { CompPresets::apply (preset, parameters); compPreset = preset; }
    juce::String getCompPresetName() const { return compPreset.name; }
    bool isCompPresetModified() { return ! CompPresets::matches (compPreset, parameters); }
    void rememberCompPreset (const CompPreset& preset) { compPreset = preset; }

    // The LIMITER's own presets (0.19.0), as COMP's. A mastering one also
    // sets the LIMITER page's loudness TARGET (not a parameter).
    LimiterPresets& getLimiterPresets() noexcept { return limiterPresets; }
    void loadLimiterPreset (const LimiterPreset&);
    juce::String getLimiterPresetName() const { return limiterPreset.name; }
    bool isLimiterPresetModified() { return ! LimiterPresets::matches (limiterPreset, parameters); }
    void rememberLimiterPreset (const LimiterPreset& preset) { limiterPreset = preset; }

    // MARS's own presets (0.22.0), as COMP's.
    MarsPresets& getMarsPresets() noexcept { return marsPresets; }
    void loadMarsPreset (const MarsPreset& preset) { MarsPresets::apply (preset, parameters); marsPreset = preset; }
    juce::String getMarsPresetName() const { return marsPreset.name; }
    bool isMarsPresetModified() { return ! MarsPresets::matches (marsPreset, parameters); }
    void rememberMarsPreset (const MarsPreset& preset) { marsPreset = preset; }

    // COMP's bands as set now (any thread), and each band now (the page's
    // meters). Look-ahead's delay is reported to the DAW from the timer.
    effects::multiband::Settings getMultibandSettings() const noexcept;
    effects::multiband::Processor::Reading getMultibandReading (int band) const noexcept { return multiband.getReading (band); }
    void setMultibandSolo (int band) noexcept { multibandSolo = band; }
    void updateLatency();   // the timer calls this; tests too

    // COMP's gain reduction on a channel in the last block (dB, 0 or more).
    float getCompressorReductionDb (int channel) const noexcept { return compressor.getReductionDb (channel); }
    // The LIMITER's gain reduction on a channel in the last block (dB, 0 or more).
    float getLimiterReductionDb (int channel) const noexcept { return limiter.getReductionDb (channel); }
    effects::MasterLimiter& getLimiter() noexcept { return limiter; }
    effects::MasterLimiter::Settings getLimiterSettings() const;
    // MARS (0.22.0): its settings from the parameters, and how hard a band
    // is driven (the louder side's, 0 to 1+), for its rack unit and page.
    effects::Mars::Settings getMarsSettings() const;
    float getMarsDrive (int band) const noexcept { return juce::jmax (mars[0].getBandDrive (band), mars[1].getBandDrive (band)); }
    // STEREO (0.22.0): its settings, and the correlation of left and right
    // after it (+1 mono, 0 unrelated, -1 out of phase; ~300 ms average).
    effects::StereoTools::Settings getStereoSettings() const;
    float getStereoCorrelation() const noexcept { return stereoCorrelation.load(); }
    // Left and right after BINARY, for its page's scope: the newest `count`
    // of each, in step. False until there's enough.
    bool readImage (float* left, float* right, int count) const noexcept
    {
        const auto end = juce::jmin (imageLeft.getTotalWritten(), imageRight.getTotalWritten());
        return imageLeft.readAt (end - count, left, count) && imageRight.readAt (end - count, right, count);
    }
    void resetTuner() noexcept { tuner.clear(); }
    void updateTuner();   // the timer calls this; tests call it to step the tuner


    //==========================================================================
    // SETTINGS page. Saved with the project.


    // The tuner's reference pitch for A4 (440 Hz unless the band tunes otherwise).
    double getTunerReferenceHz() const noexcept { return tunerReferenceHz.load(); }
    void setTunerReferenceHz (double hz) noexcept { tunerReferenceHz = juce::jlimit (420.0, 460.0, hz); }

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    LevelMeterSource& getInputMeter() noexcept { return inputMeter; }
    LevelMeterSource& getOutputMeter() noexcept { return outputMeter; }

private:
    void processChunk (juce::AudioBuffer<float>& buffer, int start, int numSamples);
    void readHostTempo();
    float param (const char* id) const;
    bool isOn (const char* id) const { return param (id) > 0.5f; }

    void timerCallback() override;
    void parameterChanged (const juce::String& parameterId, float newValue) override;
    void markModified() noexcept;
    void applyRig (const Rig& rig);

    // Undo snapshots: the sound and which rig it is.
    struct Snapshot
    {
        Rig rig;
        CurrentRig current;
        bool modified = false;   // restored too, but doesn't count as a change

        bool operator== (const Snapshot& other) const
        {
            return rig.parameters == other.rig.parameters && current == other.current;
        }
    };
    Snapshot takeSnapshot() const;
    void restoreSnapshot (const Snapshot&);
    void requestCheckpoint() noexcept { checkpointWanted = true; }
    void recordCheckpointIfReady();

    // Knob drags and button clicks start and end "gestures"; an edit is
    // finished (and worth an undo step) when its gesture ends.
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;


    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* inputGainDb = nullptr;
    std::atomic<float>* outputGainDb = nullptr;

    // Smoothing stops zipper noise when a gain knob moves.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain, outputGain;

    // Signal chain, stereo throughout: INPUT -> stomp 4 (EQ) -> PRESENCE -> OUTPUT.
    // The tuner only listens; the other stomps aren't built yet.
    // Stomp 4, the EQ (stereo itself), then PRESENCE: one per channel,
    // since each keeps its filter's memory.
    // The biggest engines live on the heap (0.22.0): a processor held on the
    // stack (as the tests hold them) would otherwise pass 300 KB, and a few
    // at once overflowed Windows' 1 MB stack. Each reference is its engine.
    std::unique_ptr<eq::ParametricEq> parametricEqHeld = std::make_unique<eq::ParametricEq>();
    eq::ParametricEq& parametricEq = *parametricEqHeld;
    struct EqBandParameters { std::atomic<float>* values[std::size (ParamIDs::eqBandSettings)] {}; };
    std::array<EqBandParameters, ParamIDs::eqNumBands> eqBandParameters {};
    std::atomic<int> eqSoloBand { -1 };
    InputTap eqInputTap { 16384 }, eqOutputTap { 16384 };   // what goes into and comes out of the EQ, for its analyser
    eq::match::Session eqMatch { {} };
    std::unique_ptr<dominion::Processor> dominionHeld = std::make_unique<dominion::Processor>();
    dominion::Processor& dominionProcessor = *dominionHeld;   // stomp 5
    struct DominionBandParameters { std::atomic<float>* values[std::size (ParamIDs::domBandSettings)] {}; };
    std::array<DominionBandParameters, ParamIDs::domNumBands> dominionBandParameters {};
    std::atomic<int> dominionListen { -1 };
    DominionPresets dominionPresets { {} };   // its folder is set with the rigs folder
    DominionPreset dominionPreset = DominionPresets::factory().front();   // "Default"
    CompPresets compPresets { {} };
    CompPreset compPreset = CompPresets::factory().front();
    LimiterPresets limiterPresets { {} };
    LimiterPreset limiterPreset = LimiterPresets::factory().front();
    MarsPresets marsPresets { {} };
    MarsPreset marsPreset = MarsPresets::factory().front();
    effects::Compressor compressor;   // stomp 2, ported from PR7-C
    effects::multiband::Processor multiband;   // stomp 2's bands, before it
    struct MultibandBandParameters { std::atomic<float>* values[std::size (ParamIDs::mbBandSettings)] {}; };
    std::array<MultibandBandParameters, ParamIDs::mbNumBands> multibandBandParameters {};
    std::atomic<int> multibandSolo { -1 };
    juce::AudioBuffer<float> compSidechain;   // what it listens to: the audio itself
    // MARS: PR7-V's saturation, which is mono, so one per side: left and
    // right stay apart (each side's DYNAMICS follows its own level).
    std::unique_ptr<std::array<effects::Mars, effects::maxChannels>> marsHeld = std::make_unique<std::array<effects::Mars, effects::maxChannels>>();
    std::array<effects::Mars, effects::maxChannels>& mars = *marsHeld;
    std::array<std::array<std::atomic<float>*, 12>, effects::Mars::maxBands> marsBandParameters {};   // by ParamIDs::marsBandParts
    effects::StereoTools stereoTools;   // STEREO: PR7-C's mono maker and width
    std::atomic<float> stereoCorrelation { 1.0f };
    InputTap imageLeft, imageRight;   // after BINARY, for its scope
    double correlationLR = 0.0, correlationLL = 0.0, correlationRR = 0.0, correlationCoefficient = 0.0;
    juce::SmoothedValue<float> listenMonoAmount;   // STEREO's MONO listening, faded
    effects::MasterLimiter limiter;   // stomp 7 (with its OUTPUT TRIM)   // EQ Match's listening (its folder is set with the rigs folder)
    std::array<effects::Presence, effects::maxChannels> presence;

    std::unique_ptr<RigLibrary> rigLibrary;
    CurrentRig currentRig;
    // Atomic because the DAW's automation changes parameters on the audio thread.
    std::atomic<bool> rigModified { false }, applyingRig { false };

    // BYPASS: the clean input, delayed by our latency so switching bypass
    // never shifts the sound in time, crossfaded in over 20 ms.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> bypassDelay { 8192 };
    juce::AudioBuffer<float> bypassDry;
    juce::SmoothedValue<float> bypassAmount;


    std::atomic<double> tunerReferenceHz { 440.0 };

    // The tuner reads the newest of it; ORACLE every sample in turn, so it
    // holds a few seconds (10 s at 48 kHz) in case the timer is held up.
    InputTap inputTap { 1 << 19 };
    Tuner tuner;
    // Where the DAW is, per block (audio thread only), and for ORACLE.
    struct HostPosition
    {
        bool known = false, playing = false;
        std::int64_t sample = 0;
        double ppq = 0.0, bpm = 120.0;
        int numerator = 4, denominator = 4;
    } hostPosition;
    std::unique_ptr<oracle::Timeline> oracleTimelineHeld = std::make_unique<oracle::Timeline>();
    oracle::Timeline& oracleTimeline = *oracleTimelineHeld;
    oracle::Transcriber oracle;

    UndoHistory<Snapshot> history;
    std::atomic<int> activeGestures { 0 };
    std::atomic<bool> checkpointWanted { true };

    int preparedBlockSize = 512;
    std::atomic<double> hostBpm { 120.0 }, tappedBpm { 0.0 };
    TapTempo tapCounter;

    LevelMeterSource inputMeter, outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParagonX1AudioProcessor)
};
