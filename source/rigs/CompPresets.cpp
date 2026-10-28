#include "CompPresets.h"

#include "Parameters.h"
#include "dsp/effects/MultibandComp.h"

namespace
{
    namespace mb = effects::multiband;
    using Character = effects::Compressor::Character;

    // A band: where it is and what it does (the rest at their defaults).
    struct BandSpec
    {
        float lowHz, highHz, thresholdDb, rangeDb, ratio = 3.0f;
        mb::Mode mode = mb::Mode::compress;
        float attackMs = 10.0f, releaseMs = 150.0f;
        Character character = Character::vca;
        mb::Stereo stereo = mb::Stereo::stereo;
        float kneeDb = 6.0f;
    };

    // The full band's settings (it's on unless a preset says otherwise), then its bands.
    StompPreset make (const juce::String& category, const juce::String& name,
                      std::initializer_list<std::pair<const char*, float>> settings, std::initializer_list<BandSpec> bands)
    {
        StompPreset preset;
        preset.category = category;
        preset.name = name;
        preset.isFactory = true;
        for (const auto& [id, value] : settings)
            preset.parameters[id] = value;
        int index = 0;
        for (const auto& b : bands)
        {
            const auto set = [&] (const char* what, float value) { preset.parameters[ParamIDs::mbBand (index, what)] = value; };
            set ("Used", 1.0f);
            set ("Low", b.lowHz);
            set ("High", b.highHz);
            set ("Thresh", b.thresholdDb);
            set ("Range", b.rangeDb);
            set ("Ratio", b.ratio);
            set ("Mode", (float) b.mode);
            set ("Attack", b.attackMs);
            set ("Release", b.releaseMs);
            set ("Char", (float) b.character);
            set ("Stereo", (float) b.stereo);
            set ("Knee", b.kneeDb);
            ++index;
        }
        return preset;
    }

    constexpr auto opto = (float) Character::opto;
    constexpr auto fet = (float) Character::fet;
    constexpr float bottom = mb::minHz, top = mb::maxHz;

    // Genre presets (0.15.0), filed as "Genres/<genre>": what the genre rigs
    // use (each genre's vocal, drums, bass, mix bus and master), so the same
    // settings can be put on a track without loading a whole rig. Our own
    // reading of how each style is usually mixed, not anyone's settings.
    std::vector<StompPreset> makeGenres()
    {
        using namespace ParamIDs;
        const auto c = mb::Mode::compress, expand = mb::Mode::expand;
        const auto vcaChar = Character::vca, optoChar = Character::opto, fetChar = Character::fet;
        const auto mid = mb::Stereo::mid, side = mb::Stereo::side, both = mb::Stereo::stereo;
        // A master: gentle, three bands with LOOK-AHEAD, the full band off (the LIMITER does the peaks).
        const auto master = [&] (const juce::String& genre, float lowRange, float midRange, float highRange, float split1, float split2)
        {
            return make ("Genres/" + genre, genre + " Master", { { compWide, 0.0f }, { mbLookahead, 1.0f } },
                         { { bottom, split1, -24.0f, lowRange, 2.0f, c, 20.0f, 200.0f, vcaChar, mid },
                           { split1, split2, -26.0f, midRange, 1.5f, c, 10.0f, 150.0f },
                           { split2, top, -30.0f, highRange, 1.5f, c, 5.0f, 100.0f } });
        };
        return {
            // Modern metal: tight, bright and loud; everything held in place.
            make ("Genres/Modern Metal", "Modern Metal Vocal",
                  { { compCharacter, fet }, { compThreshold, -20.0f }, { compRatio, 4.0f }, { compAttack, 5.0f }, { compRelease, 120.0f }, { compMakeUp, 3.0f } },
                  { { 2000.0f, 5000.0f, -30.0f, -4.0f, 3.0f, c, 2.0f, 60.0f } }),               // the scream's harsh edge, only when it spikes
            make ("Genres/Modern Metal", "Modern Metal Drums",
                  { { compThreshold, -20.0f }, { compRatio, 4.0f }, { compAttack, 20.0f }, { compRelease, 150.0f }, { compAuto, 1.0f }, { compMakeUp, 2.0f } },
                  { { bottom, 100.0f, -26.0f, -4.0f, 4.0f, c, 3.0f, 80.0f, fetChar } }),        // fast kicks kept even
            make ("Genres/Modern Metal", "Modern Metal Bass",
                  { { compCharacter, fet }, { compThreshold, -20.0f }, { compRatio, 4.0f }, { compAttack, 10.0f }, { compRelease, 150.0f }, { compMakeUp, 2.0f } },
                  { { bottom, 150.0f, -26.0f, -5.0f, 4.0f, c, 15.0f, 150.0f, optoChar } }),     // a steady bottom under the guitars
            make ("Genres/Modern Metal", "Modern Metal Mix Bus",
                  { { compThreshold, -16.0f }, { compRatio, 2.0f }, { compAttack, 30.0f }, { compRelease, 150.0f }, { compAuto, 1.0f }, { compMakeUp, 1.0f } },
                  { { bottom, 120.0f, -24.0f, -3.0f, 3.0f, c, 10.0f, 120.0f, vcaChar, mid } }),
            master ("Modern Metal", -3.0f, -1.5f, -2.0f, 120.0f, 4000.0f),

            // Metalcore: screams and cleans in one track; breakdowns with big lows.
            make ("Genres/Metalcore", "Metalcore Vocal",
                  { { compCharacter, fet }, { compThreshold, -22.0f }, { compRatio, 6.0f }, { compAttack, 3.0f }, { compRelease, 100.0f }, { compMakeUp, 4.0f } },
                  { { 150.0f, 500.0f, -30.0f, -4.0f, 3.0f, c, 10.0f, 120.0f } }),               // chest boom on the screams
            make ("Genres/Metalcore", "Metalcore Drums",
                  { { compThreshold, -22.0f }, { compRatio, 4.0f }, { compAttack, 10.0f }, { compRelease, 100.0f }, { compAuto, 1.0f }, { compMakeUp, 2.0f } },
                  { { 3000.0f, 8000.0f, -32.0f, -3.0f, 2.0f, c, 2.0f, 80.0f } }),               // crashes tamed
            make ("Genres/Metalcore", "Metalcore Bass",
                  { { compCharacter, opto }, { compThreshold, -22.0f }, { compRatio, 4.0f }, { compMakeUp, 2.0f } },
                  { { bottom, 100.0f, -26.0f, -6.0f, 4.0f, c, 10.0f, 150.0f } }),               // breakdown subs held down
            make ("Genres/Metalcore", "Metalcore Mix Bus",
                  { { compThreshold, -18.0f }, { compRatio, 2.5f }, { compAttack, 20.0f }, { compRelease, 120.0f }, { compAuto, 1.0f }, { compMakeUp, 1.5f } },
                  { { bottom, 100.0f, -24.0f, -4.0f, 3.0f, c, 10.0f, 150.0f, vcaChar, mid } }),
            master ("Metalcore", -3.5f, -1.5f, -2.0f, 100.0f, 4500.0f),

            // Djent: very low tunings, everything on the grid, clinical and tight.
            make ("Genres/Djent", "Djent Vocal",
                  { { compThreshold, -20.0f }, { compRatio, 4.0f }, { compAttack, 3.0f }, { compRelease, 100.0f }, { compMakeUp, 2.0f } },
                  { { 9000.0f, top, -42.0f, 3.0f, 2.0f } }),                                    // air brought up when it's quiet
            make ("Genres/Djent", "Djent Drums",
                  { { compThreshold, -20.0f }, { compRatio, 4.0f }, { compAttack, 3.0f }, { compRelease, 60.0f }, { compMakeUp, 2.0f } },
                  { { bottom, 120.0f, -26.0f, -4.0f, 4.0f, c, 2.0f, 60.0f, fetChar } }),        // fast double kicks even
            make ("Genres/Djent", "Djent Bass",
                  { { compCharacter, fet }, { compThreshold, -22.0f }, { compRatio, 5.0f }, { compAttack, 5.0f }, { compRelease, 100.0f }, { compMakeUp, 2.0f } },
                  { { bottom, 120.0f, -26.0f, -5.0f, 4.0f, c, 10.0f, 120.0f },
                    { 2000.0f, 5000.0f, -36.0f, 3.0f, 2.0f, c, 5.0f, 100.0f } }),               // the clank brought up between notes
            make ("Genres/Djent", "Djent Mix Bus",
                  { { compThreshold, -16.0f }, { compRatio, 2.0f }, { compAttack, 10.0f }, { compRelease, 100.0f }, { compMakeUp, 1.0f } },
                  { { bottom, 150.0f, -24.0f, -4.0f, 3.0f, c, 5.0f, 100.0f, vcaChar, mid },
                    { bottom, 150.0f, -40.0f, -10.0f, 4.0f, c, 10.0f, 150.0f, vcaChar, side } }), // low chugs kept in the middle
            master ("Djent", -3.0f, -1.5f, -1.5f, 150.0f, 5000.0f),

            // Nu metal: hip-hop low end under scooped, drop-tuned guitars.
            make ("Genres/Nu Metal", "Nu Metal Vocal",
                  { { compCharacter, fet }, { compThreshold, -18.0f }, { compRatio, 5.0f }, { compAttack, 2.0f }, { compRelease, 80.0f }, { compMakeUp, 3.0f } }, {}),
            make ("Genres/Nu Metal", "Nu Metal Drums",
                  { { compCharacter, fet }, { compThreshold, -18.0f }, { compRatio, 4.0f }, { compAttack, 15.0f }, { compRelease, 120.0f }, { compMakeUp, 2.0f } },
                  { { 150.0f, 400.0f, -26.0f, 3.0f, 2.0f, expand, 1.0f, 60.0f, fetChar, both, 3.0f } }),   // snare hits pushed out
            make ("Genres/Nu Metal", "Nu Metal Bass",
                  { { compCharacter, opto }, { compThreshold, -22.0f }, { compRatio, 4.0f }, { compMakeUp, 3.0f } },
                  { { bottom, 90.0f, -26.0f, -4.0f, 3.0f, c, 20.0f, 200.0f } }),
            make ("Genres/Nu Metal", "Nu Metal Mix Bus",
                  { { compThreshold, -16.0f }, { compRatio, 2.0f }, { compAttack, 30.0f }, { compRelease, 150.0f }, { compAuto, 1.0f }, { compMakeUp, 1.0f } },
                  { { bottom, 120.0f, -24.0f, -3.0f, 2.0f, c, 20.0f, 200.0f, vcaChar, mid } }),
            master ("Nu Metal", -2.5f, -1.5f, -1.5f, 120.0f, 4000.0f),

            // Argent metal: the dense, industrial-djent sound of modern game
            // soundtracks: huge synth sub, saturated chainsaw guitars, choirs.
            make ("Genres/Argent Metal", "Argent Metal Vocal",
                  { { compCharacter, opto }, { compThreshold, -22.0f }, { compRatio, 3.0f }, { compAttack, 20.0f }, { compRelease, 300.0f }, { compMakeUp, 2.0f } },
                  { { 150.0f, 400.0f, -36.0f, 3.0f, 2.0f, c, 20.0f, 200.0f } }),                // the choir's weight brought up
            make ("Genres/Argent Metal", "Argent Metal Drums",
                  { { compCharacter, fet }, { compThreshold, -24.0f }, { compRatio, 6.0f }, { compAttack, 5.0f }, { compRelease, 80.0f }, { compMakeUp, 3.0f } },
                  { { bottom, top, -45.0f, -8.0f, 3.0f, expand, 1.0f, 100.0f, fetChar } }),     // the tails gated short and mechanical
            make ("Genres/Argent Metal", "Argent Metal Bass",
                  { { compCharacter, fet }, { compThreshold, -22.0f }, { compRatio, 6.0f }, { compAttack, 10.0f }, { compRelease, 100.0f }, { compMakeUp, 2.0f } },
                  { { bottom, 80.0f, -26.0f, -5.0f, 4.0f, c, 20.0f, 150.0f, optoChar },
                    { 600.0f, 2500.0f, -36.0f, 3.0f, 2.0f, c, 5.0f, 100.0f } }),               // the growl brought up
            make ("Genres/Argent Metal", "Argent Metal Mix Bus",
                  { { compThreshold, -18.0f }, { compRatio, 3.0f }, { compAttack, 10.0f }, { compRelease, 80.0f }, { compMakeUp, 1.5f } },
                  { { bottom, 90.0f, -24.0f, -4.0f, 3.0f, c, 20.0f, 150.0f, vcaChar, mid },
                    { 3000.0f, 8000.0f, -30.0f, -3.0f, 2.0f, c, 3.0f, 80.0f } }),               // the distortion's harsh edge
            master ("Argent Metal", -3.5f, -1.5f, -2.5f, 90.0f, 3500.0f),

            // Hard rock: natural, mid-forward, room to breathe.
            make ("Genres/Hard Rock", "Hard Rock Vocal",
                  { { compCharacter, opto }, { compThreshold, -20.0f }, { compRatio, 3.0f }, { compAttack, 10.0f }, { compRelease, 250.0f }, { compMakeUp, 2.0f } },
                  { { 2500.0f, 6000.0f, -30.0f, -3.0f, 2.0f, c, 3.0f, 80.0f } }),
            make ("Genres/Hard Rock", "Hard Rock Drums",
                  { { compThreshold, -18.0f }, { compRatio, 3.0f }, { compAttack, 30.0f }, { compRelease, 200.0f }, { compAuto, 1.0f }, { compMakeUp, 1.5f } }, {}),
            make ("Genres/Hard Rock", "Hard Rock Bass",
                  { { compCharacter, opto }, { compThreshold, -22.0f }, { compRatio, 3.0f }, { compAttack, 20.0f }, { compRelease, 250.0f }, { compMakeUp, 2.0f } }, {}),
            make ("Genres/Hard Rock", "Hard Rock Mix Bus",
                  { { compThreshold, -16.0f }, { compRatio, 1.5f }, { compAttack, 30.0f }, { compRelease, 300.0f }, { compAuto, 1.0f }, { compMakeUp, 1.0f } }, {}),
            master ("Hard Rock", -2.0f, -1.0f, -1.5f, 120.0f, 5000.0f),

            // Grunge: raw, mid-heavy and darker; squashed rooms, less polish.
            make ("Genres/Grunge", "Grunge Vocal",
                  { { compCharacter, fet }, { compThreshold, -18.0f }, { compRatio, 4.0f }, { compAttack, 10.0f }, { compRelease, 200.0f }, { compMakeUp, 2.0f } }, {}),
            make ("Genres/Grunge", "Grunge Drums",
                  { { compCharacter, fet }, { compThreshold, -20.0f }, { compRatio, 6.0f }, { compAttack, 10.0f }, { compRelease, 150.0f }, { compMakeUp, 3.0f } }, {}),
            make ("Genres/Grunge", "Grunge Bass",
                  { { compCharacter, opto }, { compThreshold, -20.0f }, { compRatio, 3.0f }, { compAttack, 20.0f }, { compRelease, 300.0f }, { compMakeUp, 2.0f } }, {}),
            make ("Genres/Grunge", "Grunge Mix Bus",
                  { { compCharacter, opto }, { compThreshold, -18.0f }, { compRatio, 2.0f }, { compAttack, 30.0f }, { compRelease, 300.0f }, { compMakeUp, 1.0f } }, {}),
            master ("Grunge", -2.0f, -1.0f, -1.0f, 120.0f, 5000.0f),

            // Symphonic metal: room for an orchestra and a soaring voice.
            make ("Genres/Symphonic Metal", "Symphonic Metal Vocal",
                  { { compCharacter, opto }, { compThreshold, -24.0f }, { compRatio, 2.5f }, { compAttack, 20.0f }, { compRelease, 300.0f }, { compMakeUp, 2.0f } },
                  { { 2000.0f, 5000.0f, -30.0f, -5.0f, 3.0f, c, 3.0f, 80.0f } }),               // shrill high notes
            make ("Genres/Symphonic Metal", "Symphonic Metal Drums",
                  { { compThreshold, -20.0f }, { compRatio, 3.0f }, { compAttack, 20.0f }, { compRelease, 150.0f }, { compAuto, 1.0f }, { compMakeUp, 1.5f } }, {}),
            make ("Genres/Symphonic Metal", "Symphonic Metal Bass",
                  { { compCharacter, opto }, { compThreshold, -22.0f }, { compRatio, 4.0f }, { compMakeUp, 2.0f } },
                  { { bottom, 100.0f, -26.0f, -4.0f, 3.0f, c, 20.0f, 200.0f } }),
            make ("Genres/Symphonic Metal", "Symphonic Metal Mix Bus",
                  { { compCharacter, opto }, { compThreshold, -18.0f }, { compRatio, 1.5f }, { compAttack, 30.0f }, { compRelease, 300.0f }, { compMakeUp, 1.0f } },
                  { { 2000.0f, 5000.0f, -30.0f, -2.0f, 2.0f, c, 5.0f, 100.0f } }),
            master ("Symphonic Metal", -2.0f, -1.0f, -1.5f, 120.0f, 5000.0f),

            // Industrial: mechanical, distorted, pumping.
            make ("Genres/Industrial", "Industrial Vocal",
                  { { compCharacter, fet }, { compThreshold, -16.0f }, { compRatio, 8.0f }, { compAttack, 1.0f }, { compRelease, 60.0f }, { compMakeUp, 3.0f } }, {}),
            make ("Genres/Industrial", "Industrial Drums",
                  { { compCharacter, fet }, { compThreshold, -16.0f }, { compRatio, 8.0f }, { compAttack, 2.0f }, { compRelease, 50.0f }, { compMakeUp, 3.0f } }, {}),
            make ("Genres/Industrial", "Industrial Bass",
                  { { compCharacter, fet }, { compThreshold, -20.0f }, { compRatio, 6.0f }, { compAttack, 5.0f }, { compRelease, 80.0f }, { compMakeUp, 2.0f } },
                  { { bottom, 100.0f, -26.0f, -4.0f, 4.0f, c, 10.0f, 120.0f } }),
            make ("Genres/Industrial", "Industrial Mix Bus",
                  { { compCharacter, fet }, { compThreshold, -16.0f }, { compRatio, 4.0f }, { compAttack, 5.0f }, { compRelease, 80.0f }, { compAuto, 1.0f },
                    { compMakeUp, 2.0f } },
                  { { 3000.0f, 8000.0f, -30.0f, -3.0f, 2.0f, c, 3.0f, 80.0f } }),
            master ("Industrial", -2.5f, -1.5f, -2.0f, 100.0f, 4000.0f),
        };
    }

    // Signature sounds (0.19.0), filed as "Signature Sounds/<sound>": each
    // inspired by how a particular record sounds (see signatureSounds in
    // FactoryRigs.h), from what's published about its production. Our own
    // reading, not anyone's settings; never tuned by ear.
    std::vector<StompPreset> makeSignatures()
    {
        using namespace ParamIDs;
        const auto c = mb::Mode::compress, expand = mb::Mode::expand;
        const auto vcaChar = Character::vca, optoChar = Character::opto, fetChar = Character::fet;
        const auto mid = mb::Stereo::mid, both = mb::Stereo::stereo;
        const auto category = [] (const juce::String& sound) { return "Signature Sounds/" + sound; };
        const auto master = [&] (const juce::String& sound, float lowRange, float midRange, float highRange, float split1, float split2)
        {
            return make (category (sound), sound + " Master", { { compWide, 0.0f }, { mbLookahead, 1.0f } },
                         { { bottom, split1, -24.0f, lowRange, 2.0f, c, 20.0f, 200.0f, vcaChar, mid },
                           { split1, split2, -26.0f, midRange, 1.5f, c, 10.0f, 150.0f },
                           { split2, top, -30.0f, highRange, 1.5f, c, 5.0f, 100.0f } });
        };
        return {
            // Ethereal Djent: very low seven-strings (drop F#), airy cleans over harsh screams, sparse synths.
            make (category ("Ethereal Djent"), "Ethereal Djent Vocal",
                  { { compCharacter, opto }, { compThreshold, -22.0f }, { compRatio, 3.0f }, { compAttack, 10.0f }, { compRelease, 250.0f }, { compMakeUp, 3.0f } },
                  { { 9000.0f, top, -42.0f, 3.0f, 2.0f } }),                                     // the air on the cleans, brought up
            make (category ("Ethereal Djent"), "Ethereal Djent Drums",
                  { { compThreshold, -20.0f }, { compRatio, 4.0f }, { compAttack, 15.0f }, { compRelease, 120.0f }, { compAuto, 1.0f }, { compMakeUp, 2.0f } },
                  { { bottom, 100.0f, -26.0f, -4.0f, 4.0f, c, 3.0f, 80.0f, fetChar } }),
            make (category ("Ethereal Djent"), "Ethereal Djent Bass",
                  { { compCharacter, fet }, { compThreshold, -22.0f }, { compRatio, 4.0f }, { compAttack, 10.0f }, { compRelease, 120.0f }, { compMakeUp, 2.0f } },
                  { { bottom, 80.0f, -26.0f, -5.0f, 4.0f, c, 15.0f, 150.0f, optoChar } }),      // the low F# held steady
            make (category ("Ethereal Djent"), "Ethereal Djent Mix Bus",
                  { { compThreshold, -16.0f }, { compRatio, 2.0f }, { compAttack, 30.0f }, { compRelease, 150.0f }, { compAuto, 1.0f }, { compMakeUp, 1.0f } },
                  { { bottom, 110.0f, -24.0f, -3.0f, 3.0f, c, 10.0f, 120.0f, vcaChar, mid } }),
            master ("Ethereal Djent", -3.0f, -1.5f, -1.5f, 110.0f, 5000.0f),

            // Dark Synth Metal: electronic and very loud (DR 4), intimate vocals, synth bass under drop-tuned guitars.
            make (category ("Dark Synth Metal"), "Dark Synth Metal Vocal",
                  { { compCharacter, opto }, { compThreshold, -24.0f }, { compRatio, 4.0f }, { compAttack, 8.0f }, { compRelease, 200.0f }, { compMakeUp, 4.0f } },
                  { { 150.0f, 500.0f, -36.0f, 3.0f, 2.0f, c, 20.0f, 200.0f } }),                 // close, intimate body
            make (category ("Dark Synth Metal"), "Dark Synth Metal Drums",
                  { { compCharacter, fet }, { compThreshold, -20.0f }, { compRatio, 4.0f }, { compAttack, 10.0f }, { compRelease, 80.0f }, { compMakeUp, 2.5f } },
                  { { bottom, 100.0f, -26.0f, -4.0f, 4.0f, c, 3.0f, 80.0f, fetChar } }),
            make (category ("Dark Synth Metal"), "Dark Synth Metal Bass",
                  { { compCharacter, fet }, { compThreshold, -22.0f }, { compRatio, 5.0f }, { compAttack, 5.0f }, { compRelease, 100.0f }, { compMakeUp, 2.0f } },
                  { { bottom, 70.0f, -26.0f, -5.0f, 4.0f, c, 15.0f, 150.0f, optoChar } }),      // the synth sub kept even
            make (category ("Dark Synth Metal"), "Dark Synth Metal Mix Bus",
                  { { compThreshold, -16.0f }, { compRatio, 2.5f }, { compAttack, 10.0f }, { compRelease, 100.0f }, { compAuto, 1.0f }, { compMakeUp, 1.5f } },
                  { { bottom, 100.0f, -24.0f, -4.0f, 3.0f, c, 10.0f, 120.0f, vcaChar, mid } }),
            master ("Dark Synth Metal", -3.5f, -2.0f, -2.0f, 100.0f, 4500.0f),

            // Raw 90s Nu Metal: played live in the room, few overdubs, a clicky slap bass (DR 8: open).
            make (category ("Raw 90s Nu Metal"), "Raw 90s Nu Metal Vocal",
                  { { compCharacter, fet }, { compThreshold, -18.0f }, { compRatio, 4.0f }, { compAttack, 5.0f }, { compRelease, 100.0f }, { compMakeUp, 2.0f } }, {}),
            make (category ("Raw 90s Nu Metal"), "Raw 90s Nu Metal Drums",
                  { { compCharacter, fet }, { compThreshold, -18.0f }, { compRatio, 3.0f }, { compAttack, 20.0f }, { compRelease, 150.0f }, { compMakeUp, 1.5f } }, {}),
            make (category ("Raw 90s Nu Metal"), "Raw 90s Nu Metal Bass",
                  { { compCharacter, fet }, { compThreshold, -20.0f }, { compRatio, 4.0f }, { compAttack, 3.0f }, { compRelease, 80.0f }, { compMakeUp, 2.0f } },
                  { { 2000.0f, 5000.0f, -36.0f, 3.0f, 2.0f, expand, 1.0f, 60.0f, fetChar, both, 3.0f } }),   // the slap's click pushed out
            make (category ("Raw 90s Nu Metal"), "Raw 90s Nu Metal Mix Bus",
                  { { compThreshold, -16.0f }, { compRatio, 1.5f }, { compAttack, 30.0f }, { compRelease, 300.0f }, { compAuto, 1.0f }, { compMakeUp, 0.5f } }, {}),
            master ("Raw 90s Nu Metal", -2.0f, -1.0f, -1.0f, 120.0f, 5000.0f),

            // Clean Prog Djent: live drums tightly gated (no samples), DI bass with saturation, clinical (DR 6).
            make (category ("Clean Prog Djent"), "Clean Prog Djent Vocal",
                  { { compThreshold, -20.0f }, { compRatio, 4.0f }, { compAttack, 3.0f }, { compRelease, 100.0f }, { compMakeUp, 2.0f } },
                  { { 9000.0f, top, -42.0f, 3.0f, 2.0f } }),
            make (category ("Clean Prog Djent"), "Clean Prog Djent Drums",
                  { { compThreshold, -20.0f }, { compRatio, 4.0f }, { compAttack, 5.0f }, { compRelease, 60.0f }, { compMakeUp, 2.0f } },
                  { { bottom, top, -45.0f, -10.0f, 3.0f, expand, 1.0f, 80.0f, fetChar } }),     // tight: the spill and tails gated
            make (category ("Clean Prog Djent"), "Clean Prog Djent Bass",
                  { { compCharacter, fet }, { compThreshold, -22.0f }, { compRatio, 5.0f }, { compAttack, 5.0f }, { compRelease, 100.0f }, { compMakeUp, 2.0f } },
                  { { bottom, 120.0f, -26.0f, -5.0f, 4.0f, c, 10.0f, 120.0f },
                    { 2000.0f, 5000.0f, -36.0f, 3.0f, 2.0f, c, 5.0f, 100.0f } }),               // the clank brought up
            make (category ("Clean Prog Djent"), "Clean Prog Djent Mix Bus",
                  { { compThreshold, -16.0f }, { compRatio, 2.0f }, { compAttack, 10.0f }, { compRelease, 100.0f }, { compMakeUp, 1.0f } },
                  { { bottom, 150.0f, -24.0f, -4.0f, 3.0f, c, 5.0f, 100.0f, vcaChar, mid } }),
            master ("Clean Prog Djent", -3.0f, -1.5f, -1.5f, 150.0f, 5000.0f),

            // Cinematic Post-Hardcore: choirs, strings and piano as loud as the band, soaring cleans and screams.
            make (category ("Cinematic Post-Hardcore"), "Cinematic Post-Hardcore Vocal",
                  { { compCharacter, fet }, { compThreshold, -22.0f }, { compRatio, 5.0f }, { compAttack, 3.0f }, { compRelease, 100.0f }, { compMakeUp, 3.0f } },
                  { { 150.0f, 500.0f, -30.0f, -4.0f, 3.0f, c, 10.0f, 120.0f } }),
            make (category ("Cinematic Post-Hardcore"), "Cinematic Post-Hardcore Drums",
                  { { compThreshold, -22.0f }, { compRatio, 4.0f }, { compAttack, 10.0f }, { compRelease, 100.0f }, { compAuto, 1.0f }, { compMakeUp, 2.0f } },
                  { { 3000.0f, 8000.0f, -32.0f, -3.0f, 2.0f, c, 2.0f, 80.0f } }),
            make (category ("Cinematic Post-Hardcore"), "Cinematic Post-Hardcore Bass",
                  { { compCharacter, opto }, { compThreshold, -22.0f }, { compRatio, 4.0f }, { compMakeUp, 2.0f } },
                  { { bottom, 100.0f, -26.0f, -6.0f, 4.0f, c, 10.0f, 150.0f } }),
            make (category ("Cinematic Post-Hardcore"), "Cinematic Post-Hardcore Mix Bus",
                  { { compThreshold, -18.0f }, { compRatio, 2.0f }, { compAttack, 20.0f }, { compRelease, 150.0f }, { compAuto, 1.0f }, { compMakeUp, 1.0f } },
                  { { bottom, 100.0f, -24.0f, -3.0f, 3.0f, c, 10.0f, 150.0f, vcaChar, mid } }),
            master ("Cinematic Post-Hardcore", -3.0f, -1.5f, -2.0f, 100.0f, 4500.0f),

            // Swedish Melodic Death: polished and larger than life, loud (DR 5), clicky kick, mid-forward guitars.
            make (category ("Swedish Melodic Death"), "Swedish Melodic Death Vocal",
                  { { compCharacter, fet }, { compThreshold, -22.0f }, { compRatio, 5.0f }, { compAttack, 3.0f }, { compRelease, 100.0f }, { compMakeUp, 3.0f } }, {}),
            make (category ("Swedish Melodic Death"), "Swedish Melodic Death Drums",
                  { { compCharacter, fet }, { compThreshold, -20.0f }, { compRatio, 4.0f }, { compAttack, 5.0f }, { compRelease, 80.0f }, { compMakeUp, 2.0f } },
                  { { bottom, 120.0f, -26.0f, -4.0f, 4.0f, c, 2.0f, 60.0f, fetChar } }),        // fast double kicks even
            make (category ("Swedish Melodic Death"), "Swedish Melodic Death Bass",
                  { { compCharacter, opto }, { compThreshold, -22.0f }, { compRatio, 4.0f }, { compMakeUp, 2.0f } },
                  { { 500.0f, 1500.0f, -36.0f, 3.0f, 2.0f, c, 5.0f, 100.0f } }),               // its grind brought up, so it isn't lost
            make (category ("Swedish Melodic Death"), "Swedish Melodic Death Mix Bus",
                  { { compThreshold, -18.0f }, { compRatio, 2.5f }, { compAttack, 15.0f }, { compRelease, 100.0f }, { compAuto, 1.0f }, { compMakeUp, 1.5f } },
                  { { bottom, 110.0f, -24.0f, -3.0f, 3.0f, c, 10.0f, 120.0f, vcaChar, mid } }),
            master ("Swedish Melodic Death", -3.5f, -2.0f, -2.0f, 110.0f, 4500.0f),

            // Early 2000s Metalcore: razor-sharp, sampled-sounding kick, the bass audible, drop C (DR 7).
            make (category ("Early 2000s Metalcore"), "Early 2000s Metalcore Vocal",
                  { { compCharacter, fet }, { compThreshold, -20.0f }, { compRatio, 5.0f }, { compAttack, 3.0f }, { compRelease, 100.0f }, { compMakeUp, 3.0f } },
                  { { 150.0f, 500.0f, -30.0f, -4.0f, 3.0f, c, 10.0f, 120.0f } }),
            make (category ("Early 2000s Metalcore"), "Early 2000s Metalcore Drums",
                  { { compThreshold, -20.0f }, { compRatio, 4.0f }, { compAttack, 20.0f }, { compRelease, 120.0f }, { compAuto, 1.0f }, { compMakeUp, 2.0f } }, {}),
            make (category ("Early 2000s Metalcore"), "Early 2000s Metalcore Bass",
                  { { compCharacter, fet }, { compThreshold, -22.0f }, { compRatio, 4.0f }, { compAttack, 10.0f }, { compRelease, 150.0f }, { compMakeUp, 2.0f } },
                  { { 800.0f, 2500.0f, -36.0f, 3.0f, 2.0f, c, 5.0f, 100.0f } }),               // audible: the mids brought up
            make (category ("Early 2000s Metalcore"), "Early 2000s Metalcore Mix Bus",
                  { { compThreshold, -16.0f }, { compRatio, 2.0f }, { compAttack, 30.0f }, { compRelease, 150.0f }, { compAuto, 1.0f }, { compMakeUp, 1.0f } },
                  { { bottom, 100.0f, -24.0f, -3.0f, 3.0f, c, 10.0f, 150.0f, vcaChar, mid } }),
            master ("Early 2000s Metalcore", -2.5f, -1.5f, -1.5f, 100.0f, 4500.0f),

            // Polished Nu Metal: multitracked guitars and vocals, rapped and screamed, samples and scratches (DR 6).
            make (category ("Polished Nu Metal"), "Polished Nu Metal Vocal",
                  { { compCharacter, fet }, { compThreshold, -20.0f }, { compRatio, 6.0f }, { compAttack, 2.0f }, { compRelease, 80.0f }, { compMakeUp, 3.0f } },
                  { { 2500.0f, 6000.0f, -30.0f, -3.0f, 2.0f, c, 2.0f, 60.0f } }),
            make (category ("Polished Nu Metal"), "Polished Nu Metal Drums",
                  { { compCharacter, fet }, { compThreshold, -18.0f }, { compRatio, 4.0f }, { compAttack, 15.0f }, { compRelease, 100.0f }, { compMakeUp, 2.0f } },
                  { { 150.0f, 400.0f, -26.0f, 3.0f, 2.0f, expand, 1.0f, 60.0f, fetChar, both, 3.0f } }),   // snare hits pushed out
            make (category ("Polished Nu Metal"), "Polished Nu Metal Bass",
                  { { compCharacter, opto }, { compThreshold, -22.0f }, { compRatio, 4.0f }, { compMakeUp, 3.0f } },
                  { { bottom, 90.0f, -26.0f, -4.0f, 3.0f, c, 20.0f, 200.0f } }),
            make (category ("Polished Nu Metal"), "Polished Nu Metal Mix Bus",
                  { { compThreshold, -16.0f }, { compRatio, 2.0f }, { compAttack, 30.0f }, { compRelease, 150.0f }, { compAuto, 1.0f }, { compMakeUp, 1.0f } },
                  { { bottom, 120.0f, -24.0f, -3.0f, 2.0f, c, 20.0f, 200.0f, vcaChar, mid } }),
            master ("Polished Nu Metal", -2.5f, -1.5f, -1.5f, 120.0f, 4000.0f),
        };
    }
}

bool CompPresetTraits::holds (const juce::String& id)
{
    return (id.startsWith ("comp") && id != ParamIDs::compOn) || id.startsWith ("mb");
}

std::vector<StompPreset> CompPresetTraits::makeFactory()
{
    // Our own starting points, from common mixing moves, not copies of
    // anyone's presets. Thresholds depend on how loud the material is: set
    // THRESHOLD by ear (a few dB of gain reduction is usually plenty).
    using namespace ParamIDs;
    const auto expand = mb::Mode::expand;
    std::vector<StompPreset> presets {
        make ("", "Default", {}, {}),

        // Vocals
        make ("Vocals", "Vocal Leveler", { { compCharacter, opto }, { compThreshold, -24.0f }, { compRatio, 3.0f }, { compAttack, 15.0f },
                                           { compRelease, 250.0f }, { compMakeUp, 3.0f } }, {}),
        make ("Vocals", "Vocal Smooth & Bright", { { compCharacter, opto }, { compThreshold, -22.0f }, { compRatio, 2.5f }, { compMakeUp, 2.0f } },
              { { 2500.0f, 7000.0f, -32.0f, -5.0f, 3.0f, mb::Mode::compress, 3.0f, 80.0f },   // the harsh part, tamed when it pokes out
                { 9000.0f, top, -42.0f, 3.0f, 2.0f } }),                                       // the air, brought up when it's quiet
        make ("Vocals", "Vocal Body", { { compWide, 0.0f } },
              { { 150.0f, 500.0f, -36.0f, 4.0f, 2.0f, mb::Mode::compress, 20.0f, 200.0f } }),  // fullness brought up from below
        // (0.19.0)
        make ("Vocals", "Rap Vocal Tight", { { compCharacter, fet }, { compThreshold, -20.0f }, { compRatio, 6.0f }, { compAttack, 2.0f },
                                             { compRelease, 80.0f }, { compMakeUp, 3.0f } }, {}),
        make ("Vocals", "Scream Control", { { compCharacter, fet }, { compThreshold, -18.0f }, { compRatio, 8.0f }, { compAttack, 1.0f },
                                            { compRelease, 60.0f }, { compMakeUp, 3.0f } },
              { { 2000.0f, 5000.0f, -30.0f, -4.0f, 3.0f, mb::Mode::compress, 2.0f, 60.0f } }),   // the harsh edge when it spikes
        make ("Vocals", "Backing Vocals Tuck", { { compCharacter, opto }, { compThreshold, -26.0f }, { compRatio, 4.0f }, { compMakeUp, 1.0f } }, {}),
        make ("Vocals", "Podcast Voice", { { compCharacter, opto }, { compThreshold, -24.0f }, { compRatio, 3.0f }, { compAttack, 15.0f },
                                           { compRelease, 300.0f }, { compMakeUp, 4.0f } },
              { { 80.0f, 250.0f, -30.0f, -3.0f, 2.0f, mb::Mode::compress, 15.0f, 200.0f } }),     // the chesty proximity boom

        // Drums
        make ("Drums", "Drum Bus Glue", { { compThreshold, -20.0f }, { compRatio, 2.0f }, { compAttack, 30.0f }, { compRelease, 150.0f },
                                          { compAuto, 1.0f }, { compMakeUp, 2.0f } }, {}),
        make ("Drums", "Kick Tight", { { compWide, 0.0f } },
              { { bottom, 120.0f, -24.0f, -6.0f, 4.0f, mb::Mode::compress, 8.0f, 120.0f, Character::fet } }),
        make ("Drums", "Snare Punch", { { compWide, 0.0f } },
              { { 150.0f, 400.0f, -24.0f, 4.0f, 2.0f, expand, 1.0f, 60.0f, Character::fet, mb::Stereo::stereo, 3.0f } }),
        make ("Drums", "Room Gate", { { compWide, 0.0f } },
              { { bottom, top, -40.0f, -18.0f, 3.0f, expand, 1.0f, 120.0f, Character::fet } }),
        // (0.19.0)
        make ("Drums", "Toms Punch", { { compWide, 0.0f } },
              { { 80.0f, 300.0f, -26.0f, 3.0f, 2.0f, expand, 1.0f, 80.0f, Character::fet, mb::Stereo::stereo, 3.0f } }),
        make ("Drums", "Overheads Smooth", { { compWide, 0.0f } },
              { { 3000.0f, 10000.0f, -32.0f, -3.0f, 2.0f, mb::Mode::compress, 3.0f, 100.0f } }),
        make ("Drums", "Hi-Hat Tame", { { compWide, 0.0f } },
              { { 6000.0f, top, -34.0f, -4.0f, 3.0f, mb::Mode::compress, 1.0f, 60.0f } }),
        make ("Drums", "Room Crush", { { compCharacter, fet }, { compThreshold, -30.0f }, { compRatio, 10.0f }, { compAttack, 1.0f },
                                       { compRelease, 60.0f }, { compMakeUp, 6.0f } }, {}),

        // Bass and instruments
        make ("Instruments", "Bass Even", { { compCharacter, opto }, { compThreshold, -22.0f }, { compRatio, 4.0f }, { compMakeUp, 3.0f } },
              { { bottom, 90.0f, -26.0f, -4.0f, 3.0f, mb::Mode::compress, 20.0f, 200.0f } }),
        make ("Instruments", "Guitar Fizz Control", { { compWide, 0.0f } },
              { { 3000.0f, 9000.0f, -34.0f, -6.0f, 3.0f, mb::Mode::compress, 2.0f, 80.0f } }),
        make ("Instruments", "Piano & Keys Even", { { compCharacter, opto }, { compThreshold, -24.0f }, { compRatio, 2.0f }, { compMakeUp, 1.5f } },
              { { bottom, 200.0f, -28.0f, -3.0f, 2.0f } }),
        // (0.19.0)
        make ("Instruments", "808 Steady", { { compCharacter, fet }, { compThreshold, -20.0f }, { compRatio, 5.0f }, { compAttack, 10.0f },
                                             { compRelease, 120.0f }, { compMakeUp, 2.0f } },
              { { bottom, 100.0f, -26.0f, -4.0f, 4.0f, mb::Mode::compress, 15.0f, 150.0f, Character::opto } }),
        make ("Instruments", "Rhythm Guitar Tight", { { compWide, 0.0f } },
              { { 100.0f, 300.0f, -30.0f, -4.0f, 3.0f, mb::Mode::compress, 5.0f, 100.0f } }),   // palm-mute boom when it blooms
        make ("Instruments", "Lead Guitar Sustain", { { compCharacter, opto }, { compThreshold, -26.0f }, { compRatio, 4.0f }, { compAttack, 10.0f },
                                                      { compRelease, 300.0f }, { compMakeUp, 3.0f } }, {}),
        make ("Instruments", "Acoustic Even", { { compCharacter, opto }, { compThreshold, -24.0f }, { compRatio, 3.0f }, { compMakeUp, 2.0f } },
              { { 100.0f, 300.0f, -28.0f, -3.0f, 2.0f, mb::Mode::compress, 15.0f, 150.0f } }),
        make ("Instruments", "Synth Pad Glue", { { compCharacter, opto }, { compThreshold, -20.0f }, { compRatio, 2.0f }, { compAttack, 30.0f },
                                                 { compRelease, 400.0f }, { compMakeUp, 1.0f } }, {}),

        // Mix and mastering
        make ("Mix & Master", "Mix Bus Glue", { { compThreshold, -16.0f }, { compRatio, 1.5f }, { compAttack, 30.0f }, { compRelease, 200.0f },
                                                { compAuto, 1.0f }, { compMakeUp, 1.0f } }, {}),
        make ("Mix & Master", "Master Four Bands", { { compWide, 0.0f }, { mbLookahead, 1.0f } },
              { { bottom, 120.0f, -24.0f, -3.0f, 2.0f, mb::Mode::compress, 20.0f, 200.0f },
                { 120.0f, 800.0f, -24.0f, -2.0f, 1.5f, mb::Mode::compress, 15.0f, 150.0f },
                { 800.0f, 5000.0f, -26.0f, -2.0f, 1.5f, mb::Mode::compress, 10.0f, 120.0f },
                { 5000.0f, top, -30.0f, -2.0f, 1.5f, mb::Mode::compress, 5.0f, 100.0f } }),
        make ("Mix & Master", "Low End Control", { { compWide, 0.0f } },
              { { bottom, 150.0f, -24.0f, -4.0f, 3.0f, mb::Mode::compress, 20.0f, 150.0f, Character::vca, mb::Stereo::mid },
                { bottom, 150.0f, -40.0f, -12.0f, 4.0f, mb::Mode::compress, 10.0f, 150.0f, Character::vca, mb::Stereo::side } }),
        make ("Mix & Master", "De-Harsh", { { compWide, 0.0f } },
              { { 2000.0f, 6000.0f, -30.0f, -3.0f, 2.0f, mb::Mode::compress, 3.0f, 80.0f } }),
        make ("Mix & Master", "Air Lift", { { compWide, 0.0f } },
              { { 10000.0f, top, -40.0f, 3.0f, 2.0f, mb::Mode::compress, 20.0f, 200.0f } }),

        // Fixes (0.19.0): one job each, only when the problem is there.
        make ("Fixes", "De-Mud", { { compWide, 0.0f } }, { { 200.0f, 500.0f, -30.0f, -4.0f, 3.0f, mb::Mode::compress, 10.0f, 120.0f } }),
        make ("Fixes", "De-Ess", { { compWide, 0.0f } }, { { 5000.0f, 9000.0f, -34.0f, -6.0f, 4.0f, mb::Mode::compress, 0.5f, 50.0f } }),
        make ("Fixes", "Boom Control", { { compWide, 0.0f } }, { { bottom, 150.0f, -26.0f, -5.0f, 4.0f, mb::Mode::compress, 15.0f, 150.0f } }),
        make ("Fixes", "Harsh Upper Mids", { { compWide, 0.0f } }, { { 2000.0f, 5000.0f, -30.0f, -4.0f, 3.0f, mb::Mode::compress, 2.0f, 80.0f } }),
        make ("Fixes", "Honk Control", { { compWide, 0.0f } }, { { 600.0f, 1200.0f, -30.0f, -4.0f, 3.0f, mb::Mode::compress, 5.0f, 100.0f } }),
        make ("Fixes", "Fizz Control", { { compWide, 0.0f } }, { { 6000.0f, top, -36.0f, -5.0f, 3.0f, mb::Mode::compress, 1.0f, 60.0f } }),
        make ("Fixes", "Boxy Snare", { { compWide, 0.0f } }, { { 300.0f, 600.0f, -28.0f, -4.0f, 3.0f, mb::Mode::compress, 3.0f, 80.0f } }),
        make ("Fixes", "Thin Vocal Body", { { compWide, 0.0f } }, { { 150.0f, 400.0f, -38.0f, 3.0f, 2.0f, mb::Mode::compress, 20.0f, 200.0f } }),

        // Creative
        make ("Creative", "Upward Squash", { { compWide, 0.0f } },
              { { bottom, 250.0f, -30.0f, 12.0f, 10.0f }, { 250.0f, 3000.0f, -30.0f, 12.0f, 10.0f }, { 3000.0f, top, -30.0f, 12.0f, 10.0f } }),
    };

    // Genre presets (0.15.0) come after these, each genre in its own submenu;
    // then the signature sounds (0.19.0).
    // (0.19.0) The plain categories become submenus under STARTING POINTS,
    // as the genres are, so the menu (now much longer) fits on the screen.
    for (auto& preset : presets)
        if (preset.category.isNotEmpty())
            preset.category = "Starting Points/" + preset.category;
    auto genres = makeGenres();
    presets.insert (presets.end(), genres.begin(), genres.end());
    auto signatures = makeSignatures();
    presets.insert (presets.end(), signatures.begin(), signatures.end());
    return presets;
}
