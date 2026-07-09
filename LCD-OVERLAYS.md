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

Planned:

- **Roland MT-32 / CM-32L** — munt already emulates the MT-32's full LCD
  content (patch name plus the per-channel partial-usage bargraph, via
  `Synth::getDisplayState()`), but this fork doesn't render it yet. Text-mode
  rather than pixel-buffer, so a different (smaller) implementation than the
  Sound Canvas one — see the "MT-32" section below for the current state.

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
