// CHIP-8 interpreter core. Targets original COSMAC VIP behaviour.
'use strict';

const W = 64, H = 32;

const FONT = [
  0xF0,0x90,0x90,0x90,0xF0, 0x20,0x60,0x20,0x20,0x70, 0xF0,0x10,0xF0,0x80,0xF0,
  0xF0,0x10,0xF0,0x10,0xF0, 0x90,0x90,0xF0,0x10,0x10, 0xF0,0x80,0xF0,0x10,0xF0,
  0xF0,0x80,0xF0,0x90,0xF0, 0xF0,0x10,0x20,0x40,0x40, 0xF0,0x90,0xF0,0x90,0xF0,
  0xF0,0x90,0xF0,0x10,0xF0, 0xF0,0x90,0xF0,0x90,0x90, 0xE0,0x90,0xE0,0x90,0xE0,
  0xF0,0x80,0x80,0x80,0xF0, 0xE0,0x90,0x90,0x90,0xE0, 0xF0,0x80,0xF0,0x80,0xF0,
  0xF0,0x80,0xF0,0x80,0x80,
];

class Chip8 {
  constructor() { this.reset(); }

  reset(rom) {
    this.mem = new Uint8Array(4096);
    this.V = new Uint8Array(16);
    this.disp = new Uint8Array(W * H);
    this.keys = new Uint8Array(16);
    this.stack = new Uint16Array(16);
    this.I = 0; this.pc = 0x200; this.sp = 0;
    this.dt = 0; this.st = 0;
    this.held = -1;
    this.drawFlag = false;
    this.mem.set(FONT, 0);
    if (rom) this.mem.set(rom.subarray(0, 4096 - 0x200), 0x200);
  }

  step() {
    const m = this.mem, V = this.V;
    const op = (m[this.pc] << 8) | m[this.pc + 1];
    this.pc = (this.pc + 2) & 0xFFF;

    const x = (op >> 8) & 0xF, y = (op >> 4) & 0xF, n = op & 0xF, nn = op & 0xFF, nnn = op & 0xFFF;

    switch (op >> 12) {
      case 0x0:
        if (op === 0x00E0) { this.disp.fill(0); this.drawFlag = true; }
        else if (op === 0x00EE) { this.sp = (this.sp - 1) & 15; this.pc = this.stack[this.sp]; }
        break;
      case 0x1: this.pc = nnn; break;
      case 0x2: this.stack[this.sp] = this.pc; this.sp = (this.sp + 1) & 15; this.pc = nnn; break;
      case 0x3: if (V[x] === nn) this.pc += 2; break;
      case 0x4: if (V[x] !== nn) this.pc += 2; break;
      case 0x5: if (V[x] === V[y]) this.pc += 2; break;
      case 0x6: V[x] = nn; break;
      case 0x7: V[x] += nn; break;
      case 0x8: {
        const a = V[x], b = V[y];
        let f;
        switch (n) {
          case 0x0: V[x] = b; return;
          case 0x1: V[x] = a | b; V[0xF] = 0; return; // vF reset quirk
          case 0x2: V[x] = a & b; V[0xF] = 0; return;
          case 0x3: V[x] = a ^ b; V[0xF] = 0; return;
          case 0x4: V[x] = a + b; f = (a + b) > 255 ? 1 : 0; break;
          case 0x5: V[x] = a - b; f = a >= b ? 1 : 0; break;
          case 0x6: V[x] = b >> 1; f = b & 1; break;   // shift uses VY
          case 0x7: V[x] = b - a; f = b >= a ? 1 : 0; break;
          case 0xE: V[x] = b << 1; f = b >> 7; break;
          default: return;
        }
        V[0xF] = f; // flag written last so VF-as-operand works
        break;
      }
      case 0x9: if (V[x] !== V[y]) this.pc += 2; break;
      case 0xA: this.I = nnn; break;
      case 0xB: this.pc = (nnn + V[0]) & 0xFFF; break;
      case 0xC: V[x] = (Math.random() * 256) & nn; break;
      case 0xD: {
        const px = V[x] % W, py = V[y] % H;
        V[0xF] = 0;
        for (let row = 0; row < n && py + row < H; row++) {
          const bits = m[(this.I + row) & 0xFFF];
          for (let col = 0; col < 8 && px + col < W; col++) {
            if (!(bits & (0x80 >> col))) continue;
            const i = (py + row) * W + px + col;
            if (this.disp[i]) V[0xF] = 1;
            this.disp[i] ^= 1;
          }
        }
        this.drawFlag = true;
        break;
      }
      case 0xE:
        if (nn === 0x9E && this.keys[V[x] & 15]) this.pc += 2;
        else if (nn === 0xA1 && !this.keys[V[x] & 15]) this.pc += 2;
        break;
      case 0xF:
        switch (nn) {
          case 0x07: V[x] = this.dt; break;
          case 0x0A: // wait for key press *and release*
            if (this.held >= 0 && !this.keys[this.held]) { V[x] = this.held; this.held = -1; break; }
            if (this.held < 0) for (let k = 0; k < 16; k++) if (this.keys[k]) { this.held = k; break; }
            this.pc -= 2;
            break;
          case 0x15: this.dt = V[x]; break;
          case 0x18: this.st = V[x]; break;
          case 0x1E: this.I = (this.I + V[x]) & 0xFFFF; break;
          case 0x29: this.I = (V[x] & 0xF) * 5; break;
          case 0x33:
            m[this.I & 0xFFF] = (V[x] / 100) | 0;
            m[(this.I + 1) & 0xFFF] = ((V[x] / 10) | 0) % 10;
            m[(this.I + 2) & 0xFFF] = V[x] % 10;
            break;
          case 0x55: for (let i = 0; i <= x; i++) m[this.I++ & 0xFFF] = V[i]; break;
          case 0x65: for (let i = 0; i <= x; i++) V[i] = m[this.I++ & 0xFFF]; break;
        }
        break;
    }
  }

  // One 60 Hz frame. Display-wait quirk: stop after the first sprite draw.
  frame(ipf) {
    this.drawFlag = false;
    for (let i = 0; i < ipf; i++) {
      this.step();
      if (this.drawFlag) break;
    }
    if (this.dt) this.dt--;
    if (this.st) this.st--;
  }
}
