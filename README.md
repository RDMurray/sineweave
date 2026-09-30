# Sineweave

Sineweave is a Windows VST3 instrument that turns an audio sample into a playable
sound. Hold a moment from the sample, or play through its changing tone at a
speed you choose. MIDI notes control the pitch independently of playback speed.

## Install

[Download the latest Windows installer](https://github.com/RDMurray/sineweave/releases/latest/download/Sineweave-Windows-x64-Setup.exe)

Requires Windows 10 or 11, 64-bit, and an audio host that supports VST3 instruments.

Close your audio host, run the installer, then rescan plugins in your host.
Sineweave installs into `C:\Program Files\Common Files\VST3\Sineweave.vst3`.
If your host does not find it, add `C:\Program Files\Common Files\VST3` to its
plugin search paths.

Run a newer installer to update. To uninstall, remove Sineweave through Windows
Installed Apps. The installer is unsigned, so Windows may show an
unknown-publisher or SmartScreen prompt.

## Play a sample

1. Add Sineweave to a MIDI track and open its editor.
2. Choose **Load audio file...**. WAV, AIFF, FLAC and Ogg are supported.
   MP3 support depends on the codecs available on your computer.
3. Choose **Mono mix**, **Left**, or **Right**. For stereo and multichannel files,
   Left and Right use the first two channels.
4. Set **Analysis start/end** to the part of the sample you want to use.
   Times are in seconds from the start of the file; an end value of zero uses
   the file end. Keep **Sustain position** inside this range.
5. Choose **Analyse / Re-analyse** and wait for **Ready** in **Analysis status**.
6. Set **Reference pitch** to the original pitch of your sample. The default
   is MIDI 69 (A4); fractional values let you fine-tune it.
7. Play MIDI notes. Adjust gain, ADSR, polyphony and partial limit to taste.

Changing analysis resolution or amplitude floor requires re-analysis. Loading a
new file keeps the previous sound until analysis succeeds.

## Playback and looping

**Frozen frame** holds the tone at **Sustain position**. Changing this position
changes the sound for new notes; notes already held keep their tone.

**Trajectory** plays through the sample's changing tone. **Playback speed** runs
from 0 to 4× without changing pitch: 0.5× plays twice as slowly, and zero holds
the current tone. Speed changes affect notes already playing.

Choose **Initial direction** and set the playback start/end times. Choose a
**Loop mode**:

- **Off** plays to the end of the playback range, then releases the note.
- **Wrap** returns to the opposite loop boundary.
- **Ping-pong** reverses direction at each boundary.

Keep the loop inside the playback range and at least 1 ms long. **Loop crossfade**
softens transitions. Check **Analysis status** after editing: invalid ranges
leave the last valid settings in use. Changes to mode, direction, ranges, loops
and crossfade apply to new notes.

## Keyboard and screen readers

Use Tab and Shift+Tab to move between controls, Space to activate buttons, and
arrow keys to change values or selections. On a numeric slider, press Enter or
F2, type a value, and press Enter to apply it. Escape cancels the edit.
**Analysis status** is a focusable, read-only field for checking progress and
errors.

If REAPER intercepts keys, enable its option to send keyboard input to the
plugin. Note names may use a different octave numbering convention from your
host; the MIDI number identifies the pitch.

**Preview sustain changes** plays a short preview when you change the sustain
position. Uncheck it to disable previews. Previews stop when the editor closes
and are disabled during host playback and recording. Your host must process
the plugin while stopped; in REAPER, enable **Run FX when stopped** if needed.
The preview toggle resets when the editor opens.

## Saving and performance

Save your host project to keep the analysed sound and plugin settings. Playback
works without the original audio file; keep the file if you want to re-analyse
it. After reopening a project, wait for **Ready** before playing or rendering.

Analysis is limited to about 87 seconds at 48 kHz. If a selection is too large,
choose a shorter range. Output is mono, sent to both stereo channels.

Dense sounds can use substantial CPU. Lower **Polyphony** or **Partial limit**,
or freeze the track if playback struggles. Gain starts at −24 dB; raise it
carefully to avoid clipping. MPE and MIDI 2.0 are not supported.

Sineweave is open source under [AGPLv3](LICENSE).
