#include "FactoryRigs.h"

#include "CompPresets.h"
#include "DominionPresets.h"
#include "LimiterPresets.h"
#include "Parameters.h"
#include "dsp/dominion/Dominion.h"
#include "dsp/effects/MasterLimiter.h"
#include "dsp/eq/EqDesign.h"

// PR7-E's factory rigs (0.6.0): starting points for the EQ stomp, filed by
// what they're for. Bank A: vocals and voice; B: drums; C: bass, guitars
// and keys; D: the full mix; E: mastering.
//
// They're common mixing moves (cut the mud, add presence and air, tame
// sibilance and boom), not copies of anyone's presets, and every track is
// different: they're meant to be tweaked by ear. Problems that come and go
// (sibilance, boomy notes, harsh hits) use dynamic bands with AUTO, so they
// only act when the problem is there. Mastering rigs move a dB or two at
// most, and end with the LIMITER as a -1 dB safety ceiling. None of them
// touch PRESENCE or RIG LEVEL.
namespace
{
    using eq::Shape;
    using eq::Placement;

    // A band, as a rig sets it. Slope in dB/oct; a range makes it dynamic (AUTO threshold).
    struct BandSpec
    {
        Shape shape;
        double frequency, gainDb = 0.0, q = 1.0;
        int slopeDb = 12;
        Placement placement = Placement::stereo;
        double dynamicRangeDb = 0.0;
        double attack = 0.5, release = 0.5;
    };

    BandSpec cut (Shape shape, double hz, int slopeDb, Placement placement = Placement::stereo)
    {
        return { shape, hz, 0.0, 1.0, slopeDb, placement };
    }

    BandSpec bell (double hz, double gainDb, double q, Placement placement = Placement::stereo)
    {
        return { Shape::bell, hz, gainDb, q, 12, placement };
    }

    BandSpec shelf (Shape shape, double hz, double gainDb, double q = 0.7, Placement placement = Placement::stereo)
    {
        return { shape, hz, gainDb, q, 12, placement };
    }

    BandSpec tilt (double gainDb) { return { Shape::flatTilt, 1000.0, gainDb }; }   // gainDb / 10 dB per octave round 1 kHz

    // A band that moves by `rangeDb` when its part gets loud (and `gainDb` otherwise).
    BandSpec dynamic (BandSpec band, double rangeDb, double attack = 0.5, double release = 0.5)
    {
        band.dynamicRangeDb = rangeDb;
        band.attack = attack;
        band.release = release;
        return band;
    }

    int slopeIndex (int dbPerOctave)
    {
        for (int i = 0; i < eq::numSlopes; ++i)
            if (eq::slopes[i] == dbPerOctave)
                return i;
        jassertfalse;
        return eq::defaultSlope;
    }

    Rig makeRig (const juce::String& name, int bank, int number, const juce::StringArray& tags, const juce::String& notes,
                 const std::vector<BandSpec>& bands)
    {
        Rig rig;
        rig.name = name;
        rig.bank = bank;
        rig.number = number;
        rig.tags = tags;
        rig.notes = notes;
        rig.parameters[ParamIDs::eqOn] = 1.0f;
        for (int i = 0; i < (int) bands.size() && i < ParamIDs::eqNumBands; ++i)
        {
            const auto& b = bands[(size_t) i];
            const auto set = [&] (const char* what, double value) { rig.parameters[ParamIDs::eqBand (i, what)] = (float) value; };
            set ("Used", 1.0);
            set ("On", 1.0);
            set ("Shape", (double) b.shape);
            set ("Freq", b.frequency);
            set ("Gain", b.gainDb);
            set ("Q", b.q);
            set ("Slope", slopeIndex (b.slopeDb));
            set ("Place", (double) b.placement);
            if (std::abs (b.dynamicRangeDb) > 0.0)
            {
                set ("Dyn", b.dynamicRangeDb);
                set ("DynAuto", 1.0);
                set ("Attack", b.attack);
                set ("Release", b.release);
            }
        }
        return rig;
    }

    constexpr int vocals = 0, drums = 1, instruments = 2, mix = 3, mastering = 4;

    // A genre's rigs (0.15.0): one for each part of a song, at the same
    // number in each bank (x11 to x19, a genre each; bass at C21 to C29,
    // after the guitars). Each takes COMP's and DOMINION's settings from the
    // genre's presets of the same name, so a stomp's preset menu holds
    // exactly what the rig has: COMP on the vocal, drums, bass, mix and
    // master; DOMINION on the vocal, guitars and mix.
    struct GenrePart
    {
        const char* notes;
        std::vector<BandSpec> bands;
    };

    void applyPreset (Rig& rig, const std::vector<StompPreset>& presets, const juce::String& name, const char* onId)
    {
        for (const auto& preset : presets)
            if (preset.name == name)
            {
                for (const auto& [id, value] : preset.parameters)
                    if (juce::String (id) != LimiterPresetTraits::targetKey)   // the LIMITER page's TARGET: not a parameter
                        rig.parameters[id] = value;
                rig.parameters[onId] = 1.0f;
                return;
            }
        jassertfalse;   // a genre rig names a preset that isn't there
    }

    // A set of six rigs, one for each part of a song: a genre's (0.15.0, at
    // x11 to x19, bass C21 to C29) or a signature sound's (0.19.0, at x31 to
    // x38, bass C41 to C48, with a LIMITER preset on the master too).
    struct SetKind { const char* group; const char* tag; int first, bassFirst; bool limiter; };
    const SetKind genreSet { "Genres", "Genre", 11, 21, false }, signatureSet { "Signature Sounds", "Signature", 31, 41, true };

    void addSet (std::vector<Rig>& rigs, const SetKind& kind, int index, const juce::String& name, const GenrePart& vocal, const GenrePart& drumBus,
                 const GenrePart& guitars, const GenrePart& bass, const GenrePart& mixBus, const GenrePart& master)
    {
        struct Part { const GenrePart& spec; const char* name; int bank; bool isBass; const char* tag; const char* comp; const char* dominion; };
        const Part parts[] = {
            { vocal,   "Vocal",   vocals,      false, "Vocal",     "Vocal",   "Vocal" },
            { drumBus, "Drums",   drums,       false, "Drums",     "Drums",   nullptr },
            { guitars, "Guitars", instruments, false, "Guitar",    nullptr,   "Guitars" },
            { bass,    "Bass",    instruments, true,  "Bass",      "Bass",    nullptr },
            { mixBus,  "Mix",     mix,         false, "Mix",       "Mix Bus", "Mix" },
            { master,  "Master",  mastering,   false, "Mastering", "Master",  nullptr },
        };
        for (const auto& part : parts)
        {
            auto rig = makeRig (name + " " + part.name, part.bank, (part.isBass ? kind.bassFirst : kind.first) + index, { name, part.tag, kind.tag },
                                part.spec.notes, part.spec.bands);
            if (part.comp != nullptr)
            {
                applyPreset (rig, CompPresets::factory(), name + " " + part.comp, ParamIDs::compOn);
                rig.notes << " CONSTRICTOR: its \"" << name << " " << part.comp << "\" preset.";
            }
            if (part.dominion != nullptr)
            {
                applyPreset (rig, DominionPresets::factory(), name + " " + part.dominion, ParamIDs::domOn);
                rig.notes << " DOMINION: its \"" << name << " " << part.dominion << "\" preset.";
            }
            if (kind.limiter && part.bank == mastering)
            {
                applyPreset (rig, LimiterPresets::factory(), name + " Master", ParamIDs::limitOn);
                rig.notes << " LIMITER: its \"" << name << " Master\" preset (GAIN set for a mix peaking around -6 dBFS: "
                             "turn it to taste).";
            }
            rigs.push_back (std::move (rig));
        }
    }

    void addGenre (std::vector<Rig>& rigs, int index, const juce::String& genre, const GenrePart& vocal, const GenrePart& drumBus,
                   const GenrePart& guitars, const GenrePart& bass, const GenrePart& mixBus, const GenrePart& master)
    {
        addSet (rigs, genreSet, index, genre, vocal, drumBus, guitars, bass, mixBus, master);
    }

    void addSignature (std::vector<Rig>& rigs, const juce::String& sound, const GenrePart& vocal, const GenrePart& drumBus,
                       const GenrePart& guitars, const GenrePart& bass, const GenrePart& mixBus, const GenrePart& master)
    {
        addSet (rigs, signatureSet, signatureSounds.indexOf (sound), sound, vocal, drumBus, guitars, bass, mixBus, master);
    }
}

std::vector<Rig> makeFactoryRigs()
{
    std::vector<Rig> rigs;

    Rig init;
    init.name = "Init";
    init.bank = vocals;   // A01
    init.number = 1;
    init.tags = { "Init" };
    init.notes = "Everything off: a starting point.";
    rigs.push_back (init);

    // A: vocals and voice
    rigs.push_back (makeRig ("Lead Vocal", vocals, 2, { "Vocal", "Lead" },
        "Clears rumble and boxiness, adds presence and air, and tames sibilance when it's there (a dynamic band at 6.5 kHz).",
        { cut (Shape::lowCut, 80.0, 18), bell (300.0, -2.0, 1.2), bell (3000.0, 2.0, 0.8), shelf (Shape::highShelf, 10000.0, 2.5),
          dynamic (bell (6500.0, 0.0, 3.0), -6.0) }));
    rigs.push_back (makeRig ("Vocal De-Esser", vocals, 3, { "Vocal", "De-ess", "Dynamic" },
        "Only the S and T sounds: a dynamic band at 6.5 kHz pulls down sibilance as it happens and leaves the rest alone. "
        "If the S sits higher or lower in your singer's voice, solo the band and move it there.",
        { dynamic (bell (6500.0, 0.0, 2.0), -8.0, 0.25, 0.4) }));
    rigs.push_back (makeRig ("Rap Vocal", vocals, 4, { "Vocal", "Rap", "Lead" },
        "Tight and forward: a steeper low cut, less low-mid weight, more bite at 4 kHz, sibilance kept in check.",
        { cut (Shape::lowCut, 100.0, 24), bell (220.0, -2.5, 1.0), bell (4000.0, 3.0, 1.0), shelf (Shape::highShelf, 12000.0, 2.0),
          dynamic (bell (6000.0, 0.0, 2.5), -6.0) }));
    rigs.push_back (makeRig ("Backing Vocals", vocals, 5, { "Vocal", "Backing" },
        "Thinner and a little further back than the lead, so they support it rather than crowd it, with an airy top.",
        { cut (Shape::lowCut, 150.0, 24), bell (400.0, -2.0, 0.8), bell (3000.0, -1.5, 1.0), shelf (Shape::highShelf, 10000.0, 2.0),
          dynamic (bell (6500.0, 0.0, 2.5), -6.0) }));
    rigs.push_back (makeRig ("Spoken Voice", vocals, 6, { "Voice", "Podcast", "Voice-over" },
        "For speech: clear, without boom or harsh S's. Dynamic bands catch popped P's and B's (150 Hz) and sibilance (6 kHz).",
        { cut (Shape::lowCut, 70.0, 24), dynamic (bell (150.0, 0.0, 1.0), -6.0), bell (300.0, -2.0, 1.0), bell (2500.0, 2.0, 1.0),
          dynamic (bell (6000.0, 0.0, 2.5), -6.0) }));
    rigs.push_back (makeRig ("Vocal Mud Control", vocals, 7, { "Vocal", "Dynamic", "Mud" },
        "Turns down the low mids (300 Hz) only on the notes where they build up, so the voice keeps its body elsewhere.",
        { cut (Shape::lowCut, 90.0, 18), dynamic (bell (300.0, 0.0, 1.2), -5.0) }));

    // B: drums
    rigs.push_back (makeRig ("Kick Punch", drums, 1, { "Drums", "Kick" },
        "Weight at 60 Hz, less cardboard box at 350 Hz, more beater click at 4 kHz; rumble below 30 Hz cut.",
        { cut (Shape::lowCut, 30.0, 24), bell (60.0, 3.0, 1.2), bell (350.0, -4.0, 1.5), bell (4000.0, 3.0, 1.5) }));
    rigs.push_back (makeRig ("Kick Boom Control", drums, 2, { "Drums", "Kick", "Dynamic" },
        "For a kick whose boom swamps the mix: the lows come down when they bloom, and a touch of click helps it cut through.",
        { cut (Shape::lowCut, 30.0, 24), dynamic (shelf (Shape::lowShelf, 80.0, 0.0), -5.0), bell (4000.0, 2.0, 1.2) }));
    rigs.push_back (makeRig ("Snare Crack", drums, 3, { "Drums", "Snare" },
        "Body at 200 Hz, less ring at 900 Hz, crack at 5 kHz and a little sizzle on top.",
        { cut (Shape::lowCut, 80.0, 18), bell (200.0, 2.0, 1.5), bell (900.0, -2.0, 2.0), bell (5000.0, 3.0, 1.0),
          shelf (Shape::highShelf, 10000.0, 1.5) }));
    rigs.push_back (makeRig ("Hi-Hat Tame", drums, 4, { "Drums", "Hi-hat", "Cymbals", "Dynamic" },
        "Hats without the spit: the lows (kick and snare bleed) cut, harsh hits around 8 kHz pulled down as they come.",
        { cut (Shape::lowCut, 250.0, 18), dynamic (bell (8000.0, 0.0, 1.5), -5.0), shelf (Shape::highShelf, 12000.0, 1.0) }));
    rigs.push_back (makeRig ("Overheads", drums, 5, { "Drums", "Overheads", "Cymbals" },
        "Cleaner cymbals and room: less low mid, harsh crashes tamed around 3.5 kHz, and air on top.",
        { cut (Shape::lowCut, 120.0, 18), bell (400.0, -2.0, 1.0), dynamic (bell (3500.0, 0.0, 1.5), -4.0),
          shelf (Shape::highShelf, 12000.0, 2.0) }));
    rigs.push_back (makeRig ("Drum Bus", drums, 6, { "Drums", "Bus" },
        "For the whole kit: a little weight and snap, less boxiness, and a clean bottom.",
        { cut (Shape::lowCut, 25.0, 24), bell (60.0, 1.5, 0.9), bell (400.0, -1.5, 1.0), bell (5000.0, 1.5, 0.8),
          shelf (Shape::highShelf, 12000.0, 1.5) }));

    // C: bass, guitars and keys
    rigs.push_back (makeRig ("Bass DI", instruments, 1, { "Bass" },
        "A direct bass: an even bottom (boomy notes come down, at 70 Hz), less mud at 250 Hz, a little growl at 800 Hz.",
        { cut (Shape::lowCut, 35.0, 24), dynamic (bell (70.0, 1.5, 1.4), -4.0), bell (250.0, -3.0, 1.2), bell (800.0, 1.5, 1.0) }));
    rigs.push_back (makeRig ("808 Control", instruments, 2, { "Bass", "808", "Dynamic" },
        "Evens out an 808 whose notes boom unevenly, and lifts its upper harmonics (1.2 kHz) so it's heard on small speakers.",
        { cut (Shape::lowCut, 25.0, 24), dynamic (bell (55.0, 2.0, 1.0), -5.0), bell (1200.0, 2.0, 1.0),
          cut (Shape::highCut, 8000.0, 12) }));
    rigs.push_back (makeRig ("Electric Guitar", instruments, 3, { "Guitar", "Electric" },
        "Makes room in a mix: no low end below 90 Hz, less mud, a little bite at 2.5 kHz, and the fizz above 11 kHz rolled off.",
        { cut (Shape::lowCut, 90.0, 18), bell (400.0, -2.0, 1.2), bell (2500.0, 1.5, 1.0), cut (Shape::highCut, 11000.0, 12) }));
    rigs.push_back (makeRig ("Acoustic Guitar", instruments, 4, { "Guitar", "Acoustic" },
        "Body boom (200 Hz) comes down when it builds up, a less honky middle, more sparkle.",
        { cut (Shape::lowCut, 80.0, 18), dynamic (bell (200.0, 0.0, 1.2), -5.0), bell (1000.0, -1.0, 1.0), bell (5000.0, 2.0, 0.9),
          shelf (Shape::highShelf, 11000.0, 2.0) }));
    rigs.push_back (makeRig ("Piano", instruments, 5, { "Keys", "Piano" },
        "Clearer in a mix: less low-mid muddiness, a little presence and air.",
        { cut (Shape::lowCut, 40.0, 18), bell (300.0, -2.0, 1.0), bell (3000.0, 1.5, 0.8), shelf (Shape::highShelf, 10000.0, 1.5) }));
    rigs.push_back (makeRig ("Pad, Make Room", instruments, 6, { "Keys", "Synth", "Pad" },
        "Pushes a pad or keys behind the song: the lows cut, and a dip around 2.5 kHz leaves room for a vocal.",
        { cut (Shape::lowCut, 150.0, 18), bell (2500.0, -2.5, 0.8), shelf (Shape::highShelf, 9000.0, -1.5) }));

    // D: the full mix
    rigs.push_back (makeRig ("Mix Bus Polish", mix, 1, { "Mix", "Bus" },
        "Gentle moves for a whole mix: a touch of weight, a little less low-mid, a little air, and nothing below 25 Hz.",
        { cut (Shape::lowCut, 25.0, 12), shelf (Shape::lowShelf, 80.0, 1.0), bell (300.0, -1.0, 0.8),
          shelf (Shape::highShelf, 10000.0, 1.5) }));
    rigs.push_back (makeRig ("Mix Mud Control", mix, 2, { "Mix", "Dynamic", "Mud" },
        "Turns down the low mids (280 Hz) only where they build up, so the mix stays full but clear.",
        { cut (Shape::lowCut, 25.0, 12), dynamic (bell (280.0, 0.0, 1.0), -4.0) }));
    rigs.push_back (makeRig ("Mix Harshness Control", mix, 3, { "Mix", "Dynamic", "Harsh" },
        "For a mix that gets harsh when it's busy: 3 kHz and 7 kHz come down a little only in those moments.",
        { dynamic (bell (3000.0, 0.0, 1.2), -3.0), dynamic (bell (7000.0, 0.0, 1.5), -3.0) }));
    rigs.push_back (makeRig ("Mix Wider Top", mix, 4, { "Mix", "Stereo", "Width" },
        "Wider without touching the middle: the sides' top end lifted, and their lows below 120 Hz cut so the bass stays centred.",
        { cut (Shape::lowCut, 120.0, 12, Placement::side), shelf (Shape::highShelf, 6000.0, 2.0, 0.7, Placement::side) }));
    rigs.push_back (makeRig ("Mix Warmer", mix, 5, { "Mix", "Tilt", "Warm" },
        "Tilts the whole mix darker: 0.6 dB an octave, the lows up and the highs down round 1 kHz.", { tilt (-6.0) }));
    rigs.push_back (makeRig ("Mix Brighter", mix, 6, { "Mix", "Tilt", "Bright" },
        "Tilts the whole mix brighter: 0.6 dB an octave, the highs up and the lows down round 1 kHz.", { tilt (6.0) }));

    // E: mastering (a dB or two at most), each finished by the LIMITER (below)
    rigs.push_back (makeRig ("Master Gentle Polish", mastering, 1, { "Mastering" },
        "Small, broad moves: a touch of weight and air, a little less low-mid, and sub-sonic rumble (below 18 Hz) cut.",
        { cut (Shape::lowCut, 18.0, 24), shelf (Shape::lowShelf, 60.0, 0.8), bell (250.0, -0.8, 0.7),
          shelf (Shape::highShelf, 12000.0, 1.0) }));
    rigs.push_back (makeRig ("Master Low-End Control", mastering, 2, { "Mastering", "Dynamic", "Low end" },
        "Keeps the bass solid: the sides' lows (below 150 Hz) cut so the bass is centred, and boomy notes below 100 Hz "
        "come down a little when they bloom.",
        { cut (Shape::lowCut, 20.0, 24), cut (Shape::lowCut, 150.0, 12, Placement::side),
          dynamic (shelf (Shape::lowShelf, 100.0, 0.0), -3.0) }));
    rigs.push_back (makeRig ("Master De-Harsh", mastering, 3, { "Mastering", "Dynamic", "Harsh" },
        "Softens harsh moments in a finished mix (around 3.2 and 6.5 kHz), by at most 2.5 dB, only when they happen.",
        { dynamic (bell (3200.0, 0.0, 1.0), -2.5), dynamic (bell (6500.0, 0.0, 1.5), -2.5) }));
    rigs.push_back (makeRig ("Master Width", mastering, 4, { "Mastering", "Stereo", "Width" },
        "A little wider at the top (the sides above 8 kHz up 1.5 dB) with the bass kept in the middle.",
        { cut (Shape::lowCut, 120.0, 12, Placement::side), shelf (Shape::highShelf, 8000.0, 1.5, 0.7, Placement::side) }));
    rigs.push_back (makeRig ("Master Warm Tilt", mastering, 5, { "Mastering", "Tilt", "Warm" },
        "A gentle darker tilt for a bright master: 0.3 dB an octave.", { tilt (-3.0) }));
    rigs.push_back (makeRig ("Master Bright Tilt", mastering, 6, { "Mastering", "Tilt", "Bright" },
        "A gentle brighter tilt for a dull master: 0.3 dB an octave.", { tilt (3.0) }));

    // Genres (0.15.0): a rig for each part of a song in nine heavy styles,
    // our own reading of how each is usually mixed (not anyone's settings).
    // Starting points: tracks differ, and they're meant to be tweaked by ear.
    using S = Shape;
    addGenre (rigs, 0, "Modern Metal",
        { "A modern metal lead (screams or cleans): rumble and boxiness gone, presence and air up, sibilance caught as it happens.",
          { cut (S::lowCut, 100.0, 24), bell (250.0, -2.5, 1.0), bell (3500.0, 2.5, 0.9), shelf (S::highShelf, 11000.0, 2.0),
            dynamic (bell (6500.0, 0.0, 2.5), -6.0) } },
        { "A tight, punchy kit: kick weight at 60 Hz, the box at 400 Hz cut, beater click at 4.5 kHz.",
          { cut (S::lowCut, 30.0, 24), bell (60.0, 2.0, 1.0), bell (400.0, -3.0, 1.0), bell (4500.0, 2.5, 1.0), shelf (S::highShelf, 12000.0, 1.5) } },
        { "High-gain rhythm guitars: lows below 90 Hz left to the bass, palm-mute boom (180 Hz) turned down as it blooms, "
          "less mud, a little bite, the fizz above 10 kHz rolled off.",
          { cut (S::lowCut, 90.0, 24), dynamic (bell (180.0, 0.0, 1.2), -4.0), bell (450.0, -2.0, 1.0), bell (3000.0, 1.5, 1.0),
            cut (S::highCut, 10000.0, 18) } },
        { "Bass under heavy guitars: an even bottom, the mud cut, grind at 1.2 kHz and clank at 2.5 kHz so it's heard.",
          { cut (S::lowCut, 35.0, 24), dynamic (bell (70.0, 1.5, 1.4), -4.0), bell (250.0, -3.0, 1.2), bell (1200.0, 2.0, 1.0),
            bell (2500.0, 1.5, 1.2), cut (S::highCut, 7000.0, 12) } },
        { "A dense metal mix: low mids turned down when the riffs pile up, harshness at 3.5 kHz only when it's there, a little air.",
          { cut (S::lowCut, 25.0, 12), dynamic (bell (250.0, 0.0, 1.0), -3.0), dynamic (bell (3500.0, 0.0, 1.2), -3.0),
            shelf (S::highShelf, 10000.0, 1.5) } },
        { "Small moves for a loud, tight master: boomy notes below 100 Hz come down a little, a little less low-mid, a touch of air.",
          { cut (S::lowCut, 18.0, 24), dynamic (shelf (S::lowShelf, 100.0, 0.0), -2.0), bell (300.0, -0.8, 0.7),
            shelf (S::highShelf, 12000.0, 1.0) } });

    addGenre (rigs, 1, "Metalcore",
        { "For screams and cleans in one track: boom on the screams turned down only when it builds, presence and air up, "
          "sibilance caught.",
          { cut (S::lowCut, 110.0, 24), dynamic (bell (300.0, 0.0, 1.2), -4.0), bell (2800.0, 2.5, 0.9), shelf (S::highShelf, 10000.0, 2.5),
            dynamic (bell (7000.0, 0.0, 2.5), -6.0) } },
        { "A big, bright kit for breakdowns: kick weight at 55 Hz, less box, snap at 5 kHz and shimmer on top.",
          { cut (S::lowCut, 30.0, 24), bell (55.0, 2.5, 1.0), bell (450.0, -3.0, 1.0), bell (5000.0, 2.5, 0.9), shelf (S::highShelf, 11000.0, 2.0) } },
        { "Chugging rhythm guitars: the lows left to the bass, chug boom (200 Hz) turned down as it blooms, less mud, more bite.",
          { cut (S::lowCut, 100.0, 24), dynamic (bell (200.0, 0.0, 1.2), -4.0), bell (500.0, -2.5, 1.0), bell (2500.0, 2.0, 0.9),
            cut (S::highCut, 11000.0, 18) } },
        { "Bass for breakdowns: the sub held in check when it blooms, less mud, grind at 1 kHz and clank at 3 kHz.",
          { cut (S::lowCut, 30.0, 24), dynamic (shelf (S::lowShelf, 90.0, 0.0), -4.0), bell (300.0, -3.0, 1.2), bell (1000.0, 2.0, 1.0),
            bell (3000.0, 1.5, 1.2), cut (S::highCut, 7000.0, 12) } },
        { "A metalcore mix: a touch of low weight, low mids turned down when they build, a little presence and air.",
          { cut (S::lowCut, 25.0, 12), shelf (S::lowShelf, 70.0, 1.0), dynamic (bell (300.0, 0.0, 1.0), -3.0), bell (3000.0, 1.0, 0.8),
            shelf (S::highShelf, 11000.0, 1.5) } },
        { "A loud, bright master: a touch of weight and air, a little less low-mid.",
          { cut (S::lowCut, 18.0, 24), shelf (S::lowShelf, 60.0, 0.8), bell (350.0, -1.0, 0.7), shelf (S::highShelf, 12000.0, 1.2) } });

    addGenre (rigs, 2, "Djent",
        { "A clear, forward vocal over busy low guitars: tight low cut, less low-mid, bite at 4 kHz, sibilance caught.",
          { cut (S::lowCut, 100.0, 24), bell (400.0, -2.0, 1.0), bell (4000.0, 2.5, 1.2), shelf (S::highShelf, 12000.0, 2.0),
            dynamic (bell (6500.0, 0.0, 2.5), -6.0) } },
        { "A tight, clicky kit for fast, gridded parts: kick boom held down on double kicks, the box cut, sharp click at 6 kHz.",
          { cut (S::lowCut, 35.0, 24), dynamic (shelf (S::lowShelf, 80.0, 0.0), -3.0), bell (350.0, -3.5, 1.0), bell (6000.0, 3.0, 1.2),
            shelf (S::highShelf, 12000.0, 1.0) } },
        { "Extended-range guitars (7 or 8 strings): the low cut kept low (70 Hz) for the low strings, their boom (150 Hz) "
          "turned down on the chugs, less mud, the pick attack at 1.6 kHz and some bite.",
          { cut (S::lowCut, 70.0, 24), dynamic (bell (150.0, 0.0, 1.4), -5.0), bell (400.0, -2.5, 1.0), bell (1600.0, 1.5, 1.5),
            bell (3500.0, 1.5, 1.0), cut (S::highCut, 9500.0, 18) } },
        { "Clanky bass that reads over the guitars: an even bottom, less mud, clank at 2.8 kHz.",
          { cut (S::lowCut, 35.0, 24), dynamic (bell (70.0, 0.0, 1.4), -4.0), bell (400.0, -3.0, 1.0), bell (2800.0, 3.0, 1.2),
            cut (S::highCut, 8000.0, 12) } },
        { "A clinical djent mix: low boom from the chugs held down as it blooms, a little less low-mid, a little air.",
          { cut (S::lowCut, 28.0, 12), dynamic (shelf (S::lowShelf, 100.0, 0.0), -3.0), bell (400.0, -1.5, 0.8),
            shelf (S::highShelf, 10000.0, 1.5) } },
        { "A tight master: the sides' lows (below 150 Hz) cut so the low strings stay centred, low boom brought down a little "
          "when it blooms, a touch of air.",
          { cut (S::lowCut, 20.0, 24), cut (S::lowCut, 150.0, 12, Placement::side), dynamic (bell (120.0, 0.0, 1.0), -2.0),
            shelf (S::highShelf, 12000.0, 1.0) } });

    addGenre (rigs, 3, "Nu Metal",
        { "A rapped or shouted vocal: tight and forward, a mid bark at 1.5 kHz and bite at 4 kHz, sibilance caught.",
          { cut (S::lowCut, 100.0, 24), bell (220.0, -2.5, 1.0), bell (1500.0, 2.0, 1.0), bell (4000.0, 2.0, 1.0),
            dynamic (bell (6000.0, 0.0, 2.5), -6.0) } },
        { "A hip-hop weighted kit: kick boom at 50 Hz, snare body at 200 Hz, less box, a little crack.",
          { cut (S::lowCut, 28.0, 24), bell (50.0, 3.0, 1.0), bell (200.0, 1.5, 1.5), bell (450.0, -2.5, 1.0), bell (3500.0, 1.5, 1.0),
            shelf (S::highShelf, 10000.0, 1.0) } },
        { "Drop-tuned, scooped guitars: the lows left to the bass, chug boom turned down as it blooms, the middle scooped at 700 Hz.",
          { cut (S::lowCut, 80.0, 24), dynamic (bell (160.0, 0.0, 1.2), -4.0), bell (700.0, -3.5, 0.7), bell (3500.0, 1.5, 1.0),
            cut (S::highCut, 10000.0, 18) } },
        { "A fat bass: weight at 60 Hz, less mud, growl at 1.5 kHz and a little clank.",
          { cut (S::lowCut, 30.0, 24), bell (60.0, 2.5, 1.0), bell (250.0, -2.0, 1.0), bell (1500.0, 1.5, 1.0), bell (3000.0, 1.0, 1.2),
            cut (S::highCut, 8000.0, 12) } },
        { "A nu metal mix: a hip-hop low end, a little less middle, a little air.",
          { cut (S::lowCut, 25.0, 12), shelf (S::lowShelf, 70.0, 2.0), bell (700.0, -1.5, 0.7), shelf (S::highShelf, 10000.0, 1.5) } },
        { "A master with a big bottom: weight below 60 Hz up, a little less middle, a touch of air.",
          { cut (S::lowCut, 18.0, 24), shelf (S::lowShelf, 60.0, 1.5), bell (800.0, -0.8, 0.6), shelf (S::highShelf, 12000.0, 1.0) } });

    addGenre (rigs, 4, "Argent Metal",
        { "A dark, massive choir or chant to sit behind the guitars: weight at 180 Hz, softer at 2.5 kHz, a darker top, "
          "S's caught.",
          { cut (S::lowCut, 70.0, 18), bell (180.0, 2.0, 0.8), bell (2500.0, -2.0, 1.0), shelf (S::highShelf, 7000.0, -2.0),
            dynamic (bell (5500.0, 0.0, 2.5), -5.0) } },
        { "Mechanical, huge drums: sub weight at 45 Hz, the box cut, attack at 3 kHz.",
          { cut (S::lowCut, 25.0, 24), bell (45.0, 3.0, 0.9), bell (500.0, -3.0, 1.0), bell (3000.0, 2.0, 0.8), shelf (S::highShelf, 10000.0, 1.5) } },
        { "Saturated, very low-tuned guitars (8 or 9 strings): the low cut kept at 60 Hz, sub chugs (120 Hz) turned down as "
          "they bloom, less mud, a snarl at 2 kHz, fizz rolled off above 8.5 kHz so the synths have the top.",
          { cut (S::lowCut, 60.0, 24), dynamic (bell (120.0, 0.0, 1.4), -5.0), bell (350.0, -2.0, 1.0), bell (2000.0, 2.0, 0.8),
            cut (S::highCut, 8500.0, 18) } },
        { "A synth or distorted sub bass: sub weight at 45 Hz, boomy notes (90 Hz) held down, less mud, growl at 1 kHz.",
          { cut (S::lowCut, 25.0, 24), bell (45.0, 2.0, 0.9), dynamic (bell (90.0, 0.0, 1.2), -4.0), bell (300.0, -3.0, 1.0),
            bell (1000.0, 2.0, 0.9), cut (S::highCut, 6000.0, 12) } },
        { "A dense, heavy mix: sub weight up, low mids turned down when they pile up, harsh distortion at 5 kHz caught.",
          { cut (S::lowCut, 22.0, 12), shelf (S::lowShelf, 80.0, 2.5), dynamic (bell (250.0, 0.0, 1.0), -3.0), bell (2000.0, 1.0, 0.7),
            dynamic (bell (5000.0, 0.0, 1.2), -3.0) } },
        { "A heavy master: sub weight up a little, low mids and harshness brought down only when they build.",
          { cut (S::lowCut, 18.0, 24), shelf (S::lowShelf, 50.0, 1.5), dynamic (bell (250.0, 0.0, 1.0), -2.0),
            dynamic (bell (4500.0, 0.0, 1.0), -2.0) } });

    addGenre (rigs, 5, "Hard Rock",
        { "A big rock vocal: less low-mid, presence at 3 kHz, plenty of air, sibilance caught.",
          { cut (S::lowCut, 90.0, 18), bell (350.0, -2.0, 1.0), bell (3000.0, 2.5, 0.8), shelf (S::highShelf, 10000.0, 3.0),
            dynamic (bell (6500.0, 0.0, 2.5), -6.0) } },
        { "A natural, roomy kit: a little weight, a little less box, snap at 5 kHz and air on the cymbals.",
          { cut (S::lowCut, 30.0, 18), bell (70.0, 1.5, 1.0), bell (300.0, -1.5, 1.0), bell (5000.0, 2.0, 0.9), shelf (S::highShelf, 12000.0, 2.0) } },
        { "Crunchy, mid-forward guitars: the lows cut, a little less mud, body at 1 kHz, a little bite.",
          { cut (S::lowCut, 100.0, 18), bell (300.0, -1.5, 1.0), bell (1000.0, 2.0, 0.8), bell (3000.0, 1.0, 1.0), cut (S::highCut, 12000.0, 12) } },
        { "A rock bass: weight at 80 Hz, less mud, growl at 800 Hz.",
          { cut (S::lowCut, 35.0, 18), bell (80.0, 1.5, 1.0), bell (250.0, -2.0, 1.0), bell (800.0, 2.0, 0.9), cut (S::highCut, 6000.0, 12) } },
        { "A rock mix: a touch of weight, a forward middle, a little air.",
          { cut (S::lowCut, 25.0, 12), shelf (S::lowShelf, 80.0, 1.0), bell (1000.0, 0.8, 0.7), shelf (S::highShelf, 10000.0, 1.5) } },
        { "A warm rock master: a touch of weight, a forward middle, a touch of air.",
          { cut (S::lowCut, 18.0, 24), shelf (S::lowShelf, 70.0, 0.8), bell (1200.0, 0.6, 0.6), shelf (S::highShelf, 12000.0, 1.0) } });

    addGenre (rigs, 6, "Grunge",
        { "A raw, gritty vocal: throat at 500 Hz, edge at 2 kHz, a darker top, harsh strain at 4 kHz caught.",
          { cut (S::lowCut, 90.0, 18), bell (500.0, 1.5, 0.8), bell (2000.0, 2.0, 1.0), shelf (S::highShelf, 8000.0, -1.5),
            dynamic (bell (4000.0, 0.0, 2.0), -4.0) } },
        { "A trashy, roomy kit: a little weight, the room's honk at 800 Hz, darker cymbals.",
          { cut (S::lowCut, 30.0, 18), bell (100.0, 1.5, 1.0), bell (800.0, 1.0, 0.8), bell (4000.0, 1.0, 1.0), shelf (S::highShelf, 10000.0, -1.0) } },
        { "Fuzzy, mid-heavy guitars: the lows cut, warmth at 250 Hz, the middle up at 800 Hz, less top.",
          { cut (S::lowCut, 80.0, 18), bell (250.0, 1.0, 1.0), bell (800.0, 2.0, 0.8), bell (4000.0, -1.5, 1.0), cut (S::highCut, 9000.0, 12) } },
        { "A fuzzy bass: weight at 100 Hz, fuzz mids at 600 Hz, a dark top.",
          { cut (S::lowCut, 35.0, 18), bell (100.0, 1.5, 1.0), bell (600.0, 2.0, 0.8), cut (S::highCut, 5000.0, 12) } },
        { "A raw mix: a little more middle, a little less top.",
          { cut (S::lowCut, 25.0, 12), bell (800.0, 1.0, 0.7), shelf (S::highShelf, 9000.0, -1.0) } },
        { "A gentle darker tilt for a raw master: 0.2 dB an octave.",
          { cut (S::lowCut, 18.0, 24), tilt (-2.0) } });

    addGenre (rigs, 7, "Symphonic Metal",
        { "An operatic lead: less low-mid, piercing high notes (3 kHz) turned down only when they pierce, air on top.",
          { cut (S::lowCut, 120.0, 18), bell (400.0, -1.5, 1.0), dynamic (bell (3000.0, 0.0, 1.5), -5.0), shelf (S::highShelf, 12000.0, 3.0),
            dynamic (bell (7000.0, 0.0, 2.0), -4.0) } },
        { "A tight kit that leaves room for the orchestra: weight, less box, a dip at 2.5 kHz for the strings and voice, "
          "snap at 5 kHz.",
          { cut (S::lowCut, 35.0, 24), bell (60.0, 2.0, 1.0), bell (400.0, -2.5, 1.0), bell (2500.0, -1.5, 0.8), bell (5000.0, 2.0, 1.0),
            shelf (S::highShelf, 12000.0, 1.5) } },
        { "Guitars made narrower to fit the orchestra: the lows cut higher, less low-mid, a dip at 2.5 kHz for the voice, "
          "the fizz rolled off steeply.",
          { cut (S::lowCut, 110.0, 24), bell (300.0, -2.5, 1.0), bell (2500.0, -1.5, 1.2), bell (4000.0, 1.0, 1.0), cut (S::highCut, 9000.0, 24) } },
        { "A bass that doesn't fight the orchestra's low end: boomy notes held down, less mud, definition at 1.2 kHz.",
          { cut (S::lowCut, 35.0, 24), dynamic (bell (70.0, 0.0, 1.4), -4.0), bell (250.0, -2.5, 1.0), bell (1200.0, 2.0, 1.0),
            cut (S::highCut, 6000.0, 12) } },
        { "A wide, airy symphonic mix: low mids turned down when they build, air on top, the sides' top lifted for width "
          "with their lows cut.",
          { cut (S::lowCut, 25.0, 12), dynamic (bell (250.0, 0.0, 1.0), -3.0), shelf (S::highShelf, 12000.0, 2.0),
            cut (S::lowCut, 120.0, 12, Placement::side), shelf (S::highShelf, 6000.0, 1.5, 0.7, Placement::side) } },
        { "An open master: a little less low-mid, air on top, the sides' top lifted a little for width.",
          { cut (S::lowCut, 18.0, 24), bell (250.0, -0.8, 0.7), shelf (S::highShelf, 12000.0, 1.5),
            shelf (S::highShelf, 8000.0, 1.5, 0.7, Placement::side) } });

    addGenre (rigs, 8, "Industrial",
        { "A hard, distorted vocal: a narrow, mid-heavy band (bite at 1.2 kHz), the top rolled off, harshness caught.",
          { cut (S::lowCut, 150.0, 24), bell (1200.0, 3.0, 1.0), bell (3000.0, 1.5, 1.0), dynamic (bell (5000.0, 0.0, 2.0), -4.0),
            cut (S::highCut, 12000.0, 12) } },
        { "Machine-like drums: a hard kick at 55 Hz, less box, attack at 2.5 kHz, the very top cut.",
          { cut (S::lowCut, 30.0, 24), bell (55.0, 3.0, 1.2), bell (300.0, -3.0, 1.0), bell (2500.0, 2.5, 1.0), cut (S::highCut, 14000.0, 12) } },
        { "Buzzsaw guitars: the lows cut, less mud, the buzz at 1.5 kHz up, harsh moments at 3.5 kHz caught.",
          { cut (S::lowCut, 90.0, 24), bell (400.0, -2.0, 1.0), bell (1500.0, 2.5, 1.0), dynamic (bell (3500.0, 0.0, 1.5), -3.0),
            cut (S::highCut, 10000.0, 18) } },
        { "A distorted synth bass: weight at 55 Hz, less mud, bite at 1.5 kHz.",
          { cut (S::lowCut, 30.0, 24), bell (55.0, 2.0, 1.0), bell (300.0, -2.5, 1.0), bell (1500.0, 2.5, 1.0), cut (S::highCut, 9000.0, 12) } },
        { "A hard industrial mix: a little low weight, the upper middle forward, harshness caught, a little air.",
          { cut (S::lowCut, 28.0, 12), shelf (S::lowShelf, 70.0, 1.5), bell (1500.0, 1.0, 0.8), dynamic (bell (4000.0, 0.0, 1.2), -3.0),
            shelf (S::highShelf, 11000.0, 1.0) } },
        { "A hard master: a touch of weight, the upper middle a little forward, a touch of air.",
          { cut (S::lowCut, 18.0, 24), shelf (S::lowShelf, 60.0, 1.0), bell (2000.0, 0.8, 0.7), shelf (S::highShelf, 12000.0, 0.8) } });

    // Signature sounds (0.19.0, the owner's ask): each inspired by how a
    // particular record sounds, from what's published about its production
    // (tunings, how it was recorded, how reviewers hear it) and its measured
    // loudness. Named for the sound, never the artist or record. Our own
    // reading, not anyone's settings, and not tuned by ear: starting points.
    addSignature (rigs, "Ethereal Djent",
        { "Airy, ethereal cleans and harsh screams over very low guitars: boxiness out, a little presence, lots of air, "
          "sibilance caught.",
          { cut (S::lowCut, 100.0, 24), bell (300.0, -2.0, 1.0), bell (3000.0, 1.5, 0.9), shelf (S::highShelf, 10000.0, 3.0),
            dynamic (bell (7000.0, 0.0, 2.5), -6.0) } },
        { "A modern, tight kit under drop-F# guitars: kick weight at 50 Hz, the box cut, click and air.",
          { cut (S::lowCut, 28.0, 24), bell (50.0, 2.0, 1.0), bell (400.0, -3.0, 1.0), bell (5000.0, 2.0, 1.0), shelf (S::highShelf, 12000.0, 1.5) } },
        { "Seven-strings tuned very low (drop F#, about 46 Hz): the low cut lower than usual, the boom caught as it blooms, "
          "less mud, a little bite, the fizz rolled off.",
          { cut (S::lowCut, 60.0, 24), dynamic (bell (140.0, 0.0, 1.2), -4.0), bell (400.0, -2.0, 1.0), bell (2500.0, 1.5, 1.0),
            cut (S::highCut, 9000.0, 18) } },
        { "Bass under very low guitars: the lowest notes held even, the mud cut, grind at 1.2 kHz so it's heard.",
          { cut (S::lowCut, 30.0, 24), dynamic (bell (55.0, 0.0, 1.2), -4.0), bell (250.0, -3.0, 1.2), bell (1200.0, 2.0, 1.0),
            cut (S::highCut, 6000.0, 12) } },
        { "An open, airy mix (the record measures DR 11 on vinyl): low mids and harshness caught when they build, air on top.",
          { cut (S::lowCut, 22.0, 12), dynamic (bell (200.0, 0.0, 1.0), -3.0), dynamic (bell (3500.0, 0.0, 1.2), -3.0),
            shelf (S::highShelf, 12000.0, 2.0) } },
        { "A master with room to breathe: boomy notes kept in check, a little less low-mid, air.",
          { cut (S::lowCut, 18.0, 24), dynamic (shelf (S::lowShelf, 90.0, 0.0), -2.0), bell (300.0, -0.8, 0.7),
            shelf (S::highShelf, 12000.0, 1.5) } });

    addSignature (rigs, "Dark Synth Metal",
        { "Close, haunting vocals over synths: warm, breathy and present, sibilance caught.",
          { cut (S::lowCut, 90.0, 24), bell (250.0, -1.5, 1.0), bell (4000.0, 2.0, 0.9), shelf (S::highShelf, 11000.0, 3.0),
            dynamic (bell (6500.0, 0.0, 2.5), -6.0) } },
        { "Real drums that sit with electronics: a deep kick at 55 Hz, the box cut, snap at 4 kHz.",
          { cut (S::lowCut, 30.0, 24), bell (55.0, 2.5, 1.0), bell (350.0, -3.0, 1.0), bell (4000.0, 2.0, 1.0), shelf (S::highShelf, 11000.0, 1.0) } },
        { "Heavy drop-tuned guitars that sit under the synths: boom caught, mud cut, a darker top so the synths shine.",
          { cut (S::lowCut, 80.0, 24), dynamic (bell (170.0, 0.0, 1.2), -4.0), bell (500.0, -2.5, 1.0), bell (2000.0, 1.0, 1.0),
            cut (S::highCut, 8500.0, 18) } },
        { "Synth bass or bass guitar: the sub held even when it blooms, the mud cut, a little growl at 900 Hz.",
          { cut (S::lowCut, 28.0, 24), dynamic (bell (90.0, 0.0, 1.0), -3.0), bell (300.0, -3.0, 1.2), bell (900.0, 1.5, 1.0),
            cut (S::highCut, 5000.0, 12) } },
        { "A dense, very loud mix (the record measures DR 4): a touch of low weight, low mids and harshness caught as they build.",
          { cut (S::lowCut, 25.0, 12), shelf (S::lowShelf, 60.0, 1.0), dynamic (bell (250.0, 0.0, 1.0), -3.0), dynamic (bell (3000.0, 0.0, 1.2), -3.0),
            shelf (S::highShelf, 11000.0, 1.5) } },
        { "A loud, dark-polished master: a touch of weight, a little less low-mid, a touch of air.",
          { cut (S::lowCut, 18.0, 24), shelf (S::lowShelf, 60.0, 1.0), bell (300.0, -1.0, 0.7), shelf (S::highShelf, 12000.0, 1.0) } });

    addSignature (rigs, "Raw 90s Nu Metal",
        { "A raw, unpolished vocal, recorded live in the room: rumble out, presence up, little air.",
          { cut (S::lowCut, 90.0, 24), bell (400.0, -1.0, 1.0), bell (2500.0, 2.0, 1.0), shelf (S::highShelf, 9000.0, 1.0) } },
        { "A natural, roomy kit: kick body at 70 Hz, the box cut a little, crack at 5 kHz.",
          { cut (S::lowCut, 30.0, 24), bell (70.0, 2.0, 1.0), bell (400.0, -2.0, 1.0), bell (5000.0, 1.5, 1.0) } },
        { "Scooped seven-strings (A standard): the mids dipped wide, the boom caught, a little edge, a dark top.",
          { cut (S::lowCut, 70.0, 24), dynamic (bell (150.0, 0.0, 1.2), -3.0), bell (600.0, -3.0, 0.8), bell (3000.0, 1.0, 1.0),
            cut (S::highCut, 8000.0, 18) } },
        { "A percussive, clicky slap bass: some weight at 80 Hz, less mud, lots of click at 2.5 kHz and snap at 4 kHz.",
          { cut (S::lowCut, 35.0, 24), bell (80.0, 1.5, 1.0), bell (300.0, -3.0, 1.2), bell (2500.0, 3.0, 1.0), bell (4500.0, 1.5, 1.2),
            cut (S::highCut, 9000.0, 12) } },
        { "A raw, open mix (the record measures DR 8): low mids caught when they pile up, a little edge, not much air (it's 90s).",
          { cut (S::lowCut, 25.0, 12), dynamic (bell (300.0, 0.0, 1.0), -2.5), bell (3000.0, 1.0, 0.8), shelf (S::highShelf, 10000.0, 0.5) } },
        { "A light master that keeps the room and the dynamics: a little less low-mid, a whisper of air.",
          { cut (S::lowCut, 20.0, 24), bell (300.0, -0.5, 0.7), shelf (S::highShelf, 12000.0, 0.5) } });

    addSignature (rigs, "Clean Prog Djent",
        { "A clear, forward vocal over busy guitars: tight low cut, less low-mid, presence at 3.5 kHz, air, sibilance caught.",
          { cut (S::lowCut, 100.0, 24), bell (350.0, -2.0, 1.0), bell (3500.0, 2.0, 1.0), shelf (S::highShelf, 12000.0, 2.0),
            dynamic (bell (6500.0, 0.0, 2.5), -6.0) } },
        { "Live drums, tightly gated and EQ'd (no samples): kick weight at 60 Hz, the box cut hard, crack at 5 kHz, air.",
          { cut (S::lowCut, 30.0, 24), bell (60.0, 2.0, 1.0), bell (450.0, -3.5, 1.0), bell (5000.0, 2.5, 1.0), shelf (S::highShelf, 12000.0, 1.5) } },
        { "Tight, clinical modelled guitars: the lows left to the bass, chug boom caught, less mud, a little bite.",
          { cut (S::lowCut, 100.0, 24), dynamic (bell (200.0, 0.0, 1.2), -4.0), bell (500.0, -2.0, 1.0), bell (3000.0, 1.5, 1.0),
            cut (S::highCut, 10000.0, 18) } },
        { "A DI bass with saturation: an even bottom, the mud cut, grind at 1 kHz and clank at 2.5 kHz.",
          { cut (S::lowCut, 35.0, 24), dynamic (bell (70.0, 0.0, 1.4), -3.0), bell (250.0, -3.0, 1.2), bell (1000.0, 2.0, 1.0),
            bell (2500.0, 2.0, 1.2), cut (S::highCut, 7000.0, 12) } },
        { "A clinical mix (the record measures DR 6 on CD): low mids and upper-mid harshness caught as they build, air.",
          { cut (S::lowCut, 25.0, 12), dynamic (bell (250.0, 0.0, 1.0), -3.0), dynamic (bell (4000.0, 0.0, 1.2), -3.0),
            shelf (S::highShelf, 12000.0, 1.5) } },
        { "A tight master: a touch of weight, a little presence and air.",
          { cut (S::lowCut, 18.0, 24), shelf (S::lowShelf, 60.0, 1.0), bell (2000.0, 0.8, 0.7), shelf (S::highShelf, 12000.0, 0.8) } });

    addSignature (rigs, "Cinematic Post-Hardcore",
        { "Soaring cleans and screams: boxiness out, presence and lots of air, sibilance caught.",
          { cut (S::lowCut, 120.0, 24), bell (300.0, -2.0, 1.0), bell (3000.0, 2.5, 0.9), shelf (S::highShelf, 10000.0, 3.0),
            dynamic (bell (7000.0, 0.0, 2.5), -6.0) } },
        { "A big kit for anthems and breakdowns: deep kick, less box, snap and shimmer.",
          { cut (S::lowCut, 30.0, 24), bell (55.0, 2.5, 1.0), bell (450.0, -3.0, 1.0), bell (5000.0, 2.5, 0.9), shelf (S::highShelf, 11000.0, 2.0) } },
        { "Guitars that leave room for choirs, strings and piano: the lows left to the bass, the boom caught, less mud and a "
          "little less around 1 kHz, some bite.",
          { cut (S::lowCut, 100.0, 24), dynamic (bell (200.0, 0.0, 1.2), -4.0), bell (500.0, -2.5, 1.0), bell (1000.0, -1.0, 1.0),
            bell (2500.0, 1.5, 1.0), cut (S::highCut, 10000.0, 18) } },
        { "Bass for breakdowns under an orchestra: the sub held when it blooms, the mud cut, grind and clank.",
          { cut (S::lowCut, 30.0, 24), dynamic (shelf (S::lowShelf, 90.0, 0.0), -4.0), bell (300.0, -3.0, 1.2), bell (1000.0, 1.5, 1.0),
            bell (3000.0, 1.0, 1.2), cut (S::highCut, 7000.0, 12) } },
        { "A dense mix of band and orchestra: low mids and harshness caught when they build, a little less in the middle, air. "
          "(No loudness figure was found for this record: a guess.)",
          { cut (S::lowCut, 25.0, 12), dynamic (bell (300.0, 0.0, 1.0), -3.0), bell (1000.0, -1.0, 0.7), dynamic (bell (3000.0, 0.0, 1.2), -3.0),
            shelf (S::highShelf, 11000.0, 2.0) } },
        { "A big, bright master: a touch of weight and air, a little less low-mid.",
          { cut (S::lowCut, 18.0, 24), shelf (S::lowShelf, 60.0, 0.8), bell (350.0, -1.0, 0.7), shelf (S::highShelf, 12000.0, 1.2) } });

    addSignature (rigs, "Swedish Melodic Death",
        { "Half clean, half harsh: boxiness out, presence and some air, sibilance caught.",
          { cut (S::lowCut, 110.0, 24), bell (300.0, -2.0, 1.0), bell (2500.0, 2.0, 0.9), shelf (S::highShelf, 9000.0, 2.0),
            dynamic (bell (6500.0, 0.0, 2.5), -6.0) } },
        { "A tight, triggered-sounding kit: kick body at 70 Hz, the box cut, lots of beater click at 4.5 kHz.",
          { cut (S::lowCut, 30.0, 24), bell (70.0, 2.0, 1.0), bell (400.0, -3.0, 1.0), bell (4500.0, 3.0, 1.0), shelf (S::highShelf, 12000.0, 1.0) } },
        { "Mid-forward Swedish guitars (B standard): the boom caught, a little less mud, mids and bite kept, the fizz rolled off.",
          { cut (S::lowCut, 90.0, 24), dynamic (bell (180.0, 0.0, 1.2), -3.0), bell (400.0, -1.5, 1.0), bell (1200.0, 1.0, 1.0),
            bell (3000.0, 1.0, 1.0), cut (S::highCut, 9500.0, 18) } },
        { "Bass that's heard under busy guitars: a little weight, less mud, grind at 800 Hz.",
          { cut (S::lowCut, 35.0, 24), bell (80.0, 1.0, 1.0), bell (250.0, -3.0, 1.2), bell (800.0, 2.0, 1.0), cut (S::highCut, 5000.0, 12) } },
        { "A polished, loud mix (the record measures DR 5 on CD): low mids and harshness caught as they build, a little air.",
          { cut (S::lowCut, 25.0, 12), dynamic (bell (250.0, 0.0, 1.0), -3.0), dynamic (bell (3500.0, 0.0, 1.2), -2.5),
            shelf (S::highShelf, 10000.0, 1.0) } },
        { "A loud, polished master: a touch of weight and air, a little less low-mid.",
          { cut (S::lowCut, 18.0, 24), shelf (S::lowShelf, 60.0, 0.8), bell (350.0, -1.0, 0.7), shelf (S::highShelf, 12000.0, 1.0) } });

    addSignature (rigs, "Early 2000s Metalcore",
        { "Screams and big clean choruses: boxiness out, presence and air, sibilance caught.",
          { cut (S::lowCut, 110.0, 24), bell (300.0, -2.0, 1.0), bell (3000.0, 2.5, 0.9), shelf (S::highShelf, 10000.0, 2.0),
            dynamic (bell (7000.0, 0.0, 2.5), -6.0) } },
        { "A sampled-sounding kick and a sharp snare: kick weight at 60 Hz, the box cut, click at 4 kHz, some air.",
          { cut (S::lowCut, 30.0, 24), bell (60.0, 2.0, 1.0), bell (400.0, -3.0, 1.0), bell (4000.0, 2.5, 1.0), shelf (S::highShelf, 11000.0, 1.5) } },
        { "Razor-sharp guitars (drop C): the boom caught, less mud, edge at 2.8 kHz, the fizz rolled off.",
          { cut (S::lowCut, 90.0, 24), dynamic (bell (180.0, 0.0, 1.2), -3.0), bell (450.0, -2.0, 1.0), bell (2800.0, 2.0, 1.0),
            cut (S::highCut, 10500.0, 18) } },
        { "An audible bass, as on the record: weight at 70 Hz, less mud, grind at 1 kHz and edge at 2.5 kHz.",
          { cut (S::lowCut, 35.0, 24), bell (70.0, 1.5, 1.0), bell (280.0, -3.0, 1.2), bell (1000.0, 2.5, 1.0), bell (2500.0, 1.5, 1.2),
            cut (S::highCut, 7000.0, 12) } },
        { "A sharp, polished mix (the record measures about DR 7): low mids caught when they build, presence, a little air.",
          { cut (S::lowCut, 25.0, 12), dynamic (bell (280.0, 0.0, 1.0), -3.0), bell (3000.0, 1.0, 0.8), shelf (S::highShelf, 11000.0, 1.0) } },
        { "A punchy master: a touch of weight and air, a little less low-mid.",
          { cut (S::lowCut, 18.0, 24), shelf (S::lowShelf, 60.0, 0.8), bell (350.0, -1.0, 0.7), shelf (S::highShelf, 12000.0, 1.0) } });

    addSignature (rigs, "Polished Nu Metal",
        { "Layered raps and screams: rumble and boxiness out, presence and air, sibilance caught.",
          { cut (S::lowCut, 100.0, 24), bell (250.0, -2.0, 1.0), bell (3500.0, 2.0, 1.0), shelf (S::highShelf, 10000.0, 2.5),
            dynamic (bell (6500.0, 0.0, 2.5), -6.0) } },
        { "Hip-hop punch in a rock kit: kick weight at 60 Hz, the box cut, snare crack at 3.5 kHz, some air.",
          { cut (S::lowCut, 30.0, 24), bell (60.0, 2.5, 1.0), bell (350.0, -3.0, 1.0), bell (3500.0, 2.0, 1.0), shelf (S::highShelf, 10000.0, 1.5) } },
        { "Thick, multitracked drop-C# guitars: the boom caught, less mud, a little edge, a smooth top.",
          { cut (S::lowCut, 80.0, 24), dynamic (bell (200.0, 0.0, 1.2), -3.0), bell (600.0, -2.0, 1.0), bell (2000.0, 1.5, 1.0),
            cut (S::highCut, 9000.0, 18) } },
        { "A round bass under the guitars: weight at 70 Hz, less mud, a little growl at 800 Hz.",
          { cut (S::lowCut, 30.0, 24), bell (70.0, 2.0, 1.0), bell (250.0, -3.0, 1.2), bell (800.0, 1.5, 1.0), cut (S::highCut, 5000.0, 12) } },
        { "A very polished mix (the record measures DR 6 on CD): a touch of low weight, low mids caught, presence and air.",
          { cut (S::lowCut, 25.0, 12), shelf (S::lowShelf, 70.0, 1.0), dynamic (bell (300.0, 0.0, 1.0), -3.0), bell (3000.0, 1.0, 0.8),
            shelf (S::highShelf, 11000.0, 1.5) } },
        { "A loud, polished master: a touch of weight and air, a little less low-mid.",
          { cut (S::lowCut, 18.0, 24), shelf (S::lowShelf, 60.0, 1.0), bell (300.0, -1.0, 0.7), shelf (S::highShelf, 12000.0, 1.0) } });

    // More starting points (0.19.0, the owner's ask): one per source (x51
    // on), one-job fixes (x71 on) and mastering targets (E51 on). Each uses
    // CONSTRICTOR's, DOMINION's or the LIMITER's preset of the same purpose,
    // so a stomp's PRESET menu shows what the rig has.
    const auto add = [&rigs] (const juce::String& name, int bank, int number, const juce::StringArray& tags, const juce::String& notes,
                              const std::vector<BandSpec>& bands, const char* comp = nullptr, const char* dom = nullptr, const char* lim = nullptr)
    {
        auto rig = makeRig (name, bank, number, tags, notes, bands);
        if (comp != nullptr)
        {
            applyPreset (rig, CompPresets::factory(), comp, ParamIDs::compOn);
            rig.notes << " CONSTRICTOR: its \"" << comp << "\" preset.";
        }
        if (dom != nullptr)
        {
            applyPreset (rig, DominionPresets::factory(), dom, ParamIDs::domOn);
            rig.notes << " DOMINION: its \"" << dom << "\" preset.";
        }
        if (lim != nullptr)
        {
            applyPreset (rig, LimiterPresets::factory(), lim, ParamIDs::limitOn);
            rig.notes << " LIMITER: its \"" << lim << "\" preset.";
        }
        rigs.push_back (std::move (rig));
    };

    // Sources: vocals
    add ("Scream Vocal", vocals, 51, { "Vocal", "Scream" },
         "Screams: chest boom caught when it builds, the harsh edge tamed, presence kept.",
         { cut (S::lowCut, 120.0, 24), dynamic (bell (300.0, 0.0, 1.2), -4.0), bell (2500.0, 1.5, 1.0), dynamic (bell (4000.0, 0.0, 1.5), -4.0),
           shelf (S::highShelf, 10000.0, 1.5) },
         "Scream Control", "Vocal De-Harsh");
    add ("Breathy Pop Vocal", vocals, 52, { "Vocal", "Pop", "Lead" },
         "A close, breathy lead: warmth kept, presence and lots of air, sibilance caught.",
         { cut (S::lowCut, 90.0, 18), bell (250.0, -1.5, 1.0), bell (4000.0, 2.0, 0.9), shelf (S::highShelf, 11000.0, 3.0),
           dynamic (bell (6500.0, 0.0, 2.5), -6.0) },
         "Vocal Smooth & Bright", "Vocal Sibilance");
    add ("Choir & Group Vocals", vocals, 53, { "Vocal", "Choir", "Group" },
         "Many voices together: thinner at the bottom so they don't swamp the mix, smooth in the middle, airy on top.",
         { cut (S::lowCut, 150.0, 24), bell (400.0, -2.0, 0.8), bell (2500.0, -1.0, 1.0), shelf (S::highShelf, 10000.0, 2.0) },
         "Backing Vocals Tuck", "Backing Vocals Soft");
    add ("Podcast Voice", vocals, 54, { "Voice", "Podcast", "Voice-over" },
         "A spoken voice for podcasts: proximity boom caught, clarity at 3 kHz, sibilance caught.",
         { cut (S::lowCut, 80.0, 24), dynamic (bell (150.0, 0.0, 1.0), -3.0), bell (3000.0, 1.5, 1.0), dynamic (bell (6500.0, 0.0, 2.5), -5.0) },
         "Podcast Voice", "Podcast Voice", "Podcast (-16 LUFS)");

    // Sources: drums
    add ("Toms", drums, 51, { "Drums", "Toms" },
         "Toms: weight at 100 Hz, the boxy ring cut, attack at 4 kHz.",
         { cut (S::lowCut, 50.0, 18), bell (100.0, 2.0, 1.0), bell (500.0, -3.0, 1.2), bell (4000.0, 2.0, 1.0) },
         "Toms Punch", "Toms Ring");
    add ("Room Mics", drums, 52, { "Drums", "Room" },
         "Room mics: the low boom and boxiness out, crushed for size (blend it under the kit).",
         { cut (S::lowCut, 80.0, 18), bell (400.0, -3.0, 1.0), shelf (S::highShelf, 8000.0, 1.5) },
         "Room Crush", "Room Boxiness");
    add ("Snare Bottom", drums, 53, { "Drums", "Snare" },
         "The snare's bottom mic: no low end, the wires' sizzle brought out.",
         { cut (S::lowCut, 200.0, 24), bell (800.0, -2.0, 1.0), bell (6000.0, 2.0, 1.0) });
    add ("Electronic Drums", drums, 54, { "Drums", "Electronic" },
         "Drum machines and samples: sub weight at 50 Hz, less box, snap and air.",
         { cut (S::lowCut, 25.0, 24), bell (50.0, 2.0, 1.0), bell (300.0, -2.0, 1.0), bell (5000.0, 1.5, 1.0), shelf (S::highShelf, 12000.0, 1.5) },
         "Drum Bus Glue");

    // Sources: instruments
    add ("Rhythm Guitar High Gain", instruments, 51, { "Guitar", "Electric", "High gain" },
         "Distorted rhythm guitars: the lows left to the bass, chug boom caught, less mud, the fizz rolled off.",
         { cut (S::lowCut, 90.0, 24), dynamic (bell (180.0, 0.0, 1.2), -4.0), bell (450.0, -2.0, 1.0), bell (3000.0, 1.0, 1.0),
           cut (S::highCut, 10000.0, 18) },
         "Rhythm Guitar Tight", "Rhythm Guitar Whistle");
    add ("Lead Guitar", instruments, 52, { "Guitar", "Electric", "Lead" },
         "Solos: thinner at the bottom, a little more voice at 1.5 kHz, smooth on top, sustain evened out.",
         { cut (S::lowCut, 120.0, 24), bell (1500.0, 2.0, 1.0), cut (S::highCut, 9000.0, 12) },
         "Lead Guitar Sustain", "Electric Guitar Fizz");
    add ("Clean Guitar", instruments, 53, { "Guitar", "Electric", "Clean" },
         "Clean electric guitar: low rumble out, a little less boom, sparkle on top.",
         { cut (S::lowCut, 80.0, 18), bell (250.0, -1.5, 1.0), shelf (S::highShelf, 8000.0, 2.0) },
         "Acoustic Even");
    add ("Distorted Bass", instruments, 54, { "Bass", "Distorted" },
         "Bass with grit: an even bottom, the mud cut, the growl at 1.2 kHz, the fizz above 6 kHz rolled off.",
         { cut (S::lowCut, 35.0, 24), dynamic (bell (70.0, 0.0, 1.4), -3.0), bell (300.0, -3.0, 1.2), bell (1200.0, 2.0, 1.0),
           cut (S::highCut, 6000.0, 12) },
         "Bass Even", "Bass Smoother");
    add ("Synth Lead", instruments, 55, { "Keys", "Synth", "Lead" },
         "A synth lead: the lows out of the way, presence, its harsh resonances smoothed.",
         { cut (S::lowCut, 150.0, 24), bell (2500.0, 1.5, 1.0) },
         nullptr, "Synth Lead Harsh");
    add ("Strings & Orchestra", instruments, 56, { "Keys", "Strings", "Orchestra" },
         "Orchestral strings and samples: rumble out, less low-mid build-up, air.",
         { cut (S::lowCut, 60.0, 18), bell (300.0, -2.0, 0.8), shelf (S::highShelf, 10000.0, 2.0) },
         "Synth Pad Glue");
    add ("Organ & Electric Piano", instruments, 57, { "Keys", "Organ", "Electric piano" },
         "Organs and electric pianos: rumble and mud out, a little bark at 1 kHz.",
         { cut (S::lowCut, 70.0, 18), bell (250.0, -2.0, 1.0), bell (1000.0, 1.5, 1.0), cut (S::highCut, 12000.0, 12) },
         "Piano & Keys Even");

    // Fixes: one job each
    add ("Fix: Thin Vocal", vocals, 71, { "Vocal", "Fix", "Thin" },
         "A thin vocal: body brought up at 200 Hz, and lifted when it's quiet.",
         { cut (S::lowCut, 70.0, 18), bell (200.0, 2.5, 0.9) }, "Thin Vocal Body");
    add ("Fix: Nasal Vocal", vocals, 72, { "Vocal", "Fix", "Nasal" },
         "A nasal or honky vocal: the honk round 1 kHz turned down when it sticks out.",
         { dynamic (bell (1000.0, -1.0, 1.4), -4.0) }, nullptr, "Nasal Honk");
    add ("Fix: Harsh Vocal", vocals, 73, { "Vocal", "Fix", "Harsh" },
         "A harsh, piercing vocal: the upper mids turned down only when they bite.",
         { dynamic (bell (3500.0, 0.0, 1.2), -5.0) }, "Harsh Upper Mids", "Vocal De-Harsh");
    add ("Fix: Boxy Snare", drums, 71, { "Drums", "Snare", "Fix" },
         "A boxy snare: the cardboard round 450 Hz cut, and turned down further on the hits.",
         { bell (450.0, -2.5, 1.4), dynamic (bell (450.0, 0.0, 1.4), -3.0) }, "Boxy Snare", "Snare Ring");
    add ("Fix: Ringing Toms", drums, 72, { "Drums", "Toms", "Fix" },
         "Toms that ring on: the ring smoothed as it sustains.",
         { cut (S::lowCut, 50.0, 18), bell (300.0, -1.5, 1.0) }, nullptr, "Toms Ring");
    add ("Fix: Cymbal Wash", drums, 73, { "Drums", "Cymbals", "Fix" },
         "Washy, harsh cymbals: the highs turned down when the crashes pile up.",
         { dynamic (shelf (S::highShelf, 6000.0, 0.0), -4.0) }, "Hi-Hat Tame", "Cymbal Wash");
    add ("Fix: Honky Guitar", instruments, 71, { "Guitar", "Fix", "Honk" },
         "A honky guitar: the honk round 800 Hz turned down when it sticks out.",
         { dynamic (bell (800.0, -1.0, 1.4), -4.0) }, "Honk Control", "Nasal Honk");
    add ("Fix: Guitar Fizz", instruments, 72, { "Guitar", "Fix", "Fizz" },
         "Fizzy distorted guitars: the fizz above 6 kHz smoothed and rolled off.",
         { cut (S::highCut, 11000.0, 12), dynamic (shelf (S::highShelf, 6000.0, 0.0), -4.0) }, "Fizz Control", "Fizz");
    add ("Fix: Muddy Bass", instruments, 73, { "Bass", "Fix", "Mud" },
         "A muddy bass: 250 Hz cut, and turned down further when it piles up.",
         { cut (S::lowCut, 30.0, 24), bell (250.0, -2.0, 1.0), dynamic (bell (250.0, 0.0, 1.2), -3.0) }, "De-Mud", "De-Mud");
    add ("Fix: Boomy Low End", mix, 71, { "Mix", "Fix", "Boom" },
         "A mix whose low end booms on some notes: those notes turned down as they happen, the rest left alone.",
         { cut (S::lowCut, 25.0, 12), dynamic (shelf (S::lowShelf, 120.0, 0.0), -3.0) }, "Boom Control", "Boomy Room");
    add ("Fix: Honky Midrange", mix, 72, { "Mix", "Fix", "Honk" },
         "A mix that honks round 1 kHz: turned down only when it sticks out.",
         { dynamic (bell (1000.0, 0.0, 1.0), -3.0) }, "Honk Control", "Nasal Honk");
    add ("Fix: Sibilant Mix", mix, 73, { "Mix", "Fix", "Sibilance" },
         "Sharp S sounds over the whole mix: 5 to 9 kHz turned down on each one.",
         { dynamic (bell (7000.0, 0.0, 1.5), -4.0, 0.25, 0.4) }, "De-Ess");

    // Mastering targets: the LIMITER's preset for each (it sets the loudness TARGET on its page: turn GAIN until INTEGRATED reaches it).
    const std::vector<BandSpec> polish { cut (S::lowCut, 18.0, 24), bell (300.0, -0.5, 0.7), shelf (S::highShelf, 12000.0, 1.0) };
    add ("Master for Streaming", mastering, 51, { "Mastering", "Streaming", "Loudness" },
         "For Spotify, Apple Music, YouTube and the like: they all play at about -14 LUFS, so louder buys nothing there. "
         "Gentle polish, -1 dBTP ceiling.", polish, nullptr, nullptr, "Streaming (-14 LUFS)");
    add ("Master Loud Club", mastering, 52, { "Mastering", "Club", "Loudness" },
         "As loud as club and DJ tracks (about -9 LUFS): AGGRESSIVE, oversampled, a -0.3 dBTP ceiling. Costs some punch.",
         { cut (S::lowCut, 25.0, 24), shelf (S::lowShelf, 60.0, 1.0), bell (300.0, -1.0, 0.7), shelf (S::highShelf, 12000.0, 1.0) },
         nullptr, nullptr, "Loud Club (-9 LUFS)");
    add ("Master CD", mastering, 53, { "Mastering", "CD" },
         "A CD master: loud, a -0.3 dBTP ceiling, dithered to 16 bits last (export at 16 bits).", polish, nullptr, nullptr, "CD Master (16-bit)");
    add ("Master Hi-Res", mastering, 54, { "Mastering", "Hi-res" },
         "For 24-bit downloads: CLEAR, oversampled, dithered to 24 bits.", polish, nullptr, nullptr, "Hi-Res (24-bit)");
    add ("Master Gentle & Dynamic", mastering, 55, { "Mastering", "Dynamic" },
         "Keeps the life in it: SAFE, a longer look-ahead and release, little gain.", polish, nullptr, nullptr, "Gentle & Dynamic");
    add ("Master for Vinyl", mastering, 56, { "Mastering", "Vinyl" },
         "Vinyl-friendly: no rumble, the very top tamed when it gets sharp, a -3 dBTP ceiling and DC filter, gently limited.",
         { cut (S::lowCut, 25.0, 24), dynamic (shelf (S::highShelf, 8000.0, 0.0), -2.0) }, nullptr, nullptr, "Vinyl Safe");
    add ("Master Podcast", mastering, 57, { "Mastering", "Podcast", "Voice" },
         "A finished podcast or voice-over at -16 LUFS: rumble out, clarity, -1.5 dBTP (for the voice itself, Podcast Voice at A54).",
         { cut (S::lowCut, 30.0, 24), bell (3000.0, 1.0, 1.0) }, nullptr, nullptr, "Podcast (-16 LUFS)");
    add ("Master Broadcast R128", mastering, 58, { "Mastering", "Broadcast" },
         "For TV and radio in Europe (EBU R128): -23 LUFS, -1 dBTP. Usually means turning it down (TRIM or the mix), not up.",
         { cut (S::lowCut, 20.0, 24) }, nullptr, nullptr, "Broadcast EBU R128 (-23 LUFS)");
    add ("Master Broadcast A/85", mastering, 59, { "Mastering", "Broadcast" },
         "For US TV (ATSC A/85): -24 LUFS, -2 dBTP.", { cut (S::lowCut, 20.0, 24) }, nullptr, nullptr, "Broadcast ATSC A/85 (-24 LUFS)");

    // COMP (0.9.0) on the rigs that would usually have one: gentle settings,
    // thresholds for recordings peaking around -10 dBFS (a guess: louder or
    // quieter tracks want the THRESHOLD moved).
    struct CompSpec { const char* rig; int character; float threshold, ratio, attack, release; bool autoRelease; const char* note; };
    const CompSpec comps[] = {
        { "Lead Vocal",     1, -20.0f, 3.0f, 10.0f, 300.0f, false, " COMP (OPTO) evens out the level smoothly." },
        { "Rap Vocal",      0, -18.0f, 4.0f,  5.0f, 150.0f, false, " COMP (VCA) keeps the delivery tight and upfront." },
        { "Drum Bus",       0, -20.0f, 4.0f, 30.0f, 300.0f, true,  " COMP (VCA, slow attack so hits keep their punch, AUTO release) glues the kit." },
        { "Mix Bus Polish", 0, -16.0f, 1.5f, 30.0f, 300.0f, true,  " COMP (VCA, 1.5:1) adds a light glue." },
    };
    for (const auto& c : comps)
        for (auto& rig : rigs)
            if (rig.name == c.rig)
            {
                rig.parameters[ParamIDs::compOn] = 1.0f;
                rig.parameters[ParamIDs::compCharacter] = (float) c.character;
                rig.parameters[ParamIDs::compThreshold] = c.threshold;
                rig.parameters[ParamIDs::compRatio] = c.ratio;
                rig.parameters[ParamIDs::compAttack] = c.attack;
                rig.parameters[ParamIDs::compRelease] = c.release;
                rig.parameters[ParamIDs::compAuto] = c.autoRelease ? 1.0f : 0.0f;
                rig.notes << c.note;
            }

    // DOMINION (0.10.0) where harshness and ringing are common: SOFT, gentle
    // depths, and a low cut on its depth curve so the low end is left alone
    // (and, on the mix, a lift round 3 kHz, where harshness lives).
    struct DominionSpec { const char* rig; float depth, detail, lowCutHz, bellHz, bellDb; const char* note; };
    const DominionSpec dominions[] = {
        { "Lead Vocal",            4.0f, 60.0f, 150.0f, 0.0f,    0.0f, " DOMINION smooths harsh, ringing resonances as they happen." },
        { "Overheads",             5.0f, 70.0f, 400.0f, 0.0f,    0.0f, " DOMINION tames ringing cymbals and room resonances." },
        { "Acoustic Guitar",       4.0f, 50.0f, 120.0f, 0.0f,    0.0f, " DOMINION smooths boomy and harsh spots in the body and strings." },
        { "Mix Harshness Control", 2.5f, 40.0f, 200.0f, 3000.0f, 6.0f, " DOMINION gently smooths resonances, most of all round 3 kHz." },
    };
    for (const auto& d : dominions)
        for (auto& rig : rigs)
            if (rig.name == d.rig)
            {
                rig.parameters[ParamIDs::domOn] = 1.0f;
                rig.parameters[ParamIDs::domDepth] = d.depth;
                rig.parameters[ParamIDs::domDetail] = d.detail;
                const auto band = [&rig] (int index, dominion::Shape shape, float hz, float depthDb)
                {
                    rig.parameters[ParamIDs::domBand (index, "Used")] = 1.0f;
                    rig.parameters[ParamIDs::domBand (index, "Shape")] = (float) shape;
                    rig.parameters[ParamIDs::domBand (index, "Freq")] = hz;
                    rig.parameters[ParamIDs::domBand (index, "Depth")] = depthDb;
                };
                band (0, dominion::Shape::lowCut, d.lowCutHz, 0.0f);
                if (d.bellHz > 0.0f)
                    band (1, dominion::Shape::bell, d.bellHz, d.bellDb);
                rig.notes << d.note;
            }

    // The mastering rigs end with the LIMITER (0.8.0; from 0.16.0 its MODERN
    // style with TRUE PEAK): a -1 dBTP ceiling, the most streaming services
    // allow, as a safety net against overs rather than to make it louder.
    for (auto& rig : rigs)
        if (rig.bank == mastering && rig.parameters.count (ParamIDs::limitOn) == 0)   // (those with a LIMITER preset of their own keep it)
        {
            rig.parameters[ParamIDs::limitOn] = 1.0f;
            rig.parameters[ParamIDs::limitCeiling] = -1.0f;
            rig.parameters[ParamIDs::limitMode] = (float) effects::limiter::Style::modern;
            rig.parameters[ParamIDs::limitTruePeak] = 1.0f;
            rig.notes << " The LIMITER (stomp 7) holds a -1 dBTP ceiling (true peak).";
        }

    return rigs;
}
