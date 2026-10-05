# Lyra 3.0.0 for Windows

Lyra is a compact music programming language for notes, tracks, chords, loops, drums, and native WAV/MIDI output.

## Quick start

Open a new terminal after installation and run:

```text
lyra --version
lyra init main.lyra
lyra run main.lyra song.wav
```

Other useful commands:

```text
lyra check main.lyra
lyra test .
lyra -f midi main.lyra song.mid
lyra -f json main.lyra events.json
```

## Installed folders

- `stdlib` contains the open Lyra standard library.
- `examples` contains ready-to-run compositions.
- `docs` contains the English language and architecture documentation.
- `source` is available when the optional C++ source component is selected.

Project website: https://lyralang.vercel.app/

Source repository: https://github.com/tomokuroki/lyra
