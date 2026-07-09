# LCD overlays fork

This is a fork of [DOSBox Staging](https://www.dosbox-staging.org/) that adds
on-screen LCD status panels for MIDI synth emulators, composited directly
into the game canvas — no separate window, no window manager fiddling. If
you've ever wanted to see the classic Roland Sound Canvas LCD (patch names,
per-channel level/pan/reverb/chorus, the little dot-matrix bargraph) glowing
in the corner of your screen while you play, this is for you.

Currently implemented:

- **Roland Sound Canvas (SC-55 / SC-55mk2)** — via
  [senjinthedragon/Nuked-SC55-CLAP](https://github.com/senjinthedragon/Nuked-SC55-CLAP)
  (`lcd-framebuffer` branch), a fork of
  [johnnovak/Nuked-SC55-CLAP](https://github.com/johnnovak/Nuked-SC55-CLAP)
  adding a custom CLAP extension that exposes the emulated LCD as a raw pixel
  buffer.
- **Roland MT-32 / CM-32L** — munt (dosbox-staging's built-in `mididevice =
  mt32` backend, not a CLAP plugin) already emulates the full 20-character
  LCD content, including the per-channel partial-usage bargraph. Rasterized
  each frame into a small dot-matrix bitmap using a hand-authored font and
  rendered through the same overlay machinery as Sound Canvas.

## Setup

### Sound Canvas

1. Build [senjinthedragon/Nuked-SC55-CLAP](https://github.com/senjinthedragon/Nuked-SC55-CLAP)
   (`lcd-framebuffer` branch) and copy the resulting `.clap` into your DOSBox
   config directory's `plugins/` folder (e.g. `~/.config/dosbox/plugins/` on
   Linux).
2. Place SC-55 ROMs in `soundcanvas-roms/` per the [upstream Sound Canvas
   docs](website/docs/0.83/manual/sound/sound-devices/sound-canvas.md).
3. In your DOSBox config:

   ```ini
   [midi]
   mididevice = soundcanvas

   [soundcanvas]
   soundcanvas_model = sc55mk2
   soundcanvas_lcd_overlay = on
   soundcanvas_lcd_overlay_opacity = 85
   ```

### MT-32 / CM-32L

1. Place MT-32/CM-32L ROMs in `mt32-roms/` per the [upstream MT-32
   docs](website/docs/0.83/manual/sound/sound-devices/) (ROMs are identified
   by checksum, not filename).
2. In your DOSBox config:

   ```ini
   [midi]
   mididevice = mt32

   [mt32]
   mt32_lcd_overlay = on
   mt32_lcd_overlay_opacity = 85
   ```

No CLAP plugin needed — munt is built directly into dosbox-staging.

Currently only supported with the OpenGL/shader render backend
(`output = opengl` or similar) — `SdlRenderer` has no equivalent per-frame
hook yet.

## How it works

`Nuked-SC55-CLAP` is intentionally headless — no window, no video
dependency at all, since CLAP's audio-plugin ABI has no video/GUI concept
built in. Rather than spawn a second OS window (real risk: creating an
SDL2(-compat) window from a background thread inside a process that already
owns its own SDL3 main loop and window-system connection), the LCD panel's
pixel data is pulled out via a custom CLAP extension
(`net.johnnovak.nuked_sc55_clap.lcd_framebuffer/1`) and composited straight
into DOSBox's own OpenGL render output, right after the shader pipeline's
final pass — see `src/gui/render/lcd_overlay.{h,cpp}` and the call site in
`OpenGlRenderer::PresentFrame()`.

MT-32 is simpler: munt runs directly inside dosbox-staging (no plugin
boundary to cross), and `MT32Emu::Service::getDisplayState()` already
returns the current 20-character LCD text each frame — including two
reserved non-ASCII bytes real hardware uses for its per-channel bargraph
(`0x01` = "part active" solid block, `0x02` = a duplicate pipe glyph). This
gets rasterized into a small RGBA bitmap using a hand-authored 5x7
dot-matrix font (`src/gui/render/private/mt32_lcd_font.h` — original
letterforms, not derived from munt's own LCD4Linux-based reference font, to
avoid that attribution) and fed through the same `LcdOverlay` class used for
Sound Canvas, just with nearest-neighbour texture filtering instead of
bilinear so the low-resolution text stays crisp rather than blurring. See
`src/gui/render/mt32_lcd_rasterizer.{h,cpp}` and `MidiDeviceMt32::
GetDisplayState()` in `src/midi/mt32.cpp`.

## Building

Same as upstream — see the main [README](README.md) and
[build-linux.md](docs/build-linux.md)/[build-windows.md](docs/build-windows.md)/[build-macos.md](docs/build-macos.md).
No new build dependencies; the overlay reuses the existing OpenGL/shader
infrastructure.

## Status

This is a hobby fork, not an official DOSBox Staging feature. If you hit
issues, open one on [this fork's
issue tracker](https://github.com/senjinthedragon/dosbox-staging/issues)
rather than upstream's.
