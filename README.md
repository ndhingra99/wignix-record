# wignix-record

Record a part of the screen, with the system's audio, on WigNix.

A small Qt 5 program. It drives `slop` to pick a region, `ffmpeg` to
capture the screen and the PulseAudio monitor, and stops on the same key.
Bound to Shift+Print in the WigNix desktop.

## Files

| file | what |
| --- | --- |
| `wignix-record.pro` | the qmake project |
| `main.cpp` | the entry point |
| `recorder.cpp`, `recorder.h` | the recorder: region, capture, stop |

## Build

```
qmake
make
```

The xbps-src template that packages it is `srcpkgs/wignix-record` in
`wignix-packages`.

## License

MIT.
