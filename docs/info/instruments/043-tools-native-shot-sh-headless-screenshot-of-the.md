---
id: I043
kind: instrument
status: trusted
created: 2026-08-07
---

## Instrument

tools/native_shot.sh -- headless screenshot of the native build

## Validated by

Run against both classes. NEGATIVE first, and it was a real one: the first version photographed a 100%-black Xvfb root because SDL3 prefers Wayland and had put the window on the user's real desktop, ignoring DISPLAY -- the script now forces SDL_VIDEODRIVER=x11 with WAYLAND_DISPLAY cleared, and x2native prints which video driver it actually got. POSITIVE: the same script then captured the game's save/load panel, 1574 distinct colours. It reports the colour histogram of every capture and says in words when the image is one flat colour, which is what 'nothing was drawn' and 'you photographed the wrong display' both look like.

## Known failure modes

(none recorded yet)
