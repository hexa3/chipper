# chipper

A CHIP-8 interpreter that runs in the browser.

**Live demo:** https://hexa3.github.io/chipper/

## Features

- The full base CHIP-8 instruction set (35 opcodes)
- Original COSMAC VIP quirks: VF reset, shifts that use VY, display wait, and FX55/FX65 that increment I
- A fixed 60 Hz timer with a square-wave beep
- Keyboard input (`1234 / QWER / ASDF / ZXCV`) and an on-screen keypad for touch screens
- Built-in test ROMs, plus loading your own `.ch8` file
- Adjustable instructions per frame, pause and reset

It passes the IBM logo, Corax+ and Flags tests from
[Timendus/chip8-test-suite](https://github.com/Timendus/chip8-test-suite).

## Files

| Path | What it is |
|---|---|
| `index.html` | Web UI: canvas, controls, keypad and sound |
| `chip8.js` | Interpreter core |
| `native/chip8.c` | The original C/SDL2 version |
| `roms/` | Test ROMs from Timendus/chip8-test-suite (GPL-3.0) |

## Running locally

```sh
python3 -m http.server   # then open http://localhost:8000
```

Native version:

```sh
gcc -O2 native/chip8.c -o chip8 $(pkg-config --cflags --libs sdl2)
./chip8 roms/2-ibm-logo.ch8
```
