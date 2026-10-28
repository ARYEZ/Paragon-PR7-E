#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"

namespace
{
    constexpr double gainRampSeconds = 0.02;

    // Extra settings kept in the saved state (DAW project or preset).
    const juce::Identifier rigNameKey { "rigName" };
    const juce::Identifier rigFileKey { "rigFile" };
    const juce::Identifier rigFactoryKey { "rigFactory" };
    const juce::Identifier rigModifiedKey { "rigModified" };
    const juce::Identifier tappedBpmKey { "tappedBpm" };
    const juce::Identifier tunerReferenceKey { "tunerReferenceHz" };
    // Which version's parameters a project was saved with, as for rigs (see
    // Rig.cpp). Projects from before 0.3.0 don't say: 0.
    const juce::Identifier parameterVersionKey { "parameterVersion" };
    constexpr int parameterVersion = 36;

}

ParagonX1AudioProcessor::ParagonX1AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ParagonX1", createParameterLayout())
{
    inputGainDb = parameters.getRawParameterValue (ParamIDs::inputGain);
    outputGainDb = parameters.getRawParameterValue (ParamIDs::outputGain);
    for (int band = 0; band < ParamIDs::eqNumBands; ++band)
        for (size_t i = 0; i < std::size (ParamIDs::eqBandSettings); ++i)
            eqBandParameters[(size_t) band].values[i] = parameters.getRawParameterValue (ParamIDs::eqBand (band, ParamIDs::eqBandSettings[i]));
    for (int band = 0; band < ParamIDs::domNumBands; ++band)
        for (size_t i = 0; i < std::size (ParamIDs::domBandSettings); ++i)
            dominionBandParameters[(size_t) band].values[i] = parameters.getRawParameterValue (ParamIDs::domBand (band, ParamIDs::domBandSettings[i]));
    for (int band = 0; band < ParamIDs::mbNumBands; ++band)
        for (size_t i = 0; i < std::size (ParamIDs::mbBandSettings); ++i)
            multibandBandParameters[(size_t) band].values[i] = parameters.getRawParameterValue (ParamIDs::mbBand (band, ParamIDs::mbBandSettings[i]));

    rigLibrary = std::make_unique<RigLibrary>();
    eqMatch.setReferencesFolder (rigLibrary->getFolder().getChildFile ("Match References"));
    dominionPresets.setFolder (rigLibrary->getFolder().getChildFile ("Dominion Presets"));
    compPresets.setFolder (rigLibrary->getFolder().getChildFile ("Comp Presets"));
    limiterPresets.setFolder (rigLibrary->getFolder().getChildFile ("Limiter Presets"));

    // Any change to a parameter that rigs store marks the rig as modified,
    // and finishing an edit to one records an undo step.
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            if (! ParamIDs::notInRigs.contains (withId->paramID))
            {
                parameters.addParameterListener (withId->paramID, this);
                parameter->addListener (this);
            }

    // Housekeeping off the audio thread: show MIDI pedal moves and record
    // undo steps.
    startTimerHz (30);

    // The starting point, so the first edit can be undone.
    history.record (takeSnapshot());
    checkpointWanted = false;
}

ParagonX1AudioProcessor::~ParagonX1AudioProcessor()
{
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
        {
            parameters.removeParameterListener (withId->paramID, this);
            parameter->removeListener (this);
        }

    stopTimer();
}

void ParagonX1AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    inputGain.reset (sampleRate, gainRampSeconds);
    outputGain.reset (sampleRate, gainRampSeconds);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGainDb->load()));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load() + param (ParamIDs::rigLevel)));

    preparedBlockSize = juce::jmax (1, samplesPerBlock);
    bypassDelay.prepare ({ sampleRate, (juce::uint32) preparedBlockSize, (juce::uint32) effects::maxChannels });
    bypassDry.setSize (effects::maxChannels, preparedBlockSize);
    bypassAmount.reset (sampleRate, gainRampSeconds);
    bypassAmount.setCurrentAndTargetValue (isOn (ParamIDs::bypass) ? 1.0f : 0.0f);
    parametricEq.prepare (sampleRate, preparedBlockSize);
    dominionProcessor.prepare (sampleRate, preparedBlockSize);
    compressor.prepare (sampleRate, preparedBlockSize);
    multiband.prepare (sampleRate, preparedBlockSize);
    compSidechain.setSize (2, preparedBlockSize);
    limiter.prepare (sampleRate, preparedBlockSize);
    eqInputTap.setSampleRate (sampleRate);
    eqOutputTap.setSampleRate (sampleRate);
    for (auto& shelf : presence)
        shelf.prepare (sampleRate, preparedBlockSize);
    inputTap.setSampleRate (sampleRate);

    updateLatency();
}

effects::MasterLimiter::Settings ParagonX1AudioProcessor::getLimiterSettings() const
{
    effects::MasterLimiter::Settings s;
    s.on = isOn (ParamIDs::limitOn);
    s.style = (effects::limiter::Style) juce::jlimit (0, ParamRanges::limitModes.size() - 1, juce::roundToInt (param (ParamIDs::limitMode)));
    s.gainDb = param (ParamIDs::limitGain);
    s.ceilingDb = param (ParamIDs::limitCeiling);
    s.lookaheadMs = param (ParamIDs::limitLookahead);
    s.attackMs = param (ParamIDs::limitAttack);
    s.releaseMs = param (ParamIDs::limitRelease);
    s.transientLink = param (ParamIDs::limitLinkTransient) / 100.0f;
    s.releaseLink = param (ParamIDs::limitLinkRelease) / 100.0f;
    s.truePeak = isOn (ParamIDs::limitTruePeak);
    s.oversampling = juce::roundToInt (param (ParamIDs::limitOversampling));
    s.dither = juce::roundToInt (param (ParamIDs::limitDither));
    s.shaping = juce::roundToInt (param (ParamIDs::limitShaping));
    s.unity = isOn (ParamIDs::limitUnity);
    s.delta = isOn (ParamIDs::limitDelta);
    s.dcFilter = isOn (ParamIDs::limitDcFilter);
    s.trimDb = param (ParamIDs::limitTrim);
    s.mix = param (ParamIDs::limitMix) / 100.0f;
    s.link = isOn (ParamIDs::limitLink);
    return s;
}

void ParagonX1AudioProcessor::updateLatency()
{
    // Only COMP's LOOK-AHEAD delays the sound (the NAM amp, which also did, went with PR7-B's bass blocks).
    // COMP's LOOK-AHEAD, and the LIMITER's (with its oversampling and true peak).
    const auto latency = (isOn (ParamIDs::mbLookahead) ? (int) std::lround (effects::multiband::lookaheadSeconds * inputTap.getSampleRate()) : 0)
                         + limiter.getLatencySamples (getLimiterSettings());
    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

effects::multiband::Settings ParagonX1AudioProcessor::getMultibandSettings() const noexcept
{
    namespace mb = effects::multiband;
    mb::Settings settings;
    settings.lookahead = isOn (ParamIDs::mbLookahead);
    settings.mix = param (ParamIDs::mbMix) / 100.0f;
    settings.soloBand = multibandSolo.load();
    for (size_t i = 0; i < settings.bands.size(); ++i)
    {
        const auto& v = multibandBandParameters[i].values;   // in mbBandSettings' order
        auto& band = settings.bands[i];
        band.used = v[0]->load() > 0.5f;
        band.on = v[1]->load() > 0.5f;
        band.lowHz = v[2]->load();
        band.highHz = v[3]->load();
        band.slope = juce::jlimit (0, mb::numSlopes - 1, juce::roundToInt (v[4]->load()));
        band.thresholdDb = v[5]->load();
        band.rangeDb = v[6]->load();
        band.mode = (mb::Mode) juce::jlimit (0, 1, juce::roundToInt (v[7]->load()));
        band.ratio = v[8]->load();
        band.kneeDb = v[9]->load();
        band.attackMs = v[10]->load();
        band.releaseMs = v[11]->load();
        band.character = (effects::Compressor::Character) juce::jlimit (0, 2, juce::roundToInt (v[12]->load()));
        band.stereo = (mb::Stereo) juce::jlimit (0, 2, juce::roundToInt (v[13]->load()));
        band.link = v[14]->load() / 100.0f;
        band.outputDb = v[15]->load();
        band.lookaheadMs = v[16]->load();
    }
    return settings;
}

juce::AudioProcessorParameter* ParagonX1AudioProcessor::getBypassParameter() const
{
    return parameters.getParameter (ParamIDs::bypass);
}

float ParagonX1AudioProcessor::param (const char* id) const
{
    // Looked up by ID each block. APVTS keeps these in a sorted map, so the
    // lookup is a handful of string compares and never allocates.
    return parameters.getRawParameterValue (id)->load();
}

eq::ParametricEq::Settings ParagonX1AudioProcessor::getEqSettings() const noexcept
{
    eq::ParametricEq::Settings settings;
    settings.on = isOn (ParamIDs::eqOn);
    settings.outputDb = param (ParamIDs::eqOutput);
    settings.gainScale = param (ParamIDs::eqGainScale);
    settings.autoGain = isOn (ParamIDs::eqAutoGain);
    settings.soloBand = eqSoloBand.load();
    for (size_t i = 0; i < settings.bands.size(); ++i)
    {
        const auto& values = eqBandParameters[i].values;   // in eqBandSettings' order
        auto& band = settings.bands[i];
        band.used = values[0]->load() > 0.5f;
        band.on = values[1]->load() > 0.5f;
        band.shape = (eq::Shape) juce::roundToInt (values[2]->load());
        band.frequency = values[3]->load();
        band.gainDb = values[4]->load();
        band.q = values[5]->load();
        band.slope = juce::roundToInt (values[6]->load());
        band.placement = (eq::Placement) juce::roundToInt (values[7]->load());
        band.dynamicRangeDb = values[8]->load();
        band.autoThreshold = values[9]->load() > 0.5f;
        band.thresholdDb = values[10]->load();
        band.attack = values[11]->load();
        band.release = values[12]->load();
    }
    return settings;
}

dominion::Settings ParagonX1AudioProcessor::getDominionSettings() const noexcept
{
    dominion::Settings s;
    s.on = isOn (ParamIDs::domOn);
    s.mode = (dominion::Mode) juce::jlimit (0, 1, juce::roundToInt (param (ParamIDs::domMode)));
    s.depth = param (ParamIDs::domDepth);
    s.detail = param (ParamIDs::domDetail);
    s.attack = param (ParamIDs::domAttack);
    s.release = param (ParamIDs::domRelease);
    s.stereo = (dominion::StereoMode) juce::jlimit (0, 1, juce::roundToInt (param (ParamIDs::domStereo)));
    s.link = param (ParamIDs::domLink);
    s.focus = param (ParamIDs::domFocus);
    s.detailTiltLow = param (ParamIDs::domDetailLow);
    s.detailTiltHigh = param (ParamIDs::domDetailHigh);
    s.attackTiltLow = param (ParamIDs::domAttackLow);
    s.attackTiltHigh = param (ParamIDs::domAttackHigh);
    s.releaseTiltLow = param (ParamIDs::domReleaseLow);
    s.releaseTiltHigh = param (ParamIDs::domReleaseHigh);
    s.maxCutDb = param (ParamIDs::domMaxCut);
    s.wetTrimDb = param (ParamIDs::domWetTrim);
    s.mix = param (ParamIDs::domMix);
    s.outDb = param (ParamIDs::domOut);
    s.delta = isOn (ParamIDs::domDelta);
    for (size_t i = 0; i < s.bands.size(); ++i)
    {
        const auto& values = dominionBandParameters[i].values;   // in domBandSettings' order
        auto& band = s.bands[i];
        band.used = values[0]->load() > 0.5f;
        band.on = values[1]->load() > 0.5f;
        band.shape = (dominion::Shape) juce::jlimit (0, dominion::shapeNames.size() - 1, juce::roundToInt (values[2]->load()));
        band.frequency = values[3]->load();
        band.depthDb = values[4]->load();
        band.q = values[5]->load();
        band.slope = juce::roundToInt (values[6]->load());
        band.focus = values[7]->load();
    }
    s.listenBand = dominionListen.load();
    return s;
}

void ParagonX1AudioProcessor::loadLimiterPreset (const LimiterPreset& preset)
{
    LimiterPresets::apply (preset, parameters);
    if (const auto target = preset.parameters.find (LimiterPresetTraits::targetKey); target != preset.parameters.end())
        parameters.state.setProperty (LimiterPresetTraits::targetKey, target->second, nullptr);
    limiterPreset = preset;
}

void ParagonX1AudioProcessor::loadDominionPreset (const DominionPreset& preset)
{
    DominionPresets::apply (preset, parameters);
    dominionPreset = preset;
}

void ParagonX1AudioProcessor::readHostTempo()
{
    hostPosition.known = false;
    if (auto* host = getPlayHead())
        if (const auto position = host->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
            {
                hostBpm = *bpm;
                hostPosition.bpm = *bpm;
            }
            // Where the song is, for ORACLE.
            if (const auto samples = position->getTimeInSamples())
            {
                hostPosition.known = true;
                hostPosition.playing = position->getIsPlaying() || position->getIsRecording();
                hostPosition.sample = *samples;
                hostPosition.ppq = position->getPpqPosition().orFallback ((double) *samples / juce::jmax (1.0, inputTap.getSampleRate()) * hostPosition.bpm / 60.0);
                if (const auto signature = position->getTimeSignature())
                {
                    hostPosition.numerator = signature->numerator;
                    hostPosition.denominator = signature->denominator;
                }
            }
        }
}

bool ParagonX1AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto isMonoOrStereo = [] (const juce::AudioChannelSet& set)
    {
        return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
    };

    return isMonoOrStereo (layouts.getMainInputChannelSet())
        && isMonoOrStereo (layouts.getMainOutputChannelSet());
}

double ParagonX1AudioProcessor::getTempoBpm() const noexcept
{
    const auto tapped = tappedBpm.load();
    return tapped > 0.0 ? tapped : hostBpm.load();
}

void ParagonX1AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midi);

    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    readHostTempo();

    // Some hosts occasionally send more samples than they promised in
    // prepareToPlay. Every stage's scratch memory is sized for the promise,
    // so work through an oversized block in promised-size pieces.
    for (int start = 0; start < buffer.getNumSamples(); start += preparedBlockSize)
        processChunk (buffer, start, juce::jmin (preparedBlockSize, buffer.getNumSamples() - start));
}

void ParagonX1AudioProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int numSamples)
{
    // Stereo throughout: PR7-E works on recordings and mixes, so both
    // channels go through untouched apart from what the stomps do to them.
    // A mono track is heard on both speakers.
    const auto numOutputChannels = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels(), effects::maxChannels);
    const auto numInputChannels = juce::jmin (getTotalNumInputChannels(), numOutputChannels);
    if (numInputChannels == 1)
        for (int channel = 1; channel < numOutputChannels; ++channel)
            buffer.copyFrom (channel, start, buffer, 0, start, numSamples);

    const auto block = juce::dsp::AudioBlock<float> (buffer)
                           .getSubBlock ((size_t) start, (size_t) numSamples)
                           .getSubsetChannelBlock (0, (size_t) numOutputChannels);

    // The tuner and ORACLE listen to what comes in, before anything changes
    // it: both channels of a stereo track (their middle), so a part panned
    // anywhere is heard. ORACLE first learns where in the song this piece is.
    {
        oracle::TimelineMark mark;
        mark.tapIndex = inputTap.getTotalWritten();
        mark.known = hostPosition.known;
        mark.playing = hostPosition.playing;
        mark.hostSample = hostPosition.sample + start;
        mark.bpm = hostPosition.bpm;
        mark.ppq = hostPosition.ppq + (hostPosition.playing ? start / juce::jmax (1.0, inputTap.getSampleRate()) * hostPosition.bpm / 60.0 : 0.0);
        mark.numerator = hostPosition.numerator;
        mark.denominator = hostPosition.denominator;
        if (! hostPosition.playing)
            mark.hostSample = hostPosition.sample;
        oracleTimeline.write (mark);
    }
    if (numOutputChannels > 1)
        inputTap.write (block.getChannelPointer (0), block.getChannelPointer (1), numSamples);
    else
        inputTap.write (block.getChannelPointer (0), numSamples);

    // Keep the clean input, delayed to line up with the processed sound, for BYPASS.
    const auto latency = (float) juce::jlimit (0, bypassDelay.getMaximumDelayInSamples() - 1, getLatencySamples());
    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        const auto* in = block.getChannelPointer ((size_t) channel);
        auto* dry = bypassDry.getWritePointer (channel);
        for (int i = 0; i < numSamples; ++i)
        {
            bypassDelay.pushSample (channel, in[i]);
            dry[i] = bypassDelay.popSample (channel, latency);
        }
    }

    // INPUT, the same on both channels.
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (inputGainDb->load()));
    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = inputGain.getNextValue();
        for (size_t channel = 0; channel < block.getNumChannels(); ++channel)
            block.getChannelPointer (channel)[i] *= gain;
    }
    for (size_t channel = 0; channel < block.getNumChannels(); ++channel)
        inputMeter.pushSamples (block.getChannelPointer (channel), numSamples);

    // Stomp 4, the EQ, with what goes in and comes out kept for its analyser.
    const auto writeTap = [&] (InputTap& tap)
    {
        if (numOutputChannels > 1)
            tap.write (block.getChannelPointer (0), block.getChannelPointer (1), numSamples);
        else
            tap.write (block.getChannelPointer (0), numSamples);
    };
    writeTap (eqInputTap);
    parametricEq.process (block, getEqSettings());
    writeTap (eqOutputTap);

    // Stomp 5, DOMINION, after the EQ (so it hears what the EQ made) and before COMP.
    dominionProcessor.process (block, getDominionSettings());

    // Stomp 2, COMP, after the EQ, so what the EQ cuts doesn't drive it.
    // First its bands (0.13.0; with LOOK-AHEAD on, everything from here is
    // 20 ms late, which the DAW makes up for), then the full-band compressor
    // (PR7-C's). That takes whole buffers, so it gets this piece's channels
    // as one, and the same audio to listen to (PR7-E has no sidechain filters).
    multiband.process (block, getMultibandSettings(), isOn (ParamIDs::compOn));
    {
        float* channels[effects::maxChannels] {};
        for (int channel = 0; channel < numOutputChannels; ++channel)
        {
            channels[channel] = block.getChannelPointer ((size_t) channel);
            compSidechain.copyFrom (channel, 0, channels[channel], numSamples);
        }
        juce::AudioBuffer<float> audio (channels, numOutputChannels, numSamples);
        const juce::AudioBuffer<float> detector (compSidechain.getArrayOfWritePointers(), numOutputChannels, numSamples);
        compressor.process (audio, detector, numSamples,
                            { isOn (ParamIDs::compOn) && isOn (ParamIDs::compWide), param (ParamIDs::compThreshold), param (ParamIDs::compRatio),
                              param (ParamIDs::compAttack), param (ParamIDs::compRelease), isOn (ParamIDs::compAuto),
                              param (ParamIDs::compMakeUp),
                              (effects::Compressor::Character) juce::jlimit (0, 2, juce::roundToInt (param (ParamIDs::compCharacter))),
                              isOn (ParamIDs::compLink) });
    }

    // PRESENCE on the face.
    for (size_t channel = 0; channel < block.getNumChannels(); ++channel)
        presence[channel].process (block.getSingleChannelBlock (channel), param (ParamIDs::presence));

    // The master OUTPUT knob plus the rig's own RIG LEVEL. (The expression
    // pedal's VOLUME came before 0.12.0, when the pedal went: its parameters
    // are still read from old projects and rigs, but do nothing.)
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load() + param (ParamIDs::rigLevel)));
    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = outputGain.getNextValue();
        for (size_t channel = 0; channel < block.getNumChannels(); ++channel)
            block.getChannelPointer (channel)[i] *= gain;
    }

    // Stomp 7, the LIMITER: last, so nothing after it can push a peak past
    // its ceiling. OUTPUT and RIG LEVEL come before it, so turning OUTPUT up
    // drives into it, as into a mastering limiter. Its OUTPUT TRIM and
    // DITHER are inside it, at the very end.
    limiter.process (block, getLimiterSettings());

    // BYPASS crossfades to the clean input, channel by channel.
    bypassAmount.setTargetValue (isOn (ParamIDs::bypass) ? 1.0f : 0.0f);
    if (bypassAmount.isSmoothing() || bypassAmount.getTargetValue() > 0.5f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto amount = bypassAmount.getNextValue();
            for (size_t channel = 0; channel < block.getNumChannels(); ++channel)
            {
                auto& sample = block.getChannelPointer (channel)[i];
                sample += (bypassDry.getSample ((int) channel, i) - sample) * amount;
            }
        }
    }

    for (size_t channel = 0; channel < block.getNumChannels(); ++channel)
        outputMeter.pushSamples (block.getChannelPointer (channel), numSamples);
}

//==============================================================================
void ParagonX1AudioProcessor::timerCallback()
{
    updateTuner();
    updateOracle();
    updateLatency();
    eqMatch.update (eqInputTap);   // EQ Match listens with the window closed too
    recordCheckpointIfReady();
}

void ParagonX1AudioProcessor::updateTuner()
{
    // The tuner only listens while it's on. It never touches the sound.
    if (isOn (ParamIDs::tunerOn))
        tuner.update (inputTap, tunerReferenceHz.load(), juce::Time::getMillisecondCounter());
    else
        tuner.stop();
}

void ParagonX1AudioProcessor::updateOracle()
{
    oracle.update (inputTap, oracleTimeline, isOn (ParamIDs::tunerOn));
}

juce::File ParagonX1AudioProcessor::getOracleMidiFolder()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Paragon PR7-E ORACLE");
}

//==============================================================================
void ParagonX1AudioProcessor::parameterGestureChanged (int, bool gestureIsStarting)
{
    if (gestureIsStarting)
    {
        ++activeGestures;
    }
    else
    {
        // Never below zero, even if a host sends an unmatched "end".
        auto active = activeGestures.load();
        while (active > 0 && ! activeGestures.compare_exchange_weak (active, active - 1)) {}
        requestCheckpoint();
    }
}

ParagonX1AudioProcessor::Snapshot ParagonX1AudioProcessor::takeSnapshot() const
{
    return { captureRig(), currentRig, rigModified.load() };
}

void ParagonX1AudioProcessor::recordCheckpointIfReady()
{
    // Wait until the edit is really finished: no knob still held.
    if (! checkpointWanted.load() || activeGestures.load() > 0)
        return;

    checkpointWanted = false;
    history.record (takeSnapshot());
}

void ParagonX1AudioProcessor::restoreSnapshot (const Snapshot& snapshot)
{
    applyRig (snapshot.rig);
    currentRig = snapshot.current;
    rigModified = snapshot.modified;
    rigBroadcaster.sendChangeMessage();
}

void ParagonX1AudioProcessor::undo()
{
    // Record anything not yet recorded first, so undo takes back the latest edit rather than the one before it.
    recordCheckpointIfReady();
    if (const auto snapshot = history.undo())
        restoreSnapshot (*snapshot);
}

void ParagonX1AudioProcessor::redo()
{
    recordCheckpointIfReady();
    if (const auto snapshot = history.redo())
        restoreSnapshot (*snapshot);
}

//==============================================================================
void ParagonX1AudioProcessor::tapTempo()
{
    if (const auto bpm = tapCounter.tap (juce::Time::getMillisecondCounterHiRes()))
        tappedBpm = std::round (*bpm * 10.0) / 10.0;   // to 0.1 BPM, as displayed
}

void ParagonX1AudioProcessor::clearTappedTempo()
{
    tappedBpm = 0.0;
    tapCounter = {};
}

//==============================================================================
void ParagonX1AudioProcessor::markModified() noexcept
{
    if (! applyingRig.load())
        rigModified = true;
}

void ParagonX1AudioProcessor::parameterChanged (const juce::String&, float)
{
    // Can be called on the audio thread (automation), so only touch the atomic.
    markModified();
}

void ParagonX1AudioProcessor::useRigFolder (const juce::File& folder)
{
    rigLibrary = std::make_unique<RigLibrary> (folder);
    eqMatch.setReferencesFolder (folder.getChildFile ("Match References"));
    dominionPresets.setFolder (folder.getChildFile ("Dominion Presets"));
    compPresets.setFolder (folder.getChildFile ("Comp Presets"));
    limiterPresets.setFolder (folder.getChildFile ("Limiter Presets"));
    rigBroadcaster.sendChangeMessage();
}

Rig ParagonX1AudioProcessor::captureRig() const
{
    Rig rig;
    rig.name = currentRig.name;
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (! ParamIDs::notInRigs.contains (ranged->paramID))
                rig.parameters[ranged->paramID] = ranged->convertFrom0to1 (ranged->getValue());
    return rig;
}

void ParagonX1AudioProcessor::applyRig (const Rig& rig)
{
    applyingRig = true;

    for (auto* parameter : AudioProcessor::getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        if (ranged == nullptr || ParamIDs::notInRigs.contains (ranged->paramID))
            continue;

        // Anything the rig doesn't mention goes back to its default.
        const auto found = rig.parameters.find (ranged->paramID);
        const auto normalised = found != rig.parameters.end()
                                  ? ranged->convertTo0to1 (ranged->getNormalisableRange().snapToLegalValue (found->second))
                                  : ranged->getDefaultValue();
        ranged->setValueNotifyingHost (normalised);
    }

    applyingRig = false;
    rigModified = false;
}

void ParagonX1AudioProcessor::loadRig (const RigLibrary::Entry& entry)
{
    // Copy first: applying parameters can't invalidate it, but be safe.
    const auto rig = entry.rig;
    currentRig = { rig.name, entry.file, entry.isFactory ? rig.name : juce::String() };
    applyRig (rig);
    requestCheckpoint();
    rigBroadcaster.sendChangeMessage();
}

const RigLibrary::Entry* ParagonX1AudioProcessor::findCurrentRigEntry() const
{
    if (currentRig.isUserRig())
        return rigLibrary->find (currentRig.file);
    if (currentRig.factoryName.isNotEmpty())
        return rigLibrary->findFactory (currentRig.factoryName);
    return nullptr;
}

bool ParagonX1AudioProcessor::saveCurrentRig()
{
    const auto* entry = findCurrentRigEntry();
    if (entry == nullptr || entry->isFactory)
        return false;

    // Keep how it's filed; replace the sound.
    auto rig = captureRig();
    rig.name = entry->rig.name;
    rig.bank = entry->rig.bank;
    rig.number = entry->rig.number;
    rig.tags = entry->rig.tags;
    rig.favourite = entry->rig.favourite;
    rig.notes = entry->rig.notes;

    const auto file = entry->file;
    if (! rigLibrary->overwrite (file, rig))
        return false;

    rigModified = false;
    history.replaceCurrent (takeSnapshot());
    rigBroadcaster.sendChangeMessage();
    return true;
}

bool ParagonX1AudioProcessor::saveCurrentRigAs (const juce::String& name)
{
    auto rig = captureRig();
    rig.name = name;

    // File it next to the rig it came from, in the first free slot.
    if (const auto* entry = findCurrentRigEntry())
    {
        rig.bank = entry->rig.bank;
        rig.tags = entry->rig.tags;
        rig.notes = entry->rig.notes;
    }
    rig.number = rigLibrary->nextFreeNumber (rig.bank);

    const auto file = rigLibrary->saveAsNew (rig);
    if (file == juce::File())
        return false;

    const auto* saved = rigLibrary->find (file);
    currentRig = { saved != nullptr ? saved->rig.name : name, file, {} };
    rigModified = false;
    // Saving isn't an edit to undo, but undo shouldn't bring back the old name either.
    history.replaceCurrent (takeSnapshot());
    rigBroadcaster.sendChangeMessage();
    return true;
}

void ParagonX1AudioProcessor::stepRig (int direction)
{
    if (const auto* next = rigLibrary->step (findCurrentRigEntry(), direction))
        loadRig (*next);
}

bool ParagonX1AudioProcessor::updateRigDetails (const RigLibrary::Entry& entry, const Rig& newDetails)
{
    if (entry.isFactory)
        return false;

    auto rig = entry.rig;
    rig.name = newDetails.name.trim().isEmpty() ? rig.name : newDetails.name.trim();
    rig.bank = newDetails.bank;
    rig.number = newDetails.number;
    rig.tags = newDetails.tags;

    const auto file = entry.file;
    const auto isCurrent = currentRig.file == file;
    if (! rigLibrary->overwrite (file, rig))
        return false;

    if (isCurrent)
    {
        currentRig.name = rig.name;
        history.replaceCurrent (takeSnapshot());
    }
    rigBroadcaster.sendChangeMessage();
    return true;
}

bool ParagonX1AudioProcessor::deleteRig (const RigLibrary::Entry& entry)
{
    if (entry.isFactory)
        return false;

    const auto file = entry.file;
    if (! rigLibrary->remove (file))
        return false;

    // The sound is still loaded; it just isn't saved anywhere any more.
    if (currentRig.file == file)
    {
        currentRig.file = juce::File();
        rigModified = true;
        history.replaceCurrent (takeSnapshot());
    }
    rigBroadcaster.sendChangeMessage();
    return true;
}

//==============================================================================
juce::AudioProcessorEditor* ParagonX1AudioProcessor::createEditor()
{
    return new ParagonX1AudioProcessorEditor (*this);
}

void ParagonX1AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (parameterVersionKey, parameterVersion, nullptr);
    state.setProperty (rigNameKey, currentRig.name, nullptr);
    state.setProperty (rigFileKey, currentRig.file.getFullPathName(), nullptr);
    state.setProperty (rigFactoryKey, currentRig.factoryName, nullptr);
    state.setProperty (rigModifiedKey, rigModified.load(), nullptr);
    state.setProperty (tappedBpmKey, tappedBpm.load(), nullptr);
    state.setProperty (tunerReferenceKey, tunerReferenceHz.load(), nullptr);
    state.appendChild (oracle.toValueTree(), nullptr);   // ORACLE's transcription, kept with the project

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ParagonX1AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    // ORACLE's transcription isn't parameters: out of the state before it's applied.
    const auto transcription = state.getChildWithName ("Oracle");
    oracle.fromValueTree (transcription);
    state.removeChild (transcription, nullptr);

    // Saved before 0.3.0 with the expression pedal on one of PR7-B's effects
    // (WAH, MORPH, LO-FI...): those are gone, so the pedal comes back
    // unassigned. Left alone, the saved number would land on VOLUME, the
    // last one left, and the heel would silence the track.
    if ((int) state.getProperty (parameterVersionKey, 0) < 30)
    {
        auto target = state.getChildWithProperty ("id", ParamIDs::expressionTarget);
        if (target.isValid() && (int) target["value"] > ExpressionTargets::volume)
            target.setProperty ("value", ExpressionTargets::off, nullptr);
    }
    // Saved before 0.16.0: the LIMITER's THRESHOLD and MAKE UP as GAIN and OUTPUT (the same sound).
    if ((int) state.getProperty (parameterVersionKey, 0) < 36)
    {
        const auto get = [&state] (const char* id, float otherwise)
        {
            const auto child = state.getChildWithProperty ("id", id);
            return child.isValid() ? (float) child["value"] : otherwise;
        };
        const auto set = [&state] (const char* id, float value)
        {
            auto child = state.getChildWithProperty ("id", id);
            if (! child.isValid())
            {
                child = juce::ValueTree ("PARAM");
                child.setProperty ("id", id, nullptr);
                state.appendChild (child, nullptr);
            }
            child.setProperty ("value", value, nullptr);
        };
        const auto converted = convertOldLimiter (get (ParamIDs::limitThreshold, -1.0f), get (ParamIDs::limitMakeUp, 0.0f), get (ParamIDs::limitTrim, 0.0f));
        set (ParamIDs::limitGain, converted.gain);
        set (ParamIDs::limitCeiling, converted.ceiling);
        set (ParamIDs::limitTrim, converted.trim);
    }
    parameters.replaceState (state);

    // Restoring the project changed parameters, which marked the rig as
    // modified; put back what was saved instead.
    const auto rigFile = state[rigFileKey].toString();
    currentRig = { state.getProperty (rigNameKey, "Untitled").toString(),
                   rigFile.isNotEmpty() ? juce::File (rigFile) : juce::File(),
                   state[rigFactoryKey].toString() };
    rigModified = (bool) state.getProperty (rigModifiedKey, false);
    tappedBpm = (double) state.getProperty (tappedBpmKey, 0.0);
    setTunerReferenceHz ((double) state.getProperty (tunerReferenceKey, 440.0));

    // A different project or preset: start the undo history afresh from it.
    history.clear();
    requestCheckpoint();
    rigBroadcaster.sendChangeMessage();
}

// JUCE calls this to create new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ParagonX1AudioProcessor();
}
