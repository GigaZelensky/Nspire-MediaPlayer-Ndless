# Controls

Press `Catalog` during playback to open the built-in controls guide.

### Shared Controls

These work in the picker, playback and resume prompt. Playback shortcuts are
inactive while entering a password.

| Key | Action |
| --- | --- |
| `C` | Cycle theme color |
| `S` | Save a BMP screenshot, including player error dialogs (disabled for encrypted video) |
| `O` | Open processor speed settings; also available from the picker's MHz label |
| `Scratchpad` | Save state and open OS Scratchpad |

### Picker

- `Up` / `Down` or keypad `8` / `2`: select movie
- touchpad: move cursor
- touchpad hover: show the title, filename metadata and video duration
- touchpad click: open highlighted movie
- `Enter` or keypad `5`: open movie or folder
- `Right` / keypad `6`: open selected folder; `Left` / keypad `4`: parent folder
- `U` or the USB button: connect/rescan a USB drive
- `Esc`: parent folder, or exit from the library

### Movie Filename Metadata

The picker hides the full video extension and supports optional bracketed metadata in movie filenames.

Example:

```text
Rick and Morty S07E03 [English SDH].nvp.tns
```

The list row shows `Rick and Morty S07E03`. Hover briefly to see the clean title, `English SDH` metadata and the video duration on separate lines. Multiple bracketed tags are joined with ` | ` in the tooltip.

### Playback

- `Space`: play / pause with a small pause indicator, or restart when the movie has ended
- touchpad: move cursor and show the UI
- `Enter` / keypad `5` / touchpad click: play / pause, restart at end, click hovered controls, or seek inside the bottom UI band
- `Left` / `Right` or keypad `4` / `6`: seek `-5s` / `+5s`
- keypad `7` / `9`: switch to the previous / next video in the current directory
- `Up` / `Down` or keypad `8` / `2`: increase / decrease audio volume (hold to repeat)
- `Shift` + `Up` / `Down` or `Shift` + `8` / `2`: increase / decrease screen brightness
- `Ctrl` + `M`: mute / unmute, or retry a failed audio connection
- Volume keys show a temporary vertical slider on the right. Moving the pointer to the middle of the right edge also reveals it; click its speaker to mute, or drag/click the slider
- `N`: toggle night mode
- `Ctrl` + touchpad `Up` / `Down`: enable night mode, then adjust its intensity; hold to repeat. At 0%, night mode is off.
- `Tab`: single-frame step while paused, hold to repeat
- `P`: cycle playback mode: `PLAY ONCE`, `REPLAY`, `AUTO NEXT`
- `R`: toggle realtime sync, allowing displayed-frame drops instead of slowdown
- `/`: cycle scale mode
- `Ctrl` + keypad `1`-`9`: align video
- `{` / `}`: decrease / increase playback speed
- `^`: cycle subtitle placement
- `+` / `-`: increase / decrease subtitle size, down to hidden
- `F`: cycle subtitle font
- `T`: cycle subtitle track
- `M`: toggle memory / playback diagnostics overlay
- `D`: toggle performance recording and diagnostic logging (off by default)
- `Catalog`: open / close the help overlay
- `Esc`: close help, or leave the movie if help is not open
- `On`: smoothly turn the screen off or back on; while off, `Esc` saves history and returns to the OS home menu

Night mode also works in the picker and resume prompt. Its intensity is remembered while the app is open.

Seeking shows the frames leading up to the destination. Play/pause works during
that catch-up, and repeated seek presses extend the destination. On arrival,
playback returns to the paused or playing state from before the seek.

### Processor Speed

Click the track, drag the knob, or use Left/Right or 4/6. The orange mark shows
the applied speed. **Test** compares the selected speed with the default,
then restores the previous setting. **Apply** changes this session;
the choice is saved in `ndvideo-data/ndhistory.ts.tns` only on normal app exit. A reset or crash
won't save a new choice. The short benchmark measures speed, not long-term stability.

Default uses the normal unplugged speed even on USB: 132 MHz on CX and
396 MHz on CX II. Exit restores the OS's original clock settings unless
**Keep speed after exit** is checked. This option is saved with the selected speed.

### Resume Prompt

- `Left` / `Right` or keypad `4` / `6`: choose `CONTINUE` or `START OVER`
- touchpad: move cursor
- touchpad click: activate the highlighted button
- `Enter` or keypad `5`: confirm the selected button
- `Esc`: cancel and return

## Encrypted Videos

Locked movies ask for their password before loading. Type with the keyboard;
hold `Shift` for uppercase and use `Tab` for symbols. `Enter` or a touchpad click presses the hovered
control, or the outlined default (Unlock unless you select a symbol with the arrow keys).
`Return` always presses Unlock. Playback shortcuts stay disabled while entering a password.
`Del` deletes and `Esc` cancels. Unlocking takes a while on the calculator;
the progress indicator stays responsive. The key is cleared when you leave the video.
Screenshots are disabled while an encrypted video is visible.

Install `cryptography` with `python -m pip install cryptography`, then add
`--encrypt` to your usual encoder command. It prompts for a password of up to
128 printable ASCII characters. For scripts, use `--password-file PATH`.

To encrypt an existing movie without re-encoding:

```sh
python tools/nve_crypto.py movie.nvp.tns locked.nvp.tns
```

Video, subtitles and the internal index are encrypted in independently verified
16 KiB records, so seeking still works. File size grows by about 0.2%.
The filename, duration and codec stay visible; original files and any requested
preview are kept unencrypted. New encrypted files include an authenticated
codec label and require the updated player. Older encrypted files still play;
their codec becomes visible after opening them in the current library session.

## Idle Power Management

In the picker, resume prompt or paused playback, the screen dims after one minute, turns off after two, and enters standby after three. Input wakes the screen; active video playback keeps it awake. During music, idle dim/off still runs while audio continues without CPU standby. On the verified CX II-T firmware, standby resumes directly in the player. Other firmware uses the normal OS suspend path.

## Subtitle Fonts

The built-in subtitle font cycle currently includes:

- `Tinytype`
- `VGA`
- `Thin` (default)
- `Space`
- `Fantasy`
