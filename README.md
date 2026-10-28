# Paragon PR7-X

*(Called PR7-E until 0.20.0: the same plugin, renamed. Your projects, rigs and presets carry over.)*

[![Build](https://github.com/ARYEZ/Paragon-PR7-E/actions/workflows/build.yml/badge.svg)](https://github.com/ARYEZ/Paragon-PR7-E/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/ARYEZ/Paragon-PR7-E)](https://github.com/ARYEZ/Paragon-PR7-E/releases/latest)

The utility processor of the Paragon PR7 family: a VST3 plugin drawn as a floor unit, laid out like a DigiTech GSP21's foot controller.
Built with [JUCE](https://juce.com).

Paragon PR7-X (PR7-E until 0.20.0) starts from [Paragon PR7-B](https://github.com/ARYEZ/Paragon-PR7-B) 0.11.0, the bass processor. Its
siblings are [PR7-G](https://github.com/ARYEZ/Paragon-PR7-G) (guitar), PR7-B (bass),
[PR7-V](https://github.com/ARYEZ/Paragon-PR7-V) (vocal) and [PR7-C](https://github.com/ARYEZ/Paragon-PR7-C). It installs
alongside all of them and keeps its own rigs and settings.

![The Paragon design mockup (from when the guitar version was called Paragon X1)](Paragon%20x1.png)

*The design mockup the family was built from.*

## Status

PR7-X is at the start of its own life: it has its own name and identity, and its face of four rack units, but some of
the stomps are still being planned, one at a time with the owner.

| Stomp | Plan | Now |
|---|---|---|
| **1 Oracle** | Melody to MIDI (inspired by Melodyne and VariAudio, our own design), with the tuner | Works: transcribes as the DAW plays, note editing, drag the MIDI onto a track |
| **2 Comp** | PR7-C's compressor, plus up to six bands inspired by FabFilter Pro-MB (our own design) | Works: full band, and bands that compress or expand, down or up, with look-ahead, on its own page |
| **3 Inserts** | Host other plugins in slots inside PR7-X and open their editors (a mini rack) | Placeholder |
| **4 EQ** | An EQ inspired by FabFilter Pro-Q 4 (our own design) | Works: 24 bands on its own page, any of them dynamic, and EQ Match |
| **5 Dominion** | Resonance suppression inspired by oeksound soothe 3 (our own design) | Works: Soft and Hard, the depth curve, stereo/mid-side, tilts, delta and band listen, on its own page |
| **6 Auto EQ** | Adaptive EQ inspired by Soundtheory Gullfoss (a "maybe"; our own design) | Placeholder |
| **7 Limiter** | Inspired by FabFilter Pro-L 2 (our own design; PR7-C's limiter before 0.16.0) | Works: eight styles, look-ahead, true peak, oversampling, loudness meters, dither |

Since 0.18.0 the unit has a rack unit for each built stomp in the chain (ECHELON, DOMINION, CONSTRICTOR, GUILLOTINE)
and ORACLE's key; the placeholders will get their place when they're built. Final names are the owner's; none of those
products' names or branding go on the plugin.

## Using it

- **The unit:** the screen centred along the top (the arrows beside it step through rigs; **SAVE** / **SAVE AS** under it),
  then four rack units, one to a row in the chain's order (styled after a rack of classic studio processors, in the PR7's
  own parts): **ECHELON, DOMINION, CONSTRICTOR, GUILLOTINE.** Each has a **POWER** footswitch, a ladder of LED bars that
  meters what it's doing, a screen with its readout, three keys, two knobs, and **EDIT** (or click its name) for its page,
  over a small screen with its preset or style. **PRESETS** browses every rig.
- **Small keys:** UNDO, REDO, BYPASS, and **ORACLE** (opens its page; lit while it listens).
- **ECHELON (row 1, the EQ)**, inspired by FabFilter Pro-Q 4: up to 24 bands (bell, shelves, cuts, notch, band pass, tilt shelf,
  flat tilt, all pass; 6 to 96 dB/oct), each on the stereo signal or only its left, right, mid or side. Analog-matched
  curves with zero latency. On its page: double-click to add a band, drag its dot, mouse wheel for Q (or a cut's slope),
  Alt-click to switch it off, right-click for its menu, Delete to remove it; hold SOLO to hear only what a band works on.
  OUTPUT, GAIN SCALE and AUTO GAIN. The spectrum going in and coming out shows behind the curve.
  - **Dynamic bands:** a bell, shelf or tilt with a **DYN RANGE** moves its gain when its own part of the sound gets
    loud: a negative range cuts (tame a harsh note, sibilance, a boomy bass note), a positive one boosts. Alt+double-click
    makes one; drag its range handle, or Alt + mouse wheel. **AUTO** (on by default) sets the threshold from the band's
    usual level; switch it off to set **THRESHOLD** yourself. **ATTACK** and **RELEASE** at 50% are automatic (from the
    band's frequency); lower is faster, higher slower. The curve moves as the band does.
  - **EQ Match** (MATCH, top of the page): listens to your track and a reference (another part of the song, a song
    file, or a saved reference), then proposes bands that make your track's tone like the reference's. DETAIL (a few
    broad bands or more, finer ones) and AMOUNT (how much of the difference), then ADD BANDS.
- **DOMINION (row 2)**, inspired by oeksound soothe 3: finds what rings, whistles or builds up as the track plays and
  turns just that down. MODE (Soft: what sticks out, at any level; Hard: what's loud), DEPTH, DETAIL, ATTACK, RELEASE; a
  graph of what it's cutting, with a depth curve of up to 8 bands to steer it; stereo or mid/side, LINK and FOCUS; tilts
  for the lows and highs; MAX CUT, WET TRIM, DELTA, MIX and OUT. Click its name for its page. Zero latency.
  **PRESET** (on its page): its own presets, apart from the rigs (vocals, drums, instruments, mix and master, creative,
  and under GENRES the vocal, guitars and mix of each genre the genre rigs use), and your own, saved in "Dominion Presets" beside your rigs. A preset changes only DOMINION's settings.
- **CONSTRICTOR (row 3, the compressor)**, after DOMINION; EDIT opens its page.
  - **Bands** (up to six, inspired by FabFilter Pro-MB, our own design): each works on its own part of the spectrum.
    Double-click the graph to add one; drag its dot sideways to move it and up or down for RANGE, drag its edges.
    MODE and RANGE give four kinds: COMPRESS turns loud parts down (RANGE below 0) or brings quiet parts up (above 0);
    EXPAND turns quiet parts down, like a gate (below 0), or brings loud parts up for punch (above 0). THRESHOLD, RATIO,
    KNEE, ATTACK, RELEASE, CHARACTER (VCA, OPTO, FET), STEREO (both with LINK, mid or side), OUTPUT, SLOPE, LISTEN.
    A band that isn't working leaves the sound exactly as it was. **LOOK-AHEAD** lets each band hear up to 20 ms ahead
    (COMP is then 20 ms late, which your DAW makes up for on playback); **MIX** 0 to 200%.
  - **Full band**, PR7-C's compressor, after the bands (switch it with FULL BAND): CHARACTER (VCA clean, OPTO smooth,
    FET fast with a little grit), THRESHOLD, RATIO (from Soft to infinite), ATTACK, RELEASE, AUTO release, MAKE UP and
    LINK, with its gain-reduction meter.
  - **PRESET** (on its page): COMP's own presets, apart from the rigs (vocals, drums, instruments, mix and master,
    creative, and under GENRES the vocal, drums, bass, mix bus and master of each genre the genre rigs use), and your own, saved in "Comp Presets" beside your rigs. A preset changes only COMP's settings.
- **GUILLOTINE (row 4, the limiter)**, inspired by FabFilter Pro-L 2 (our own design from its manual), last in the chain, after
  OUTPUT, so nothing can push a peak past it. Click its name for its page.
  - **PRESET** (0.19.0), as CONSTRICTOR's and DOMINION's: mastering targets (each also sets the page's loudness
    TARGET: turn GAIN until INTEGRATED reaches it), mix bus, tracks, and the signature sounds' masters; and your own,
    saved in "Limiter Presets" beside your rigs.
  - **GAIN** drives the sound into the **OUTPUT** ceiling. A fast stage looks ahead (**LOOKAHEAD**, 0 to 5 ms) and eases
    into each peak so nothing passes the ceiling; a slower stage (**ATTACK**, **RELEASE**) follows the average level.
    **LINK PEAKS** and **LINK RELEASE**: how much the two sides share each stage.
  - **STYLE**: CLEAR, PUNCHY, DYNAMIC, ALL-ROUND, AGGRESSIVE, MODERN (the default), GLUE, SAFE; and BRICK, SOFT and AUTO,
    the earlier limiter (PR7-C's), for older projects.
  - **TRUE PEAK** keeps the peaks a converter rebuilds between the samples under the ceiling too (set OUTPUT to -1 dBTP
    for streaming); **OVERSAMPLING** 2x to 8x; **UNITY** (hear it at the input's level), **DELTA** (hear only what it
    takes away), **DC FILTER**, **TRIM**, and **DITHER** with **NOISE SHAPING**, last of all.
  - The page shows the level going in and out, the gain reduction (the biggest dips labelled) and the loudness over the
    last few seconds; the true peak and reduction held since RESET; and loudness (momentary, short term, integrated,
    range, as EBU R128) against a TARGET (-14 LUFS for streaming).
  - Look-ahead, true peak and oversampling delay the sound a little (shown on the page); your DAW makes up for it.
- **ORACLE (the small key beside BYPASS)**, melody to MIDI, inspired by what Melodyne and VariAudio do with a single line (our own design):
  switch it on and play the part in your DAW. It listens as the DAW plays (one voice or instrument at a time, up to
  1,500 Hz) and finds the notes: where each starts and ends, its pitch (the middle of what was sung, not the scoops or
  the vibrato) and its velocity. Playing a passage again replaces what it heard there. Click its name for its page: the
  notes as a piano roll over the pitch line. Drag a note up or down (pitch), drag its ends (start, length), Alt-drag to
  move it, double-click to split (or to add a note), J to join, Delete; ORACLE has its own UNDO and REDO.
  **SENSITIVITY** (how readily it splits notes), **MIN NOTE** (the shortest note kept), **QUANTIZE** (for the MIDI
  file only). **DRAG MIDI**: drag it onto a MIDI track at bar 1 (the file starts at bar 1, at the song's tempo);
  click it to show the file instead. The transcription is saved with your DAW project. The tuner is part of it: the
  pedal's screen shows the note and cents, and the page shows the note now and how in tune the notes were sung. The
  reference pitch for A4 is in SETTINGS.
- **Stereo throughout:** both channels pass through as they came in, apart from what the stomps do (a mono track plays on
  both speakers). **Signal chain** (bottom strip): input → ECHELON → DOMINION → CONSTRICTOR → GUILLOTINE → output, growing as the stomps are built.
- **PRESENCE** cuts or boosts the top end (above about 3.5 kHz).
- **Rigs:** SAVE in the header saves the current sound as a rig; **<** and **>** step through rigs; a `*` after the name
  means it's changed since loading. The **PRESETS** tab browses, files (banks A-G), tags and favourites rigs. The banks
  (0.21.0): **A** drums, **B** bass, **C** guitars, **D** synths and keys, **E** vocals and voice (with **Init**,
  everything off, at E01), **F** the full mix, **G** mastering. INPUT and OUTPUT stay put when you switch rigs; each rig
  has its own **RIG LEVEL**, which adds to OUTPUT.
  - **Genre rigs:** a rig for each part of a song in nine heavy styles (modern metal, metalcore, djent, nu metal,
    argent metal, hard rock, grunge, symphonic metal, industrial), at the genre's number in every bank: **x11** modern
    metal to **x19** industrial, so A11 is the modern metal drums, B11 its bass, C11 its guitars, E11 its vocal, F11
    its mix and G11 its master. Each uses the EQ and the genre's COMP and DOMINION presets.
  - **Signature sounds** (0.19.0): eight sets of six rigs in the same way at **x31** to **x38**, each
    inspired by how a particular record sounds, from what's published about its production and its measured loudness,
    and named for the sound: Ethereal Djent, Dark Synth Metal, Raw 90s Nu Metal, Clean Prog Djent, Cinematic
    Post-Hardcore, Swedish Melodic Death, Early 2000s Metalcore, Polished Nu Metal. Each uses the EQ and presets of the
    same name in CONSTRICTOR, DOMINION and GUILLOTINE. Our own reading, not anyone's settings.
  - **More starting points** (0.19.0): one per source at **x51** on (screams, breathy pop vocal, choir, podcast voice;
    toms, room mics, snare bottom, electronic drums; high-gain rhythm, lead and clean guitar, distorted bass, synth lead,
    strings, organ and electric piano), one-job fixes at **x71** on (thin, nasal or harsh vocal; boxy snare, ringing
    toms, cymbal wash; honky guitar, guitar fizz, muddy bass; boomy, honky or sibilant mix) and mastering targets at
    **G51** on (streaming, loud club, CD, hi-res, gentle, vinyl, podcast, broadcast R128 and A/85).
- **Undo / redo:** the header arrows, UNDO / REDO on the unit, or Ctrl+Z / Ctrl+Shift+Z (Cmd on a Mac). **BYPASS** on
  the unit bypasses the whole plugin.
- **SETTINGS:** the tuner's reference pitch and the unit's finish.
- Delay: none, unless COMP's LOOK-AHEAD or GUILLOTINE's look-ahead, true peak or oversampling are on; your DAW makes
  up for it on playback.

## Download

Get the latest version from the **[Releases](https://github.com/ARYEZ/Paragon-PR7-E/releases/latest)** page: download
the zip for your computer. (Builds of every push also appear under **Actions** → a run → **Artifacts**.)

Unzip it and copy `Paragon PR7-X.vst3` to your VST3 folder:

| OS | VST3 folder |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3` |
| macOS | `~/Library/Audio/Plug-Ins/VST3` |
| Linux | `~/.vst3` |

**macOS:** the build isn't code-signed yet, so macOS will block it. After copying, run
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Paragon PR7-X.vst3"` once.

## Making a release

1. Set the new version in `CMakeLists.txt` (`project(ParagonX1 VERSION x.y.z)`).
2. Add a `## x.y.z` section to `CHANGELOG.md`.
3. Commit and push.

When a push carries a version that hasn't been released yet, GitHub Actions builds all three
platforms, tags the commit `vx.y.z` and publishes the release with notes from the changelog.

## Building from source

Requirements: CMake 3.24+, a C++20 compiler (Visual Studio 2022, Xcode 15+, or GCC 11+/Clang 14+), and Git.
JUCE and Catch2 are downloaded automatically at pinned versions.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The plugin is written to `build/ParagonX1_artefacts/Release/VST3/` (the build target keeps its family name).

On Linux, install JUCE's dependencies first (the list is in `.github/workflows/build.yml`).

To render the editor to an image without a DAW:

```bash
PARAGON_SNAPSHOT=editor.png ./build/tests/ParagonX1Tests "[snapshot]"
```

## License

Paragon PR7-X is free software under the [GNU Affero General Public License v3](LICENSE.md),
which is required by JUCE's open-source licence.

It uses:
- [JUCE](https://github.com/juce-framework/JUCE): AGPLv3
- The Inter typeface (in `resources/fonts`) by Rasmus Andersson, under the SIL Open Font License

FabFilter Pro-Q and Pro-MB, oeksound soothe and Soundtheory Gullfoss are their makers' products. PR7-X's stomps are only inspired
by how they're described and behave; it isn't affiliated with or endorsed by any of them.
