#define NO_DEBUG
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <aids.h>
#include <pallete.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <types.h>

#include <SDL3/SDL.h>

#define SCALE 3

#define WIDTH 256
#define HEIGHT 240

#define SWIDTH (WIDTH * SCALE)
#define SHEIGHT (HEIGHT * SCALE)

typedef struct CartridgeInfo CartridgeInfo;
struct CartridgeInfo {
    uint8_t prg_banks;
    uint8_t chr_banks;
    uint8_t mapper;
};

typedef struct Cartridge Cartridge;
struct Cartridge {
    ByteSlice prg_rom;
    ByteSlice chr_rom;
    CartridgeInfo info;
};

void split_data(ByteSlice* buf, Cartridge* cart) {
    size_t prg_size = cart->info.prg_banks * 16 * 1024;
    size_t chr_size = cart->info.chr_banks * 8 * 1024;
    uint8_t* base = buf->buf + 16;

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
    uint8_t* header = buf->buf;
    out->prg_banks = header[4];
    out->chr_banks = header[5];
    out->mapper = (header[6] >> 4) | (header[7] & 0xF0);
    return Ok();
}

typedef struct CPU CPU;
struct CPU {
    uint8_t A;   //  accumulator
    uint8_t P;   // status (flags)
    uint16_t PC; // program counter
    uint8_t S;   // stack pointer
    uint8_t X;   // X Register
    uint8_t Y;   // Y Register
    uint8_t cpu_ram[2048];
    Cartridge* cart; // rom and such
};

typedef struct PPU PPU;
struct PPU {
    uint8_t vram[2048];

    uint8_t palette[32];

    const ByteSlice* chr_rom;

    uint8_t nametable_mirror;

    int scanline;
    int cycle;
    uint32_t fb[WIDTH * HEIGHT];
    uint8_t control;
    uint8_t mask;
    uint8_t status;

    uint16_t vram_addr;
    uint16_t temp_addr;
    uint8_t fine_x;
    uint8_t open_bus;
    uint8_t latch;
};

uint8_t ppu_vram_read(PPU* ppu, uint16_t addr) {
    addr &= 0x3FFF;

    if (addr < 0x2000) {
        return ppu->chr_rom->buf[addr];
    }

    if (addr < 0x3F00) {
        uint16_t vram_addr = addr - 0x2000;

        if (vram_addr & 0x0800) {
            vram_addr = (vram_addr & 0x03FF) | 0x0400;
        } else {
            vram_addr &= 0x03FF;
        }

        return ppu->vram[vram_addr];
    }

    if (addr < 0x4000) {
        uint16_t index = (addr - 0x3F00) % 32;

        if (index == 0x10)
            index = 0x00;
        if (index == 0x14)
            index = 0x04;
        if (index == 0x18)
            index = 0x08;
        if (index == 0x1C)
            index = 0x0C;

        return ppu->palette[index];
    }

    return 0;
}
void ppu_vram_write(PPU* ppu, uint16_t addr, uint8_t val) {
    addr &= 0x3FFF;

    if (addr < 0x2000) {
        return;
    }
    if (addr < 0x3F00) {
        uint16_t vram_addr = addr - 0x2000;

        if (vram_addr & 0x0800) {
            vram_addr = (vram_addr & 0x03FF) | 0x0400;
        } else {
            vram_addr &= 0x03FF;
        }

        ppu->vram[vram_addr] = val;
        return;
    }

    if (addr < 0x4000) {
        ppu->palette[(addr - 0x3F00) % 32] = val;
        return;
    }
}

uint8_t cpu_read(CPU* cpu, PPU* ppu, uint16_t addr) {
    // internal ram
    if (addr < 0x2000) {
        return cpu->cpu_ram[addr & 0x07FF];
    }
    // cartridge rom
    if (addr >= 0x8000) {
        size_t offset = addr - 0x8000;

        // Mapper 0: 16KB mirroring
        if (cpu->cart->info.prg_banks == 1) {
            offset &= 0x3FFF;
        }

        return cpu->cart->prg_rom.buf[offset];
    }
    if (addr >= 0x2000 && addr < 0x4000) {
        uint16_t reg = 0x2000 + (addr % 8);

        if (reg == 0x2002) {
            uint8_t val = ppu->status;
            ppu->status &= ~0x80; // clear vblank
            ppu->latch = 0;       // reset latch
            return val;
        }

        return 0;
    }
    // TASK(20260502-134138-467-n6-189): handle other addresses in read
    return 0;
}

void cpu_write(CPU* cpu, PPU* ppu, uint16_t addr, uint8_t value) {
    // internal ram
    if (addr < 0x2000) {
        cpu->cpu_ram[addr & 0x07FF] = value;
    }
    if (addr >= 0x2000 && addr < 0x4000) {
        addr = 0x2000 + (addr % 8);
    }
    // ROM - ignores writes
    if (addr >= 0x8000) {
        return;
    }
    if (addr == 0x2006) {
        if (!ppu->latch) {
            ppu->temp_addr = (value << 8);
            ppu->latch = 1;
        } else {
            ppu->temp_addr |= value;
            ppu->vram_addr = ppu->temp_addr;
            ppu->latch = 0;
        }
        return;
    }

    if (addr == 0x2007) {
        debug("PPU WRITE: %04X <- %02X\n", ppu->vram_addr, value);
        ppu_vram_write(ppu, ppu->vram_addr, value);
        if (ppu->control & 0x04)
            ppu->vram_addr += 32;
        else
            ppu->vram_addr += 1;
        return;
    }
    if (addr == 0x2000) {
        ppu->control = value;
        return;
    }
    if (addr == 0x2005) {
        ppu->latch ^= 1;
        return;
    }
    // TASK(20260502-141946-173-n6-290): handle other addresses for writes
}

CPU create_cpu(Cartridge* cart) {
    CPU cpu = {0};
    cpu.cart = cart;
    cpu.S = 0xFD;
    cpu.P = 0x34;
    cpu.PC = cpu_read(&cpu, NULL, 0xFFFC) | (cpu_read(&cpu, NULL, 0xFFFD) << 8);
    return cpu;
}

typedef enum Flag Flag;
enum Flag {
    FLAG_C, // Carry
    FLAG_Z, // Zero
    FLAG_I, // Interrupt Disable
    FLAG_D, // Decimal
    FLAG_B, // Break
    FLAG_U, // Unused (always 1 in real 6502)
    FLAG_V, // Overflow
    FLAG_N, // Negative
};

static inline bool get_flag(CPU* cpu, Flag f) { return (cpu->P >> f) & 1; }

static inline void set_flag(CPU* cpu, Flag f, bool val) {
    if (val) {
        cpu->P |= (1 << f);
    } else {
        cpu->P &= ~(1 << f);
    }
}

void cpu_step(CPU* cpu, PPU* ppu) {
    uint8_t opcode = cpu_read(cpu, ppu, cpu->PC++);

    // TASK(20260502-211526-008-n6-146): reorganize the order of this
    switch (opcode) {
    case 0xA9: { // LDA immediate
        uint8_t value = cpu_read(cpu, ppu, cpu->PC++);
        cpu->A = value;
        set_flag(cpu, FLAG_Z, value == 0);
        set_flag(cpu, FLAG_N, value & 0x80);
        debug("%04X: LDA #$%02X\n", cpu->PC - 2, value);
        break;
    }
    case 0xAD: { // LDA absolute
        uint8_t lo = cpu_read(cpu, ppu, cpu->PC++);
        uint8_t hi = cpu_read(cpu, ppu, cpu->PC++);
        uint16_t addr = lo | (hi << 8);
        cpu->A = cpu_read(cpu, ppu, addr);
        set_flag(cpu, FLAG_Z, cpu->A == 0);
        set_flag(cpu, FLAG_N, cpu->A & 0x80);
        debug("%04X: LDA $%04X => %02X\n", cpu->PC - 3, addr, cpu->A);
        break;
    }
    case 0xA5: { // LDA zero page
        uint8_t addr = cpu_read(cpu, ppu, cpu->PC++);
        cpu->A = cpu_read(cpu, ppu, addr);
        set_flag(cpu, FLAG_Z, cpu->A == 0);
        set_flag(cpu, FLAG_N, cpu->A & 0x80);
        debug("%04X: LDA $%02X => %02X\n", cpu->PC - 2, addr, cpu->A);
        break;
    }
    case 0xBD: { // LDA absolute,X
        uint8_t lo = cpu_read(cpu, ppu, cpu->PC++);
        uint8_t hi = cpu_read(cpu, ppu, cpu->PC++);
        uint16_t base = lo | (hi << 8);

        uint16_t addr = base + cpu->X;

        cpu->A = cpu_read(cpu, ppu, addr);

        set_flag(cpu, FLAG_Z, cpu->A == 0);
        set_flag(cpu, FLAG_N, cpu->A & 0x80);

        debug("%04X: LDA $%04X,X => %02X\n", cpu->PC - 3, base, cpu->A);
        break;
    }

    case 0x8D: { // STA absolute
        uint8_t lo = cpu_read(cpu, ppu, cpu->PC++);
        uint8_t hi = cpu_read(cpu, ppu, cpu->PC++);
        uint16_t addr = lo | (hi << 8);
        cpu_write(cpu, ppu, addr, cpu->A);
        debug("%04X: STA $%04X <= %02X\n", cpu->PC - 3, addr, cpu->A);
        break;
    }
    case 0x85: { // STA zero page
        uint8_t addr = cpu_read(cpu, ppu, cpu->PC++);
        cpu_write(cpu, ppu, addr, cpu->A);
        debug("%04X: STA $%02X <= %02X\n", cpu->PC - 2, addr, cpu->A);
        break;
    }

    case 0x48: { // PHA
        cpu_write(cpu, ppu, 0x0100 + cpu->S--, cpu->A);
        debug("%04X: PHA; (pushed $%02X)\n", cpu->PC - 1, cpu->A);
        break;
    }
    case 0x68: { // PLA
        cpu->A = cpu_read(cpu, ppu, 0x0100 + cpu->S++);
        debug("%04X: PLA; (pulled $%02X)\n", cpu->PC - 1, cpu->A);
        break;
    }

    case 0xA2: { // LDX immediate
        uint8_t value = cpu_read(cpu, ppu, cpu->PC++);
        cpu->X = value;
        set_flag(cpu, FLAG_Z, value == 0);
        set_flag(cpu, FLAG_N, value & 0x80);
        debug("%04X: LDX #$%02X\n", cpu->PC - 2, value);
        break;
    }
    case 0xAE: { // LDX absolute
        uint8_t lo = cpu_read(cpu, ppu, cpu->PC++);
        uint8_t hi = cpu_read(cpu, ppu, cpu->PC++);
        uint16_t addr = lo | (hi << 8);
        cpu->X = cpu_read(cpu, ppu, addr);
        set_flag(cpu, FLAG_Z, cpu->X == 0);
        set_flag(cpu, FLAG_N, cpu->X & 0x80);
        debug("%04X: LDX $%04X => %02X\n", cpu->PC - 3, addr, cpu->X);
        break;
    }
    case 0xA6: { // LDX zero page
        uint8_t addr = cpu_read(cpu, ppu, cpu->PC++);
        cpu->X = cpu_read(cpu, ppu, addr);
        set_flag(cpu, FLAG_Z, cpu->X == 0);
        set_flag(cpu, FLAG_N, cpu->X & 0x80);
        debug("%04X: LDX $%02X => %02X\n", cpu->PC - 2, addr, cpu->X);
        break;
    }

    case 0x8E: { // STX absolute
        uint8_t lo = cpu_read(cpu, ppu, cpu->PC++);
        uint8_t hi = cpu_read(cpu, ppu, cpu->PC++);
        uint16_t addr = lo | (hi << 8);
        cpu_write(cpu, ppu, addr, cpu->X);
        debug("%04X: STX $%04X <= %02X\n", cpu->PC - 3, addr, cpu->X);
        break;
    }

    case 0xA0: { // LDY immediate
        uint8_t value = cpu_read(cpu, ppu, cpu->PC++);
        cpu->Y = value;
        set_flag(cpu, FLAG_Z, value == 0);
        set_flag(cpu, FLAG_N, value & 0x80);
        debug("%04X: LDY #$%02X\n", cpu->PC - 2, value);
        break;
    }
    case 0xAC: { // LDY absolute
        uint8_t lo = cpu_read(cpu, ppu, cpu->PC++);
        uint8_t hi = cpu_read(cpu, ppu, cpu->PC++);
        uint16_t addr = lo | (hi << 8);
        cpu->Y = cpu_read(cpu, ppu, addr);
        set_flag(cpu, FLAG_Z, cpu->Y == 0);
        set_flag(cpu, FLAG_N, cpu->Y & 0x80);
        debug("%04X: LDY $%04X => %02X\n", cpu->PC - 3, addr, cpu->Y);
        break;
    }
    case 0xA4: { // LDY zero page
        uint8_t addr = cpu_read(cpu, ppu, cpu->PC++);
        cpu->Y = cpu_read(cpu, ppu, addr);
        set_flag(cpu, FLAG_Z, cpu->Y == 0);
        set_flag(cpu, FLAG_N, cpu->Y & 0x80);
        debug("%04X: LDY $%02X => %02X\n", cpu->PC - 2, addr, cpu->Y);
        break;
    }

    case 0x8C: { // STY absolute
        uint8_t lo = cpu_read(cpu, ppu, cpu->PC++);
        uint8_t hi = cpu_read(cpu, ppu, cpu->PC++);
        uint16_t addr = lo | (hi << 8);
        cpu_write(cpu, ppu, addr, cpu->Y);
        debug("%04X: STY $%04X <= %02X\n", cpu->PC - 3, addr, cpu->Y);
        break;
    }

    case 0x08: { // PHP
        cpu_write(cpu, ppu, 0x0100 + cpu->S--, cpu->P);
        debug("%04X: PHP; (pushed $%02X)\n", cpu->PC - 1, cpu->P);
        break;
    }
    case 0x28: { // PLP
        cpu->P = cpu_read(cpu, ppu, 0x0100 + cpu->S++);
        debug("%04X: PLP; (pulled $%02X)\n", cpu->PC - 1, cpu->P);
        break;
    }

    case 0x9A: { // TXS
        cpu->S = cpu->X;
        debug("%04X: TXS; (S = %02X)\n", cpu->PC - 1, cpu->S);
        break;
    }

    case 0x4C: { // JMP
        uint8_t lo = cpu_read(cpu, ppu, cpu->PC++);
        uint8_t hi = cpu_read(cpu, ppu, cpu->PC++);
        uint16_t addr = lo | (hi << 8);
        debug("%04X: JMP $%04X\n", cpu->PC - 3, addr);
        cpu->PC = addr;
        break;
    }
    case 0x20: { // JSR absolute
        uint8_t lo = cpu_read(cpu, ppu, cpu->PC++);
        uint8_t hi = cpu_read(cpu, ppu, cpu->PC++);
        uint16_t addr = lo | (hi << 8);

        uint16_t return_addr = cpu->PC - 1;

        cpu_write(cpu, ppu, 0x0100 + cpu->S--, (return_addr >> 8));
        cpu_write(cpu, ppu, 0x0100 + cpu->S--, (return_addr & 0xFF));

        debug("%04X: JSR $%04X (ret=$%04X)\n", cpu->PC - 3, addr, cpu->PC - 1);
        cpu->PC = addr;
        break;
    }
    case 0x60: { // RTS
        uint8_t lo = cpu_read(cpu, ppu, 0x0100 + cpu->S++);
        uint8_t hi = cpu_read(cpu, ppu, 0x0100 + cpu->S++);
        uint16_t addr = (lo | (hi << 8)) + 1;

        debug("%04X: RTS (ret=$%04X)\n", cpu->PC - 1, addr);
        cpu->PC = addr;
        break;
    }
    // flag branching
    case 0xF0: { // BEQ
        int8_t offset = (int8_t)cpu_read(cpu, ppu, cpu->PC++);
        debug("%04X: BEQ %d\n", cpu->PC - 2, offset);
        if (get_flag(cpu, FLAG_Z)) {
            cpu->PC += offset;
            debug("; Branched to $%04X\n", cpu->PC);
        }
        break;
    }
    case 0xD0: { // BNE
        int8_t offset = (int8_t)cpu_read(cpu, ppu, cpu->PC++);
        debug("%04X: BNE %d\n", cpu->PC - 2, offset);
        if (!get_flag(cpu, FLAG_Z)) {
            cpu->PC += offset;
            debug("; Branched to $%04X\n", cpu->PC);
        }
        break;
    }
    case 0x30: { // BMI
        int8_t offset = (int8_t)cpu_read(cpu, ppu, cpu->PC++);
        debug("%04X: BMI %d\n", cpu->PC - 2, offset);
        if (get_flag(cpu, FLAG_N)) {
            cpu->PC += offset;
            debug("; Branched to $%04X\n", cpu->PC);
        }
        break;
    }
    case 0x10: { // BPL
        int8_t offset = (int8_t)cpu_read(cpu, ppu, cpu->PC++);
        debug("%04X: BPL %d\n", cpu->PC - 2, offset);
        if (!get_flag(cpu, FLAG_N)) {
            cpu->PC += offset;
            debug("; Branched to $%04X\n", cpu->PC);
        }
        break;
    }
    case 0x90: { // BCC
        int8_t offset = (int8_t)cpu_read(cpu, ppu, cpu->PC++);
        debug("%04X: BCC %d\n", cpu->PC - 2, offset);
        if (!get_flag(cpu, FLAG_C)) {
            cpu->PC += offset;
            debug("; Branched to $%04X\n", cpu->PC);
        }
        break;
    }
    case 0xB0: { // BCS
        int8_t offset = (int8_t)cpu_read(cpu, ppu, cpu->PC++);
        debug("%04X: BCS %d\n", cpu->PC - 2, offset);
        if (get_flag(cpu, FLAG_C)) {
            cpu->PC += offset;
            debug("; Branched to $%04X\n", cpu->PC);
        }
        break;
    }

    case 0x78: { // SEI
        set_flag(cpu, FLAG_I, true);
        debug("%04X: SEI\n", cpu->PC - 1);
        break;
    }
    case 0xD8: { // CLD
        set_flag(cpu, FLAG_D, false);
        debug("%04X: CLD\n", cpu->PC - 1);
        break;
    }
    case 0xC9: { // CMP IMMEDIATE
        uint8_t value = cpu_read(cpu, ppu, cpu->PC++);

        uint8_t result = cpu->A - value;

        set_flag(cpu, FLAG_C, cpu->A >= value);
        set_flag(cpu, FLAG_Z, result == 0);
        set_flag(cpu, FLAG_N, result & 0x80);

        debug("%04X: CMP #$%02X\n", cpu->PC - 2, value);
        break;
    }

    case 0xEA: // NOP
        debug("%04X: NOP\n", cpu->PC - 1);
        break;
    default:
        printf("%04X: %02X; was not implimented\n", cpu->PC - 1, opcode);
        exit(0);
        break;
    }
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
    uint8_t tile_id = ppu_vram_read(ppu, 0x2000 + nametable_index);

    // 4. CHR fetch
    uint8_t* chr = ppu->chr_rom->buf;
    uint16_t tile_addr = tile_id * 16;

    uint8_t plane0 = chr[tile_addr + py];
    uint8_t plane1 = chr[tile_addr + py + 8];

    int bit = 7 - px;
    uint8_t low = (plane0 >> bit) & 1;
    uint8_t high = (plane1 >> bit) & 1;
    uint8_t color_index = (high << 1) | low;

    // =========================
    // 5. ATTRIBUTE TABLE FIX
    // =========================

    int attr_x = tile_x / 4;
    int attr_y = tile_y / 4;

    int attr_index = attr_y * 8 + attr_x;
    uint8_t attr = ppu_vram_read(ppu, 0x23C0 + attr_index);

    int sub_x = (tile_x % 4) / 2;
    int sub_y = (tile_y % 4) / 2;

    int shift = (sub_y * 2 + sub_x) * 2;
    uint8_t palette_index = (attr >> shift) & 0x03;

    // =========================
    // 6. PALETTE LOOKUP
    // =========================
    if (color_index == 0) {
        return 0xFF000000 | nes_palette[ppu->palette[0]];
    }
    uint8_t nes_color_index = ppu->palette[palette_index * 4 + color_index];

    return 0xFF000000 | nes_palette[nes_color_index & 0x3F];
}

void ppu_render(PPU* ppu) {
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            ppu->fb[y * WIDTH + x] = ppu_get_pixel(ppu, x, y);
        }
    }
}

void ppu_step(PPU* ppu) {
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
        ppu_render(ppu);
    }

    if (ppu->scanline == 261 && ppu->cycle == 1) {
        ppu->status &= ~0x80;
    }
}

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
            if (e.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }
        for (int i = 0; i < 29780; i++) {
            cpu_step(&cpu, &ppu);

            for (int j = 0; j < 3; j++) {
                ppu_step(&ppu);
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
