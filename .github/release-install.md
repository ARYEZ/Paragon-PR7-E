
## Installing

Download the zip for your computer below, unzip it, and copy the `Paragon PR7-X.vst3` folder into your VST3 folder. Close your DAW first if you're replacing an older version.

| OS | VST3 folder |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3` |
| macOS | `~/Library/Audio/Plug-Ins/VST3` |
| Linux | `~/.vst3` |

**macOS:** the plugin isn't code-signed yet, so macOS blocks it until you run this once in Terminal:
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Paragon PR7-X.vst3"`

Then rescan plugins in your DAW. Your rigs are kept between versions.

**Coming from PR7-E?** It's the same plugin, renamed PR7-X in 0.20.0. Delete the old `Paragon PR7-E.vst3` from the same folder, or your DAW will list the plugin twice. Your projects open with PR7-X, and your rigs, presets and finish are copied over the first time it runs.
