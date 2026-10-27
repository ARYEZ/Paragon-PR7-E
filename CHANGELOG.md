# Changelog

Each version's notes appear on its GitHub Release page.

## 0.18.0

- **The four rows are now four rack units,** after the photo you sent (a Yamaha SPX90, a Lexicon PCM70 and a Roland SDE-3000 in a rack), drawn in the PR7's own parts: its footswitches, keys, knobs, LED bars and orange screen, in whichever finish you've chosen. In the signal chain's order: **row 1 ECHELON, row 2 DOMINION, row 3 CONSTRICTOR, row 4 the LIMITER.** Each unit, left to right:
  - **POWER:** the unit's footswitch, switching it on or off, as the stomps did. Clicking the word POWER, the unit's name or **EDIT** opens its big page.
  - **A ladder of LED bars** that lights as it works: ECHELON's shows how far its dynamic bands are moving, DOMINION's how deep it's cutting, CONSTRICTOR's its gain reduction, the LIMITER's the output level. They hold a peak and fall back like a meter's lights.
  - **The screen:** ECHELON draws its curve with each band as a dot in the gradient, and its band count; DOMINION draws what it's cutting across the spectrum, with its DEPTH; CONSTRICTOR shows its gain reduction in big digits, with threshold, ratio, character and bands; the LIMITER shows the true peak (held two seconds) and the integrated loudness, with its style, look-ahead and oversampling, and a bar for how much it's turning down.
  - **Three keys,** each with its light: ECHELON AUTO GAIN, ANALYSER, MATCH (opens EQ Match); DOMINION HARD (off is SOFT), M/S, DELTA; CONSTRICTOR VCA, OPTO, FET (its character); the LIMITER TRUE PEAK, UNITY, DELTA.
  - **Two knobs:** ECHELON OUTPUT and GAIN SCALE; DOMINION DEPTH and DETAIL; CONSTRICTOR THRESHOLD and RATIO; the LIMITER GAIN and OUTPUT.
  - **A small screen under EDIT:** ECHELON's auto gain, DOMINION's and CONSTRICTOR's preset (with a `*` once its settings have changed, as on their pages), the LIMITER's style.
- **ORACLE is now a small key beside BYPASS** (it was stomp 1): it opens ORACLE's page, where you switch it on, and its light shows when it's listening.
- **The rig pads and BANK are gone,** as agreed: change rigs with the arrows beside the screen, the header's **<** **>**, or the **PRESETS** tab.
- INSERTS and AUTO EQ aren't on the unit for now. They'll get their place when they're built.
- Nothing about the sound has changed, and projects and rigs load as before.

## 0.17.0

- **A four-row unit,** as you asked: the unit now reaches down to the signal chain, with the seven stomps on top and **three rows of rigs** under them, nineteen at a time, then **BANK ▼** and **BANK ▲** at the end of row 4. The signal chain stays as it was.
  - **BANK** now steps a whole bank at a time (a bank with more than nineteen rigs, like C with its guitar and bass genre rigs, goes on over a second page). So all of bank A's factory rigs, its own and the genre vocals, are on screen at once: A01 to A07 on row 2, the genre vocals from A11 on rows 3 and 4. Empty pads show no number.
  - **The three panels under the unit are gone** (the rig browser, the selected rig and ECHELON's overview), to make room. Nothing is lost: the **PRESETS** tab has the full rig browser, **SAVE** and **SAVE AS** are on the unit under the display, and each stomp's name opens its own page. ECHELON in the signal chain now opens ECHELON's page too, as the other blocks do.
  - The rows are a little shorter than before (the pads keep their look) so all four fit, and the INPUT and OUTPUT strips run the unit's full height.

## 0.16.1

- **The LIMITER pumps much less.** You were right: it pumped in every style. Two parts of my design caused it, and both are fixed:
  - Its slower stage followed every peak, so each kick turned the whole mix down for its RELEASE time (250 ms). It now follows the average level (over 100 ms), so a single hit hardly moves it, and each style gives it less of the work.
  - Its fast stage slowed its recovery after "long" limiting, but counted a run of kicks, or a mix limited all the time, as long limiting, so each kick's extra dip lingered. It now keeps track of how much the music usually needs turning down and comes straight back to that after a hit, slowing down only around it (where slow recovery means less distortion).
  - Measured on kicks over a loud mix that's limited all the time (GAIN +8 dB), 80 to 120 ms after a kick: MODERN's extra dip below the mix's own level went from 2.1 dB to 0.4 dB, CLEAR's from 3.3 to 0.4, SAFE's from 3.2 to 0.5. A new test keeps it that way. During the kick itself the mix still dips: when a kick hits the ceiling, a limiter that keeps the stereo image turns everything down with it (AGGRESSIVE dips least, as its clipper takes the kick's top). PUNCHY and GLUE still pump a little, on purpose.
  - TRUE PEAK's last stage now sits 0.12 dB under the ceiling (was 0.1), to stay within 0.2 dB in its hardest case.
- **New names:** the EQ is now **ECHELON** and the compressor **CONSTRICTOR**, on the stomps, the signal chain, their pages, and the parameter names your DAW shows. Projects, rigs, automation and presets are unaffected: only the names you see changed.
- **ECHELON's page in the gradient,** as DOMINION's, CONSTRICTOR's and the LIMITER's: its name in the red to pink to orange gradient, each band coloured by where it sits on that gradient (red in the lows, pink in the middle, orange in the highs), the whole curve in white, and the left, right, mid and side curves in the same family.

## 0.16.0

- **The LIMITER (stomp 7), inspired by FabFilter Pro-L 2**, as you asked. Our own design from its manual: FabFilter doesn't publish how Pro-L 2 limits, so this follows what the manual says each control does. Click LIMITER's name (or LIMITER in the signal chain) for its new page.
  - **GAIN into a ceiling:** the tall GAIN slider drives the sound into the **OUTPUT** ceiling, which nothing passes.
  - **Look-ahead:** the old limiter reacted to each peak only as it arrived, so when pushed it squared off the front of the peaks. The new one sees each peak coming (**LOOKAHEAD**, 0 to 5 ms; 1 ms by default) and glides the level down so it's exactly right when the peak arrives. In the tests, 5 ms of look-ahead halves the distortion on a bass note limited 5 dB. A slower stage follows the average level (**ATTACK**, **RELEASE**), and **LINK PEAKS** and **LINK RELEASE** set how much the left and right share each stage, as on Pro-L 2.
  - **Eight styles** with plain names: **CLEAR** (stays out of the way), **PUNCHY** (a little pumping, for punch), **DYNAMIC** (lifts the hits before limiting, so they keep their attack), **ALL-ROUND**, **AGGRESSIVE** (soft-clips the top first: loud and edgy), **MODERN** (the default: lets go quickly after a lone peak), **GLUE** (starts working 6 dB under the ceiling, like a bus compressor) and **SAFE** (never distorts). Our own versions of what each of Pro-L 2's styles is described as doing; the tests check that they differ the way their names say. BRICK, SOFT and AUTO, the earlier limiter, are still there for your older projects.
  - **TRUE PEAK:** keeps the peaks a converter or a streaming encoder rebuilds between the samples under the ceiling too, so -1 dBTP for Spotify really is -1. In the tests it holds within 0.1 dB (0.2 dB in the hardest case: AGGRESSIVE with no look-ahead or oversampling, where oversampling helps).
  - **OVERSAMPLING** 2x, 4x or 8x (less distortion, at more CPU), **UNITY** (hear it at the level it came in, so louder doesn't fool you), **DELTA** (hear only what it takes away), **DC FILTER**, **TRIM**, and **DITHER** (24 to 16 bits, with three kinds of NOISE SHAPING) for a final CD-style master.
  - **Meters:** the page scrolls the last few seconds (the level going in, in grey; what comes out, in the gradient; the gain reduction in red from the top, its biggest dips labelled; the loudness as a thin line; the ceiling dashed). Beside it, the true peak and gain reduction held since RESET (click them to reset too), and **loudness** to EBU R128: momentary, short term, integrated and range, against a **TARGET** (-14 LUFS for streaming, -9, -16, -23 or -24). In the tests the loudness meter reads EBU's own test signals to within 0.1 LU.
  - **Delay:** look-ahead, true peak and oversampling make the sound a little late (1 ms by default, about 3.3 ms with TRUE PEAK; shown on the page). Your DAW makes up for it on playback, but not when you monitor live through it; set LOOKAHEAD to 0 with TRUE PEAK and OVERSAMPLING off for no delay at all.
  - **Your projects and rigs sound the same:** the old THRESHOLD and MAKE UP become GAIN and OUTPUT, which gives exactly the same result. The mastering factory rigs (including the genre masters) now use MODERN with TRUE PEAK at -1 dBTP.
  - **CPU:** at its heaviest (TRUE PEAK, 8x, 5 ms look-ahead) it uses about 8% of one core here.
  - Not checked here: how it sounds on your music (I can't listen). Not built: presets, a side chain input, surround, and Pro-L 2's other display modes and K-meter scales.

## 0.15.0

- **Genre rigs and presets for nine heavy styles:** modern metal, metalcore, djent, nu metal, argent metal, hard rock, grunge, symphonic metal and industrial. Our own reading of how each style is usually mixed, not any producer's settings (none are published as mixing presets), and not tuned by ear: starting points to adjust on your tracks.
  - **A rig for each part of a song**, at the genre's number in every bank, so they're easy to find: **x11** modern metal, **x12** metalcore, **x13** djent, **x14** nu metal, **x15** argent metal, **x16** hard rock, **x17** grunge, **x18** symphonic metal, **x19** industrial. **A** the vocal, **B** the drum bus, **C11-C19** the guitars, **C21-C29** the bass, **D** the mix bus, **E** the master (finished by the LIMITER at -1 dB, as the other mastering rigs are). So A13 is the djent vocal and E13 the djent master.
  - **What makes them differ**, for example: hard rock's guitars are mid-forward (body at 1 kHz) where nu metal's are scooped (700 Hz down); djent's and argent metal's guitars keep their low strings (low cut at 70 and 60 Hz) and turn down the palm-mute boom only when it blooms; symphonic metal's guitars and drums leave room for an orchestra and a soprano (lows cut higher, a dip at 2.5 kHz), and its mix is wider on top; argent metal's mix and bass carry a big sub, and its choir sits darker behind the guitars; grunge is darker and mid-heavy; industrial is hard, mid-forward and pumping.
  - **Matching COMP and DOMINION presets:** each genre rig's COMP and DOMINION settings are presets of the same name, so you can put just a genre's compressor or resonance control on a track without loading the whole rig. In COMP's PRESET menu, under **GENRES**, each genre has its vocal, drums, bass, mix bus and master; in DOMINION's, its vocal, guitars and mix. Each genre is a submenu, so the menus stay short.
  - **Long rig names fit on the pads:** a name like SYMPHONIC METAL GUITARS is set a little smaller to fit whole, rather than cut off.
  - **The rig row** starts a new page of five at each ten, so BANK ▲ goes from a bank's own rigs straight to its genre rigs (A11-A15, then A16-A19). Rigs you save still go in the first free slot (A08 on), on the page before them.
  - In the tests: every genre rig stays within the factory limits (at most +4.5 dB anywhere; masters within ±2 dB and holding -1 dB), plays noise cleanly, holds exactly its presets' settings, and does what its notes say (the curves checked at the frequencies named, and the guitars' boom band acting only on loud chugs). Every new COMP preset keeps typical material clean and near its level. Not checked here: how they sound on your music (I can't listen). Compressor thresholds assume tracks peaking around -10 dBFS, so move THRESHOLD if yours are louder or quieter.

## 0.14.0

- **COMP's own presets**, as DOMINION has: a **PRESET** button at the top of COMP's page (beside BACK TO PEDAL), apart from PR7-E's rigs. Loading one sets all of COMP's settings (the full band and whether it's in use, the bands, LOOK-AHEAD and MIX); it leaves whether COMP is on, and everything else in the rig, alone. One undo takes it back.
  - **Factory presets**, by category: *Vocals* (Vocal Leveler, Vocal Smooth & Bright, Vocal Body), *Drums* (Drum Bus Glue, Kick Tight, Snare Punch, Room Gate), *Instruments* (Bass Even, Guitar Fizz Control, Piano & Keys Even), *Mix & Master* (Mix Bus Glue, Master Four Bands, Low End Control, De-Harsh, Air Lift) and *Creative* (Upward Squash), plus Default. Our own starting points from common mixing moves, not anyone's presets. Compressor thresholds depend on how loud your material is, so set THRESHOLD by ear (a few dB of gain reduction is usually plenty). In the tests, every one keeps typical material clean and close to its level.
  - **Your own:** *Save as...* keeps COMP's settings under "Your presets"; *Delete* removes one of yours; *Show the presets folder* opens "Comp Presets", beside your rigs (small `.pr7comp` files).
  - The button shows the preset's name, with a `*` once you've changed something.
- DOMINION's presets now share the same code (nothing changes for you: same menu, same files).

## 0.13.0

- **COMP gets bands: multiband compression and expansion, inspired by FabFilter Pro-MB.** Up to six bands, each a compressor for its own part of the spectrum: tighten only the lows, calm only the harsh top, glue the mids, bring up quiet detail or push down noise in one band only. Our own design from Pro-MB's manual (Pro-MB's own method isn't published). Click COMP's name (or COMP in the signal chain) for its new page.
  - **Bands placed anywhere:** double-click the graph to add one (an octave wide; near either end it reaches the end). Drag its dot sideways to move it and up or down to set how far it can go (RANGE); drag either edge to widen or narrow it; mouse wheel over a band for its THRESHOLD; right-click for its menu; double-click its dot to switch it off.
  - **Four kinds of dynamics**, from MODE and RANGE, as on Pro-MB: **COMPRESS** with RANGE below 0 turns loud parts down (ordinary compression); above 0 it brings quiet parts up (upward compression: body and detail without touching the peaks). **EXPAND** with RANGE below 0 turns quiet parts down (a gate, for spill and noise); above 0 it brings loud parts up (upward expansion: more punch on hits). RATIO sets how strongly, KNEE how gradually, RANGE how far at most.
  - **Each band:** THRESHOLD, RANGE, RATIO, KNEE, ATTACK, RELEASE, CHARACTER (COMP's VCA, OPTO or FET, our addition: Pro-MB has none), STEREO (both channels with LINK, or the middle or the sides only), its own OUTPUT, the SLOPE of its edges, LISTEN to hear only its part, and LOOK-AHEAD.
  - **Transparent when idle:** a band isn't a crossover splitting the sound. It's a pair of the EQ's shelves that only exist while the band is changing the level: a band that isn't working leaves the sound exactly as it was (measured: to within a millionth), with no delay.
  - **LOOK-AHEAD** (top of the page): each band can hear up to 20 ms ahead, so it's ready before a peak arrives. While it's on, COMP is 20 ms late, which your DAW makes up for on playback (not when you monitor live through it); off, there's no delay at all.
  - **MIX** from 0 to 200% of the bands' effect.
  - **The full-band compressor** (PR7-C's, as before) now sits on the page's right, after the bands, with FULL BAND to switch it. It's on by default, so your rigs and projects sound exactly as they did.
  - In the tests: each kind does what it says only in its band (a tone below and one above stay within 0.3 dB), the band is flat inside and half way at its edges, a sides-only band leaves the middle alone, and six steep bands with look-ahead use about 4% of one core. Not checked here: how it sounds on your material (I can't listen); the bands' timing and detectors are COMP's, our own numbers.
  - Not in this version: FET's grit per band (the full band keeps it), COMP presets (next), and an analyser on the graph.

## 0.12.0

- **The unit laid out like a DigiTech GSP21's foot controller**, as you asked. The screen is centred along the top; the seven stomps sit in one row, and under them, as on the GSP in its five-preset mode, five rigs and **BANK ▼** / **BANK ▲**. Each switch has its number and light above it and its name under it, as on the GSP.
  - **The rig row:** a pad loads its rig, and its light shows which one is loaded. The numbers are the rigs' slots (A01, A02...) and the names are under the pads. BANK steps through the rigs five at a time, within a bank and then on to the next, without loading one, so you can look before you switch, as on the GSP. Load a rig any other way (the arrows, the browser, a project) and the row turns to its five.
  - Our own finish and colours, the GSP's arrangement; no DigiTech branding.
- **No expression pedal.** In PR7-E it could only be a volume pedal, and your DAW's fader and automation do that better, so the pedal, its EXPRESSION ASSIGN panel and its MIDI setting in SETTINGS are gone, and the unit takes the full width. Old projects and rigs still load: one saved with the volume pedal pulled back now plays at full level instead of quiet (the pedal no longer does anything), so check the level of such a project once.
- **Fewer small keys:** UNDO, REDO and BYPASS stay. TUNER (stomp 1, ORACLE, does it), RESET and TAP went, and so did the BPM and TAP in the header: nothing in PR7-E has used a tempo since the delay went in 0.3.0.
- **The dock's middle panel** (the EQ's curve, or COMP's or the LIMITER's knobs) is wider, into the space the pedal's panel had.
- **The signal chain in the gradient:** EQ red, DOMINION deep pink, COMP hot pink, LIMITER orange, following the red to pink to orange of the menus and of DOMINION's and ORACLE's pages, instead of four unrelated colours.
- CI: pluginval's own Linux shutdown crash (which stopped 0.10.2) gets one more try; any other failure, or a second crash, still fails the build.

## 0.11.0

- **ORACLE (stomp 1): melody to MIDI.** The tuner grows into ORACLE: it listens to a part as your DAW plays it, writes down its notes, lets you fix them, and hands them over as a MIDI file you drag onto a track. Inspired by what Melodyne and VariAudio do with a single line; our own design (theirs isn't published). One voice or instrument at a time, as the tuner was.
  - **Listening:** switch ORACLE on (stomp 1, or ON on its page) and play the part. Every 10 ms it reads the pitch and the level, and notes where it is in the song from the DAW (position, tempo, time signature). Only while the DAW plays; play a passage again and that take replaces what it heard there (one UNDO brings the old notes back).
  - **Finding the notes:** a note ends at a silence, at a move to another note that stays, or at a fresh attack on the same note (repeated notes). Vibrato and scoops don't split a note: it judges the pitch smoothed over about 150 ms, and a note's pitch is the middle of what was sung. Velocity comes from how loud each note was. In the tests (a sung phrase with legato changes, a gap, wide vibrato and repeated notes, played through the plugin as a DAW would) every note starts and ends within about 4 to 16 ms of where it was played; a bass line's low notes (from 41 Hz) work too. **SENSITIVITY** sets how readily it splits notes, **MIN NOTE** the shortest note it keeps; changing either finds every note again (one undo).
  - **Fixing them:** click ORACLE's name for its page, a piano roll with the notes over a thin line of the pitch it heard, the song's bars along the top. Drag a note up or down for its pitch, drag its left or right end for where it starts and ends, Alt-drag to move it in time; double-click a note to split it, or empty space to add one; J joins, Delete removes, the arrow keys move a semitone (Shift: an octave). ORACLE has its own UNDO and REDO (Ctrl+Z, Ctrl+Shift+Z). The bottom strip says where the selected note is (bar and beat), how long, its velocity and how far from the note it was sung, or how in tune all the notes were on average.
  - **Into the DAW:** drag the **DRAG MIDI** tile onto a MIDI track, at bar 1: the file starts at bar 1 with the song's tempo and time signature, so it lines up. **QUANTIZE** (off, 1/32 to 1/4, with triplets) snaps the file's notes to the grid; the notes on the page stay as sung. Clicking the tile shows the file in its folder instead, for a DAW that won't take a drag. Not checked here: the drag itself in Reaper, Studio One and Cubase (it needs a DAW on Windows), so tell me if one of them doesn't take it or puts it in the wrong place.
  - **Kept with the project:** the notes, what was heard and the settings are saved in your DAW session.
  - The tuner is part of ORACLE: the pedal's screen still shows the note and cents, and ORACLE's page shows the note now. The dock's old TUNER panel (the 30-second trace) is replaced by ORACLE's page.
  - Not built yet: correcting the pitch of the audio itself (Melodyne's other half; a separate, bigger step), chords and full mixes, and MIDI sent live from the plugin.

## 0.10.3

- **DOMINION's own presets.** A **PRESET** button on DOMINION's page (top right, before BACK TO PEDAL) opens a menu of presets for DOMINION alone, apart from PR7-E's rigs. Loading one changes only DOMINION's settings (mode, depth, detail, timing, tilts, stereo, output and its depth-curve bands); it leaves the EQ, COMP, the LIMITER, everything else in the rig, and whether DOMINION is switched on, alone. One undo takes it back.
  - **Factory presets**, by category: *Vocals* (Vocal De-Harsh, Vocal Sibilance, Vocal Mud & Boom), *Drums* (Overheads Ring, Snare Ring, Kick Boom), *Instruments* (Acoustic Guitar, Electric Guitar Fizz, Bass Smoother, Piano & Keys), *Mix & Master* (Mix Bus Gentle, Master Polish) and *Creative* (Hard Carve), plus Default. Our own starting points: the depth curve steers each one to where that kind of problem usually lives (sibilance at 4 kHz and up, boom under 250 Hz on a kick...). They're settings chosen on paper and checked in the tests, not tuned by ear on your material, so treat them as places to start.
  - **Your own:** *Save as...* keeps DOMINION's settings as a preset under "Your presets"; *Delete* removes one of yours; *Show the presets folder* opens "Dominion Presets", beside your rigs (small `.pr7dom` files you can copy to another computer).
  - The button shows the preset's name, with a `*` once you've changed something.
- **DOMINION's band dots and title in the gradient.** The depth-curve band dots (and the dot by the selected band's name) take the red to pink to orange gradient at their frequency, red in the lows to orange in the highs, as the cuts do; the DOMINION title on its page is filled with the gradient too. They were violet.
- **0.10.2 wasn't published:** its Linux build crashed in one of the plugin checks (pluginval's parameter fuzzing), and the same check with the same random seed passes here every time. Its change (the cuts in the gradient) is in this release. A new test now does the same kind of fuzzing inside our own test suite (random settings on half the parameters, changed as audio plays, mono and stereo, 1,500 rounds) and the output stays clean. A build with memory and undefined-behaviour checking switched on (which stops at the first bad memory access) also passes CI's whole check, with CI's own random seed. So the crash isn't something wrong with our code that we could find; it may be in the checker itself or in the CI machine. If it comes back, we'll look again.

## 0.10.2

- **DOMINION's cuts in the menus' colours.** On its graph, what DOMINION is cutting is now drawn in the same red to pink to orange gradient as the menus' highlight (left to right across the spectrum: red in the lows, pink in the middle, orange in the highs) instead of violet. With stereo or mid/side unlinked, the second channel's cuts show fainter behind.

## 0.10.1

- **Menus in the PR7 family's colours.** Every drop-down and right-click menu (DOMINION's band shapes, the EQ's, MODE, CHARACTER, the rig menus...) is now black with white text, as on PR7-C, and the item under the mouse lights up with a red to pink to orange gradient. They were the plain grey menus before.

## 0.10.0

- **DOMINION (stomp 5): dynamic resonance suppression, inspired by oeksound soothe 3.** It finds the parts of the sound that ring, whistle or build up as the track plays, and turns just those down, moment by moment, then lets go. Our own design from soothe's manual (soothe's algorithm isn't published, and the numbers in ours are our choices). Press stomp 5 to switch it on; click its name (or DOMINION in the signal chain) for its page.
  - **How it listens:** a bank of filters 1/12 octave apart, 20 Hz to 20 kHz, each measuring its part of the spectrum. In the tests, a ringing tone 12 dB above pink noise is turned down by about 10 dB at the default settings while the noise around it moves less than 0.2 dB.
  - **MODE:** **Soft** finds what sticks out above the sound around it, so it works the same whatever the level (vocals with loud and quiet phrases). **Hard** works on what's loud (over a level set by DEPTH): firmer, level-dependent, and a sound-design tool when pushed.
  - **DEPTH** (how much), **DETAIL** (broad build-ups like boom and mud, up to narrow ringing), **ATTACK** and **RELEASE** (frequency-dependent: quicker up high, slower down low).
  - **The graph** shows what it's cutting right now, across the spectrum. Over it, the **depth curve**: up to 8 bands (Low Cut, Low Shelf, High Shelf, High Cut, Bell, Band Pass, Band Reject, Tilt) that make it work harder where you raise the curve and leave alone where you lower it. Double-click to add a band (a cut at either end), drag its dot, mouse wheel for Q, double-click a dot to switch it off. Band passes work in parallel: only their parts are processed.
  - **Each band's controls:** SHAPE, FREQ, DEPTH, Q, SLOPE, FOCUS, and **LISTEN** (hold it to hear only what DOMINION takes away in that band's part of the spectrum).
  - **STEREO:** L/R or M/S, **LINK** (100%: both channels listened to together and cut alike; 0%: each on its own) and **FOCUS** (more on one channel).
  - **TILT:** DETAIL, ATTACK and RELEASE for the lows and the highs separately (e.g. a slower attack in the bass to keep its punch).
  - **SCALE:** **MAX CUT** (the most it may cut anywhere) and **WET TRIM**. **OUTPUT:** **DELTA** (hear only what it takes away), **MIX** and **OUT**.
- **Zero latency**, like soothe's low-latency mode: no look-ahead, so it reacts a moment later than it could with one (the trade-off for no delay). Not included: soothe's higher-quality modes (they add delay), sidechain, and multichannel.
- **In the chain:** after the EQ, before COMP. It uses about 11% of one core in stereo.
- **Factory rigs with DOMINION:** Lead Vocal, Overheads, Acoustic Guitar and Mix Harshness Control (gentle settings, with a low cut on the depth curve so the low end is left alone).

## 0.9.0

- **The COMP stomp (2): PR7-C's compressor, ported.** Stomp 2 was the open slot; it's now a full channel compressor, the same as on PR7-C (its code and tests), so it behaves the same. Press stomp 2 to switch it on; click its name for its knobs in the dock.
  - **CHARACTER:** **VCA**, clean and precise (follows loudness, near-hard knee); **OPTO**, smooth, a wide soft knee, gentler attack and a release that stretches the longer it works; **FET**, fast and aggressive (catches every transient), with a touch of grit that grows as it works harder.
  - **THRESHOLD** (-50 to 0 dB), **RATIO** (Soft, then 1.5:1 up to infinite; **Soft** is a curve whose ratio rises with level: about 3:1 at 8 dB over, 6:1 at 20 dB over), **ATTACK** (0.1 to 30 ms), **RELEASE** (50 ms to 3 s), **AUTO** release (quick after a short peak, slower after long compression, so it doesn't pump), **MAKE UP** (up to 20 dB) and **LINK** (both sides together, so the stereo picture stays put).
  - The panel's **GAIN REDUCTION** meter shows how much it's turning down.
- **It comes after the EQ**, so what the EQ cuts doesn't drive it (and a boost does); then OUTPUT and the LIMITER. The signal chain at the bottom now shows EQ, COMP, LIMITER. It listens to the sound itself: PR7-C's sidechain filters aren't part of PR7-E.
- **Factory rigs with COMP:** Lead Vocal (OPTO), Rap Vocal (VCA), Drum Bus (VCA, slow attack, AUTO release) and Mix Bus Polish (VCA, 1.5:1). Their thresholds suit recordings peaking around -10 dBFS: move THRESHOLD for louder or quieter tracks.
- No delay: still zero latency.

## 0.8.0

- **The LIMITER stomp (7): PR7-C's limiter and output trim, ported.** The same limiter as on PR7-C, code and tests, so it behaves the same. Press stomp 7 to switch it on; click its name for its knobs in the dock.
  - **THRESHOLD** (-20 to 0 dB, -1 to start): the ceiling. **MAKE UP** (0 to 12 dB) turns up the limited sound, raising the ceiling with it. **MIX** blends in the unlimited sound (below 100%, some of the original peaks come back). **LINK** turns both sides down together, so the stereo picture stays put.
  - **MODE:** **Brick**, a hard ceiling; **Soft**, which eases in from 3 dB below the threshold and lets go more gently; **Auto**, Brick's ceiling, letting go quickly after short peaks and slowly under steady limiting.
  - **OUTPUT TRIM** (+-12 dB) after it, as on PR7-C: it moves the ceiling with it. It only applies while the stomp is on.
  - The panel's **GAIN REDUCTION** meter shows how much it's turning down, and the stomp shows the ceiling (threshold + make up + trim).
- **It's last in the chain**, after OUTPUT, RIG LEVEL and the volume pedal, so nothing after it can push a peak past the ceiling. Turning OUTPUT up drives into it, as into a mastering limiter.
- **No look-ahead, so no delay** (the same trade-off as on PR7-C): when it limits hard, the very start of each peak is squared off a little. It limits the samples, not the peaks between them, so a converter can still see a peak a fraction of a dB over (the reason for -1 dB rather than 0).
- **The mastering factory rigs** (bank E) now end with it: Brick, a -1 dB ceiling, as a safety net against overs rather than to make them louder.
- The signal chain at the bottom now shows EQ, then LIMITER.

## 0.7.0

- **EQ Match: make your track's tone like a reference's.** Click **MATCH** at the top of the EQ page. It listens to your track and to a reference, compares their overall tone (how much low end, middle and top each has, averaged over time), and proposes EQ bands that make your track sound like the reference. Inspired by Pro-Q 4's EQ Match, as its manual describes it; how ours analyses and picks bands is our own design.
  - **YOUR TRACK:** opening MATCH starts listening (RECORDING). Play the part you want to match for a few seconds or more (longer is steadier). CLEAR starts again. Pauses and silence don't count.
  - **REFERENCE**, three ways: **RECORD INPUT** (move to another part of your song and play it, e.g. last week's vocal take to match today's punch-ins), **LOAD FILE...** (a song: WAV, AIFF, FLAC, Ogg or MP3), or **SAVED...** (a reference saved before). SAVED also saves your track or the reference for another time; they're kept beside your rigs, in "Match References".
  - **On the display:** a thick line where the EQ would need to be to match, and a dashed line for the bands it proposes. **DETAIL** chooses a few broad bands (the overall tone) or more, finer ones. **AMOUNT** chooses how much of the difference to make up: 100% all of it, 50% halfway.
  - **ADD BANDS** adds them as ordinary bands, to change or delete like any others (and switches the EQ on if it was off). One undo takes them all back.
  - It matches tone, not loudness, and it allows for the bands you already have. It doesn't follow the reference where either one has next to nothing (a reference with no deep bass doesn't cut yours away), and no band it proposes goes past 12 dB.
- **In the tests:** noise matched to a file of the same noise through a known EQ comes out within 0.7 dB of the reference's tone, from 50 Hz to 15 kHz.
- **Not included:** listening to another track live (that would need a sidechain, left out for now) and dragging a file onto the plugin.

## 0.6.0

- **Factory rigs: 30 starting points for the EQ**, in the PRESETS tab, filed by what they're for (search their tags, e.g. "vocal" or "dynamic"). Each says what it does in its notes.
  - **Bank A, vocals and voice:** Lead Vocal, Vocal De-Esser, Rap Vocal, Backing Vocals, Spoken Voice (podcasts, voice-overs), Vocal Mud Control. Init stays at A01.
  - **Bank B, drums:** Kick Punch, Kick Boom Control, Snare Crack, Hi-Hat Tame, Overheads, Drum Bus.
  - **Bank C, bass, guitars and keys:** Bass DI, 808 Control, Electric Guitar, Acoustic Guitar, Piano, Pad, Make Room.
  - **Bank D, the full mix:** Mix Bus Polish, Mix Mud Control, Mix Harshness Control, Mix Wider Top, Mix Warmer, Mix Brighter.
  - **Bank E, mastering:** Master Gentle Polish, Master Low-End Control, Master De-Harsh, Master Width, Master Warm Tilt, Master Bright Tilt.
- **What they are:** common mixing moves (cutting rumble and mud, adding presence and air, keeping the bass centred), not copies of anyone's presets. Problems that come and go (sibilance, boomy notes, harsh moments) use dynamic bands with AUTO, so they only act when the problem is there. No rig boosts more than 4.5 dB anywhere; the mastering rigs move 2 dB at most. Every track is different: load one, then adjust by ear (and save your version with SAVE).
- **Not yet:** the mastering rigs are EQ only. A limiter comes with stomp 7.
- Your own rigs in bank A now file from A08 (Init and the vocal rigs take A01 to A07). Rigs you've already saved keep their places; a factory rig shows first if one shares a slot.

## 0.5.0

- **Dynamic EQ.** Any bell, shelf or tilt band can now be **dynamic**: its gain moves only when its own part of the sound gets loud, like a compressor working on just that part of the spectrum. Inspired by Pro-Q 4's dynamic EQ, as its manual describes it; how ours listens and reacts is our own design (FabFilter doesn't publish theirs).
  - **DYN RANGE** (beside GAIN): how far the band moves, from -30 to +30 dB. **Below 0 it cuts** when its part gets loud: tame a harsh note, sibilance, a boomy bass note, only when they happen. **Above 0 it boosts**: bring out a snare's crack or a pick's attack. 0 is an ordinary band. A band can have both: say +6 dB of gain that falls back to level when its part gets loud.
  - **It listens to its own part of the spectrum** (the same part SOLO plays), on the channel it works on: a loud bass note doesn't set off a 5 kHz de-esser, and a left-only band listens to the left.
  - **AUTO** (on by default): the threshold sits 6 dB above the band's usual level over the last few seconds, so it acts on what stands out, whatever the track's level. Switch AUTO off to set **THRESHOLD** yourself.
  - **ATTACK** and **RELEASE**: at 50% they're set from the band's frequency (low bands need longer to measure a note: 2 ms high up, up to 50 ms in the lows; release 15 times that). Lower is faster, higher slower, from a quarter to four times.
  - **On the display:** a dynamic band shows a dashed curve where its whole range would take it, a handle there to drag, and a white ring where its gain is right now; the curves move as it works. **Alt+double-click** makes a dynamic band, **Alt + mouse wheel** sets the range, and the right-click menu has **Make dynamic / Make static**. While a band is dynamic its controls get a second row: AUTO, THRESHOLD, ATTACK, RELEASE, and a meter of what it hears (against its threshold) and how far it's moving.
  - Moving it to and from dynamic glides, without clicks. Cutting a held low note adds no audible distortion (the harmonics measure 87 dB down at the fastest attack).
- **Not yet:** a sidechain input (a band listening to another track) isn't included. AUTO GAIN works from the bands' set gains, not their dynamic moves. EQ Match comes next.

## 0.4.0

- **The EQ stomp: a new EQ, inspired by FabFilter Pro-Q 4.** It replaces the 9-band graphic EQ. Our own design, from Pro-Q's manual and published filter methods (not FabFilter's code). Click stomp 4's name (or OPEN EQ in the dock) for its page.
  - **Up to 24 bands**, each a **Bell, Low Shelf, Low Cut, High Shelf, High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt or All Pass**, with **slopes from 6 to 96 dB/oct** for the cuts and shelves.
  - **Stereo, Left, Right, Mid or Side, per band:** for example, lift only the sides' top end, or cut the middle's low mud.
  - **Analog-matched, zero latency.** Most digital EQs squash curves near the top: a +12 dB bell at 15 kHz comes out up to 7.5 dB off its analog shape. These stay within about 1 dB, and exact at each band's own frequency (within 0.3 dB at 96 kHz). Bands can sit above 20 kHz, up to 30 kHz, where only their slope reaches the audible range.
  - **The page:** a big display with the curve, each band's shape, and a spectrum of what goes in (shaded) and comes out (a line). Double-click to add a band (a bell, or a cut at either end); drag its dot for frequency and gain; mouse wheel for Q (or the slope of a cut); Alt-click to switch it off; right-click for its menu; Delete to remove it. Below, the selected band's controls, and **SOLO** (hold it to hear only what that band works on).
  - **OUTPUT, GAIN SCALE** (0% to 200% of every band's gain) and **AUTO GAIN** (turns the level down by about as much as the curve turns it up, so you can judge the EQ by ear, not by loudness; an estimate from the curve, as in Pro-Q).
  - **No clicks or zipper noise:** moving a band glides, and changing what a band is (its shape, slope or placement, or switching it) crossfades.
  - The dock shows the EQ's curve in place of the graphic EQ.
- **Projects from 0.3.0** with the graphic EQ on come back with the new EQ on and flat (the graphic EQ's settings are gone).
- **Not yet:** dynamic EQ and EQ Match come next. Not planned: linear phase and the character modes.

## 0.3.0

- **Stereo stays stereo.** Until now PR7-E played a stereo track's left channel on both sides, even with everything switched off: the effects that came from PR7-B were built for a mono instrument. Now both channels pass through exactly as they came in, apart from what the stomps do to them, and BYPASS keeps both channels too. A mono track still plays on both speakers.
- **PR7-B's old effects are gone:** pitch, comp / gate, filter / pitch, glitch, modulation (AEON), delay, reverb and lo-fi. PR7-E is a utility processor: input, its seven stomps, output.
  - The signal chain at the bottom now shows just the EQ, and grows as the stomps are built.
  - The graphic EQ (stomp 4, for now) and PRESENCE work on each channel separately.
  - The expression pedal can be a **VOLUME** pedal. The effects it could also control are gone, and a project or rig that had the pedal on one of them now loads with the pedal unassigned (rather than as a volume pedal sitting at the heel, which would be silent).
- **Next:** the new EQ, inspired by FabFilter Pro-Q 4, on a big EQ page of its own, replacing the graphic EQ.

## 0.2.0

- **The TUNER stomp checks the pitch of recorded parts.** Built for checking a vocal or a bass line as it plays back, rather than for tuning an instrument. One part at a time, up to 1,500 Hz (a bass's lowest notes to a high vocal).
- **Its panel:** click TUNER's name (stomp 1) and the dock shows:
  - **The current note**, how many cents off it is, and its frequency.
  - **A trace of the last 30 seconds:** the pitch drawn against the notes, so you can see where a note goes flat, slides in, wavers or drifts. Each note is coloured by where it centres: green within 5 cents, orange within 15, red beyond.
  - **The average:** how far the notes in the trace sit from the nearest note, e.g. "+12 cents, sharp", and over how many seconds of notes. It answers "is this part in tune overall?" A part sitting halfway between two notes reads as 50 cents out, not as "in tune".
  - **RESET** empties the trace and starts the average again, e.g. just before the passage you want to check.
- **It listens to both channels.** A stereo track is heard as left and right together, so a part panned anywhere is measured. (It used to hear only the left channel.)
- **It never changes the sound.** The tuner's MUTE is gone, from SETTINGS too: when checking a recording you want to hear it. On the unit, the MUTE key is now **RESET**. A project saved with 0.1.0 with the tuner on and muting now plays out loud.
- **It keeps listening with the plugin window closed:** play the passage, then open the window and the trace is there. It follows the audio's own time, so when the DAW stops, the trace stops too, instead of filling with the last note.
- The reference pitch (A4 = 440 Hz unless you change it in SETTINGS) applies to everything above: a recording tuned to A = 432 Hz reads 32 cents flat against 440, or in tune against 432.

## 0.1.0

- **The utility processor, based on Paragon PR7-B 0.11.0.** The first release of Paragon PR7-E, the PR7 family's utility processor.
- **Its own plugin.** Paragon PR7-E has its own name, ID and plugin code, so your DAW sees it as a separate plugin: it installs alongside PR7-G, PR7-B, PR7-V and PR7-C instead of replacing any of them. It keeps its own rigs (in a "Paragon PR7-E" folder) and remembers its own finish. DAWs file it under Tools.
- **The -E mark.** The logo reads PR7-E, in the same slanted, speed-cut lettering as its siblings, and the badge says UTILITY PROCESSOR.
- **Seven stomps, three over four.** The top row is **1 Tuner, 2 Open, 3 Inserts**; the bottom row **4 EQ, 5 Resonance, 6 Auto EQ, 7 Limiter**, laid out like PR7-V, with each top stomp sitting between two below it.
  - **TUNER** works now: it's the tuner PR7-B had.
  - **EQ** switches the 9-band graphic EQ for now, until the planned EQ (inspired by FabFilter Pro-Q 4) takes its place.
  - **OPEN, INSERTS, RESONANCE, AUTO EQ and LIMITER** are still being planned. Their lights stay off and pressing them does nothing yet. Their names are placeholders too.
- **No bass blocks.** The NAM amp, the cabinet IR loader, the AMP, CABINET and RIG panels, and the OD / DISTORTION stomp (OBSIDIAN and GOTHAM) are gone, along with PR7-B's bass factory rigs. One factory rig, **Init** (everything off), is the starting point until PR7-E has its own.
  - Nothing in the chain looks ahead any more, so PR7-E adds no latency, and it uses about a quarter of the CPU PR7-B did.
- **Still here from PR7-B:** pitch, comp / gate, filter / pitch, graphic EQ, glitch, modulation (with AEON), delay, reverb and lo-fi, in the signal chain at the bottom (click a block for its knobs), plus the expression pedal, tap tempo, undo and rigs.
