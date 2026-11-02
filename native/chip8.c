// CHIP-8 interpreter (C/SDL2).
// Build: gcc -O2 chip8.c -o chip8 $(pkg-config --cflags --libs sdl2)
// Run:   ./chip8 rom.ch8 [instructions_per_frame]
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 64
#define H 32
#define SCALE 12

static uint8_t mem[4096], V[16], disp[W * H], keys[16], dt, st, sp;
static uint16_t I, pc = 0x200, stack[16];

static const uint8_t font[80] = {
    0xF0,0x90,0x90,0x90,0xF0, 0x20,0x60,0x20,0x20,0x70, 0xF0,0x10,0xF0,0x80,0xF0,
    0xF0,0x10,0xF0,0x10,0xF0, 0x90,0x90,0xF0,0x10,0x10, 0xF0,0x80,0xF0,0x10,0xF0,
    0xF0,0x80,0xF0,0x90,0xF0, 0xF0,0x10,0x20,0x40,0x40, 0xF0,0x90,0xF0,0x90,0xF0,
    0xF0,0x90,0xF0,0x10,0xF0, 0xF0,0x90,0xF0,0x90,0x90, 0xE0,0x90,0xE0,0x90,0xE0,
    0xF0,0x80,0x80,0x80,0xF0, 0xE0,0x90,0x90,0x90,0xE0, 0xF0,0x80,0xF0,0x80,0xF0,
    0xF0,0x80,0xF0,0x80,0x80,
};

// COSMAC VIP keypad layout mapped onto 1234/QWER/ASDF/ZXCV
static const SDL_Scancode keymap[16] = {
    SDL_SCANCODE_X, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3,
    SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_A,
    SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_Z, SDL_SCANCODE_C,
    SDL_SCANCODE_4, SDL_SCANCODE_R, SDL_SCANCODE_F, SDL_SCANCODE_V,
};

static int draw_flag;

static void step(void)
{
    uint16_t op = mem[pc] << 8 | mem[pc + 1];
    pc += 2;

    uint8_t x = (op >> 8) & 0xF, y = (op >> 4) & 0xF, n = op & 0xF, nn = op & 0xFF;
    uint16_t nnn = op & 0xFFF;

    switch (op >> 12) {
    case 0x0:
        if (op == 0x00E0) { memset(disp, 0, sizeof disp); draw_flag = 1; }
        else if (op == 0x00EE) pc = stack[--sp & 15];
        break;
    case 0x1: pc = nnn; break;
    case 0x2: stack[sp++ & 15] = pc; pc = nnn; break;
    case 0x3: if (V[x] == nn) pc += 2; break;
    case 0x4: if (V[x] != nn) pc += 2; break;
    case 0x5: if (V[x] == V[y]) pc += 2; break;
    case 0x6: V[x] = nn; break;
    case 0x7: V[x] += nn; break;
    case 0x8: {
        uint8_t a = V[x], b = V[y], f = 0;
        switch (n) {
        case 0x0: V[x] = b; return;
        case 0x1: V[x] = a | b; V[0xF] = 0; return;  // vF reset quirk (original VIP)
        case 0x2: V[x] = a & b; V[0xF] = 0; return;
        case 0x3: V[x] = a ^ b; V[0xF] = 0; return;
        case 0x4: V[x] = a + b; f = (a + b) > 255; break;
        case 0x5: V[x] = a - b; f = a >= b; break;
        case 0x6: V[x] = b >> 1; f = b & 1; break;     // shift uses VY (original)
        case 0x7: V[x] = b - a; f = b >= a; break;
        case 0xE: V[x] = b << 1; f = b >> 7; break;
        default: return;
        }
        V[0xF] = f;  // flag set last, so VF-as-operand works
        break;
    }
    case 0x9: if (V[x] != V[y]) pc += 2; break;
    case 0xA: I = nnn; break;
    case 0xB: pc = nnn + V[0]; break;
    case 0xC: V[x] = (rand() & 0xFF) & nn; break;
    case 0xD: {
        uint8_t px = V[x] % W, py = V[y] % H;
        V[0xF] = 0;
        for (int row = 0; row < n && py + row < H; row++) {
            uint8_t bits = mem[(I + row) & 0xFFF];
            for (int col = 0; col < 8 && px + col < W; col++) {
                if (!(bits & (0x80 >> col))) continue;
                uint8_t *p = &disp[(py + row) * W + px + col];
                if (*p) V[0xF] = 1;
                *p ^= 1;
            }
        }
        draw_flag = 1;
        break;
    }
    case 0xE:
        if (nn == 0x9E && keys[V[x] & 15]) pc += 2;
        else if (nn == 0xA1 && !keys[V[x] & 15]) pc += 2;
        break;
    case 0xF:
        switch (nn) {
        case 0x07: V[x] = dt; break;
        case 0x0A: {
            // wait for a key to be pressed *and released*
            static int held = -1;
            if (held >= 0 && !keys[held]) { V[x] = held; held = -1; break; }
            if (held < 0)
                for (int k = 0; k < 16; k++) if (keys[k]) { held = k; break; }
            pc -= 2;
            break;
        }
        case 0x15: dt = V[x]; break;
        case 0x18: st = V[x]; break;
        case 0x1E: I += V[x]; break;
        case 0x29: I = (V[x] & 0xF) * 5; break;
        case 0x33:
            mem[I & 0xFFF] = V[x] / 100;
            mem[(I + 1) & 0xFFF] = V[x] / 10 % 10;
            mem[(I + 2) & 0xFFF] = V[x] % 10;
            break;
        case 0x55: for (int i = 0; i <= x; i++) mem[I++ & 0xFFF] = V[i]; break;
        case 0x65: for (int i = 0; i <= x; i++) V[i] = mem[I++ & 0xFFF]; break;
        }
        break;
    }
}

static void beep_cb(void *ud, uint8_t *out, int len)
{
    static uint32_t phase;
    (void)ud;
    for (int i = 0; i < len; i++)
        out[i] = st ? (((phase++ / 50) & 1) ? 160 : 96) : 128;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s rom.ch8 [ipf]\n", argv[0]);
        return 1;
    }
    int ipf = argc > 2 ? atoi(argv[2]) : 11;

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    size_t got = fread(mem + 0x200, 1, sizeof mem - 0x200, f);
    fclose(f);
    printf("loaded %zu bytes\n", got);

    memcpy(mem, font, sizeof font);
    srand(time(NULL));

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    SDL_Window *win = SDL_CreateWindow("chip8", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       W * SCALE, H * SCALE, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, W, H);

    SDL_AudioSpec want = { .freq = 22050, .format = AUDIO_U8, .channels = 1,
                           .samples = 512, .callback = beep_cb };
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (audio) SDL_PauseAudioDevice(audio, 0);

    uint32_t pixels[W * H];
    for (;;) {
        uint32_t frame_start = SDL_GetTicks();
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_QUIT ||
                (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                goto quit;

        const uint8_t *ks = SDL_GetKeyboardState(NULL);
        for (int k = 0; k < 16; k++) keys[k] = ks[keymap[k]];

        // display wait quirk: original VIP only draws one sprite per frame
        draw_flag = 0;
        for (int i = 0; i < ipf; i++) {
            step();
            if (draw_flag) break;
        }

        if (dt) dt--;
        if (st) st--;

        for (int i = 0; i < W * H; i++) pixels[i] = disp[i] ? 0xFFE8E8E8 : 0xFF101010;
        SDL_UpdateTexture(tex, NULL, pixels, W * 4);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        // vsync usually handles 60Hz, but don't rely on it
        uint32_t spent = SDL_GetTicks() - frame_start;
        if (spent < 16) SDL_Delay(16 - spent);
    }
quit:
    if (audio) SDL_CloseAudioDevice(audio);
    SDL_Quit();
    return 0;
}
