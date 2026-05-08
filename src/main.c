#include <SDL3/SDL_events.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#define AIDS_IMPLEMENTATION
#include <aids.h>
#include <cpu.h>
#include <pallete.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <util.h>

#include <SDL3/SDL.h>

// TASK(20260505-161630-763-n6-804): clean this file

#define SCALE 4

#define SWIDTH (WIDTH * SCALE)
#define SHEIGHT (HEIGHT * SCALE)

void split_data(ByteSlice* buf, Cartridge* cart) {
    size_t prg_size = cart->info.prg_banks * 16 * 1024;
    size_t chr_size = cart->info.chr_banks * 8 * 1024;
    u8* base = buf->buf + 16;

    cart->prg_rom.buf = base;
    cart->prg_rom.size = prg_size;

    cart->chr_rom.buf = base + prg_size;
    cart->chr_rom.size = chr_size;
}

Result decode_nes_rom(ByteSlice* buf, CartridgeInfo* out) {
    if (buf->size < 16 || buf->buf[0] != 'N' || buf->buf[1] != 'E' ||
        buf->buf[2] != 'S' || buf->buf[3] != 0x1A) {
        return Err("not a valid iNES ROM");
    }
    u8* header = buf->buf;
    out->prg_banks = header[4];
    out->chr_banks = header[5];
    out->mapper = (header[6] >> 4) | (header[7] & 0xF0);
    return Ok();
}

CPU create_cpu(Cartridge* cart) {
    CPU cpu = {0};
    cpu.cart = cart;
    cpu.S = 0xFD;
    cpu.P = 0x34;
    cpu.PC = cpu_read(&cpu, NULL, 0xFFFC) | (cpu_read(&cpu, NULL, 0xFFFD) << 8);
    return cpu;
}

void render_frame(SDL_Renderer* r, SDL_Texture* tex, uint32_t* fb) {
    void* pixels;
    int pitch;

    SDL_LockTexture(tex, NULL, &pixels, &pitch);
    memcpy(pixels, fb, WIDTH * HEIGHT * sizeof(uint32_t));
    SDL_UnlockTexture(tex);

    SDL_RenderClear(r);
    SDL_RenderTexture(r, tex, NULL, NULL);
}

PPU create_ppu(CPU* cpu) {
    PPU ppu = {0};
    ppu.chr_rom = &cpu->cart->chr_rom;
    for (int i = 0; i < 32; i++) {
        ppu.palette[i] = i;
    }
    return ppu;
}

uint32_t ppu_get_pixel(PPU* ppu, int x, int y) {
    // 1. tile coordinates
    int tile_x = x / 8;
    int tile_y = y / 8;

    // 2. pixel inside tile
    int px = x % 8;
    int py = y % 8;

    // 3. nametable lookup
    int nametable_index = tile_y * 32 + tile_x;
    u8 tile_id = ppu_vram_read(ppu, 0x2000 + nametable_index);

    // 4. CHR fetch
    u8* chr = ppu->chr_rom->buf;
    u16 tile_addr = tile_id * 16;

    u8 plane0 = chr[tile_addr + py];
    u8 plane1 = chr[tile_addr + py + 8];

    int bit = 7 - px;
    u8 low = (plane0 >> bit) & 1;
    u8 high = (plane1 >> bit) & 1;
    u8 color_index = (high << 1) | low;

    // =========================
    // 5. ATTRIBUTE TABLE FIX
    // =========================

    int attr_x = tile_x / 4;
    int attr_y = tile_y / 4;

    int attr_index = attr_y * 8 + attr_x;
    u8 attr = ppu_vram_read(ppu, 0x23C0 + attr_index);

    int sub_x = (tile_x % 4) / 2;
    int sub_y = (tile_y % 4) / 2;

    int shift = (sub_y * 2 + sub_x) * 2;
    u8 palette_index = (attr >> shift) & 0x03;

    // =========================
    // 6. PALETTE LOOKUP
    // =========================
    if (color_index == 0) {
        return 0xFF000000 | nes_palette[ppu->palette[0]];
    }
    u8 nes_color_index = ppu->palette[palette_index * 4 + color_index];

    return 0xFF000000 | nes_palette[nes_color_index & 0x3F];
}

void ppu_render(PPU* ppu) {
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            ppu->fb[y * WIDTH + x] = ppu_get_pixel(ppu, x, y);
        }
    }
}

bool a = false;

void ppu_step(PPU* ppu, CPU* cpu) {
    ppu->cycle++;

    if (ppu->cycle >= 341) {
        ppu->cycle = 0;
        ppu->scanline++;

        if (ppu->scanline >= 262) {
            ppu->scanline = 0;
        }
    }
    if (ppu->scanline == 241 && ppu->cycle == 1) {
        ppu->status |= 0x80;
        if (ppu->control & 0x80)
            cpu->nmi_pending = true;
        ppu_render(ppu);
    }

    if (ppu->scanline == 261 && ppu->cycle == 1) {
        ppu->status &= ~0x80;
    }
}

#define FRAME_CYCLES 29780

int main() {
    ByteSlice data;
    Result res =
        load_file("roms/Super Mario Bros.nes", &data, &DEFAULT_ALLOCATOR);
    if (is_err(res)) {
        free_slice(&data, &DEFAULT_ALLOCATOR);
        panic("ERROR: " RESULT, FMTRESULT(res));
    }

    Cartridge cart;
    res = decode_nes_rom(&data, &cart.info);
    if (is_err(res)) {
        free_slice(&data, &DEFAULT_ALLOCATOR);
        panic("ERROR: " RESULT, FMTRESULT(res));
    }

    split_data(&data, &cart);

    CPU cpu = create_cpu(&cart);
    PPU ppu = create_ppu(&cpu);

    SDL_Window* win;
    SDL_Renderer* ren;
    SDL_CreateWindowAndRenderer("NES Emulator", SWIDTH, SHEIGHT, 0, &win, &ren);

    SDL_Texture* tex =
        SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);

    bool running = true;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            }
        }

        for (int _ = 0; _ < FRAME_CYCLES; _++) {
            int cycles = cpu_step(&cpu, &ppu);
            for (int i = 0; i < cycles * 3; i++) {
                ppu_step(&ppu, &cpu);
            }
        }

        SDL_RenderClear(ren);
        render_frame(ren, tex, ppu.fb);
        SDL_RenderPresent(ren);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);

    free_slice(&data, &DEFAULT_ALLOCATOR);
    return 0;
}
