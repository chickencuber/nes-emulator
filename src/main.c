#define NO_DEBUG
#include <SDL3/SDL_events.h>
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

// TASK(20260505-161630-763-n6-804): clean this file

#define SCALE 4

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
    // 0x0000–0x1FFF: internal RAM (mirror every 2KB)
    if (addr < 0x2000) {
        return cpu->cpu_ram[addr & 0x07FF];
    }

    // 0x2000–0x3FFF: PPU registers (mirrored every 8 bytes)
    if (addr < 0x4000) {
        uint16_t reg = 0x2000 + (addr & 7);

        switch (reg) {
        case 0x2002: {
            uint8_t val = ppu->status;
            ppu->status &= ~0x80; // clear vblank
            ppu->latch = 0;
            return val;
        }

        default:
            return 0;
        }
    }

    // 0x8000–0xFFFF: cartridge ROM
    if (addr >= 0x8000) {
        size_t offset = addr - 0x8000;

        if (cpu->cart->info.prg_banks == 1) {
            offset &= 0x3FFF;
        }

        return cpu->cart->prg_rom.buf[offset];
    }

    // TASK(20260502-134138-467-n6-189): handle other addresses in read
    printf(";asking for unimplemented %04X in read\n", addr);
    return 0;
}

void cpu_write(CPU* cpu, PPU* ppu, uint16_t addr, uint8_t value) {
    // 0x0000–0x1FFF: internal RAM (includes stack!)
    if (addr < 0x2000) {
        cpu->cpu_ram[addr & 0x07FF] = value;
        return;
    }

    // 0x2000–0x3FFF: PPU registers (mirrored every 8 bytes)
    if (addr < 0x4000) {
        uint16_t reg = 0x2000 + (addr & 7);

        switch (reg) {

        case 0x2000:
            ppu->control = value;
            return;

        case 0x2005:
            ppu->latch ^= 1;
            return;

        case 0x2006:
            if (!ppu->latch) {
                ppu->temp_addr = value << 8;
                ppu->latch = 1;
            } else {
                ppu->temp_addr |= value;
                ppu->vram_addr = ppu->temp_addr;
                ppu->latch = 0;
            }
            return;

        case 0x2007:
            debug("PPU WRITE: %04X <- %02X\n", ppu->vram_addr, value);

            ppu_vram_write(ppu, ppu->vram_addr, value);

            ppu->vram_addr += (ppu->control & 0x04) ? 32 : 1;
            return;

        default:
            return;
        }
    }

    if (addr >= 0x4000 && addr <= 0x4017) {
        // TASK(20260505-162005-313-n6-469): implement sound
        debug("; sound not implemented");
        return;
    }
    // 0x8000–0xFFFF: ROM (ignored writes)
    if (addr >= 0x8000) {
        return;
    }

    // TASK(20260502-141946-173-n6-290): handle other addresses for writes
    printf(";asking for unimplemented %04X in write\n", addr);
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

static inline uint8_t set_flag(uint8_t p, Flag f, bool val) {
    if (val) {
        return p | (1 << f) | (1 << FLAG_U); // flag u always 1
    } else {
        return (p & ~(1 << f)) | (1 << FLAG_U); // flag u always 1
    }
}

static inline void set_flag_mut(CPU* cpu, Flag f, bool val) {
    cpu->P = set_flag(cpu->P, f, val);
}

static inline bool page_crossed(uint16_t a, uint16_t b) {
    return (a & 0xFF00) != (b & 0xFF00);
}

#define INCPC                                                                  \
    ({                                                                         \
        opcodelen++;                                                           \
        cpu_read(cpu, ppu, cpu->PC++);                                         \
    })

#define PUSH(s) cpu_write(cpu, ppu, 0x0100 + (cpu->S--), (s))

#define POP cpu_read(cpu, ppu, 0x0100 + (++cpu->S))

int cpu_step(CPU* cpu, PPU* ppu) {
    int opcodelen = 0;
    uint8_t opcode = INCPC;

    // TASK(20260505-170156-448-n6-762): add debug messages to what I missed
    // TASK(20260502-211526-008-n6-146): reorganize the order of this
    switch (opcode) {
    case 0xA9: { // LDA immediate
        uint8_t value = INCPC;
        cpu->A = value;
        set_flag_mut(cpu, FLAG_Z, value == 0);
        set_flag_mut(cpu, FLAG_N, value & 0x80);
        debug("%04X: LDA #$%02X\n", cpu->PC - opcodelen, value);
        return 2;
    }
    case 0xAD: { // LDA absolute
        uint8_t lo = INCPC;
        uint8_t hi = INCPC;
        uint16_t addr = lo | (hi << 8);
        cpu->A = cpu_read(cpu, ppu, addr);
        set_flag_mut(cpu, FLAG_Z, cpu->A == 0);
        set_flag_mut(cpu, FLAG_N, cpu->A & 0x80);
        debug("%04X: LDA $%04X => %02X\n", cpu->PC - opcodelen, addr, cpu->A);
        return 4;
    }
    case 0xA5: { // LDA zero page
        uint8_t addr = INCPC;
        cpu->A = cpu_read(cpu, ppu, addr);
        set_flag_mut(cpu, FLAG_Z, cpu->A == 0);
        set_flag_mut(cpu, FLAG_N, cpu->A & 0x80);
        debug("%04X: LDA $%02X => %02X\n", cpu->PC - opcodelen, addr, cpu->A);
        return 3;
    }
    case 0xBD: { // LDA absolute,X
        uint8_t lo = INCPC;
        uint8_t hi = INCPC;
        uint16_t base = lo | (hi << 8);

        uint16_t addr = base + cpu->X;

        cpu->A = cpu_read(cpu, ppu, addr);

        set_flag_mut(cpu, FLAG_Z, cpu->A == 0);
        set_flag_mut(cpu, FLAG_N, cpu->A & 0x80);

        bool crossed = page_crossed(base, addr);

        debug("%04X: LDA $%04X,X => %02X\n", cpu->PC - opcodelen, base, cpu->A);
        return crossed ? 5 : 4;
    }

    case 0x8D: { // STA absolute
        uint8_t lo = INCPC;
        uint8_t hi = INCPC;
        uint16_t addr = lo | (hi << 8);
        cpu_write(cpu, ppu, addr, cpu->A);
        debug("%04X: STA $%04X <= %02X\n", cpu->PC - opcodelen, addr, cpu->A);
        return 4;
    }
    case 0x85: { // STA zero page
        uint8_t addr = INCPC;
        cpu_write(cpu, ppu, addr, cpu->A);
        debug("%04X: STA $%02X <= %02X\n", cpu->PC - opcodelen, addr, cpu->A);
        return 3;
    }
    case 0x91: { // STA (zp),Y
        uint8_t zp = INCPC;

        uint8_t lo = cpu_read(cpu, ppu, zp);
        uint8_t hi = cpu_read(cpu, ppu, (zp + 1) & 0xFF);

        uint16_t base = (lo | (hi << 8));
        uint16_t addr = base + cpu->Y;

        cpu_write(cpu, ppu, addr, cpu->A);
        bool crossed = page_crossed(base, addr);

        debug("%04X: STA ($%02X),Y => %04X <= %02X\n", cpu->PC - opcodelen, zp,
              addr, cpu->A);
        return crossed ? 7 : 6;
    }
    case 0x99: { // STA abs,Y
        uint8_t lo = INCPC;
        uint8_t hi = INCPC;

        uint16_t base = lo | (hi << 8);
        uint16_t addr = base + cpu->Y;

        cpu_write(cpu, ppu, addr, cpu->A);

        return 5;
    }

    case 0x48: { // PHA
        PUSH(cpu->A);
        debug("%04X: PHA; (pushed $%02X)\n", cpu->PC - opcodelen, cpu->A);
        return 3;
    }
    case 0x68: { // PLA
        cpu->A = POP;
        debug("%04X: PLA; (pulled $%02X)\n", cpu->PC - opcodelen, cpu->A);
        return 4;
    }

    case 0xA2: { // LDX immediate
        uint8_t value = INCPC;
        cpu->X = value;
        set_flag_mut(cpu, FLAG_Z, value == 0);
        set_flag_mut(cpu, FLAG_N, value & 0x80);
        debug("%04X: LDX #$%02X\n", cpu->PC - opcodelen, value);
        return 2;
    }
    case 0xAE: { // LDX absolute
        uint8_t lo = INCPC;
        uint8_t hi = INCPC;
        uint16_t addr = lo | (hi << 8);
        cpu->X = cpu_read(cpu, ppu, addr);
        set_flag_mut(cpu, FLAG_Z, cpu->X == 0);
        set_flag_mut(cpu, FLAG_N, cpu->X & 0x80);
        debug("%04X: LDX $%04X => %02X\n", cpu->PC - opcodelen, addr, cpu->X);
        return 4;
    }
    case 0xA6: { // LDX zero page
        uint8_t addr = INCPC;
        cpu->X = cpu_read(cpu, ppu, addr);
        set_flag_mut(cpu, FLAG_Z, cpu->X == 0);
        set_flag_mut(cpu, FLAG_N, cpu->X & 0x80);
        debug("%04X: LDX $%02X => %02X\n", cpu->PC - opcodelen, addr, cpu->X);
        return 3;
    }

    case 0xCA: { // DEX
        cpu->X--;
        set_flag_mut(cpu, FLAG_Z, cpu->X == 0);
        set_flag_mut(cpu, FLAG_N, cpu->X & 0x80);
        debug("%04X: DEX => %02X\n", cpu->PC - opcodelen, cpu->X);
        return 2;
    }

    case 0xE0: { // CPX
        uint8_t value = INCPC;
        uint8_t result = cpu->X - value;

        set_flag_mut(cpu, FLAG_C, cpu->X >= value);
        set_flag_mut(cpu, FLAG_Z, cpu->X == value);
        set_flag_mut(cpu, FLAG_N, result & 0x80);
        debug("%04X: CPX\n", cpu->PC - opcodelen);
        return 2;
    }

    case 0x8E: { // STX absolute
        uint8_t lo = INCPC;
        uint8_t hi = INCPC;
        uint16_t addr = lo | (hi << 8);
        cpu_write(cpu, ppu, addr, cpu->X);
        debug("%04X: STX $%04X <= %02X\n", cpu->PC - opcodelen, addr, cpu->X);
        return 4;
    }
    case 0x86: { // STX zero page
        uint8_t addr = INCPC;
        cpu_write(cpu, ppu, addr, cpu->X);
        debug("%04X: STX $%02X <= %02X\n", cpu->PC - opcodelen, addr, cpu->X);
        return 3;
    }

    case 0xA0: { // LDY immediate
        uint8_t value = INCPC;
        cpu->Y = value;
        set_flag_mut(cpu, FLAG_Z, value == 0);
        set_flag_mut(cpu, FLAG_N, value & 0x80);
        debug("%04X: LDY #$%02X\n", cpu->PC - opcodelen, value);
        return 2;
    }
    case 0xAC: { // LDY absolute
        uint8_t lo = INCPC;
        uint8_t hi = INCPC;
        uint16_t addr = lo | (hi << 8);
        cpu->Y = cpu_read(cpu, ppu, addr);
        set_flag_mut(cpu, FLAG_Z, cpu->Y == 0);
        set_flag_mut(cpu, FLAG_N, cpu->Y & 0x80);
        debug("%04X: LDY $%04X => %02X\n", cpu->PC - opcodelen, addr, cpu->Y);
        return 4;
    }
    case 0xA4: { // LDY zero page
        uint8_t addr = INCPC;
        cpu->Y = cpu_read(cpu, ppu, addr);
        set_flag_mut(cpu, FLAG_Z, cpu->Y == 0);
        set_flag_mut(cpu, FLAG_N, cpu->Y & 0x80);
        debug("%04X: LDY $%02X => %02X\n", cpu->PC - opcodelen, addr, cpu->Y);
        return 3;
    }

    case 0x8C: { // STY absolute
        uint8_t lo = INCPC;
        uint8_t hi = INCPC;
        uint16_t addr = lo | (hi << 8);
        cpu_write(cpu, ppu, addr, cpu->Y);
        debug("%04X: STY $%04X <= %02X\n", cpu->PC - opcodelen, addr, cpu->Y);
        return 4;
    }

    case 0x88: { // DEY
        cpu->Y--;
        set_flag_mut(cpu, FLAG_Z, cpu->Y == 0);
        set_flag_mut(cpu, FLAG_N, cpu->Y & 0x80);
        debug("%04X: DEY => %02X\n", cpu->PC - opcodelen, cpu->Y);
        return 2;
    }
    case 0xC8: { // INY
        cpu->Y++;
        set_flag_mut(cpu, FLAG_Z, cpu->Y == 0);
        set_flag_mut(cpu, FLAG_N, cpu->Y & 0x80);
        debug("%04X: INY => %02X\n", cpu->PC - opcodelen, cpu->Y);
        return 2;
    }

    case 0xC0: { // CPY
        uint8_t value = INCPC;
        uint8_t result = cpu->Y - value;

        set_flag_mut(cpu, FLAG_C, cpu->Y >= value);
        set_flag_mut(cpu, FLAG_Z, cpu->Y == value);
        set_flag_mut(cpu, FLAG_N, result & 0x80);
        debug("%04X: CPY; (%02X)\n", cpu->PC - opcodelen, value);
        return 2;
    }

    case 0x19: { // ORA abs,Y
        uint8_t lo = INCPC;
        uint8_t hi = INCPC;
        uint16_t base = (lo | (hi << 8));
        uint16_t addr = base + cpu->Y;

        uint8_t value = cpu_read(cpu, ppu, addr);
        bool crossed = page_crossed(base, addr);

        cpu->A |= value;

        set_flag_mut(cpu, FLAG_Z, cpu->A == 0);
        set_flag_mut(cpu, FLAG_N, cpu->A & 0x80);

        debug("%04X: ORA $%04X,Y => A=%02X\n", cpu->PC - opcodelen,
              addr - cpu->Y, cpu->A);
        return crossed ? 5 : 4;
    }
    case 0x09: { // ORA immediate
        uint8_t value = INCPC;

        cpu->A |= value;

        set_flag_mut(cpu, FLAG_Z, cpu->A == 0);
        set_flag_mut(cpu, FLAG_N, cpu->A & 0x80);

        return 2;
    }
    case 0x29: { // AND immediate
        uint8_t value = INCPC;

        cpu->A &= value;

        set_flag_mut(cpu, FLAG_Z, cpu->A == 0);
        set_flag_mut(cpu, FLAG_N, cpu->A & 0x80);

        return 2;
    }
    case 0x08: { // PHP
        PUSH(cpu->P);
        debug("%04X: PHP; (pushed $%02X)\n", cpu->PC - opcodelen, cpu->P);
        return 3;
    }
    case 0x28: { // PLP
        cpu->P = POP;
        debug("%04X: PLP; (pulled $%02X)\n", cpu->PC - opcodelen, cpu->P);
        return 4;
    }

    case 0x9A: { // TXS
        cpu->S = cpu->X;
        debug("%04X: TXS; (S = %02X)\n", cpu->PC - opcodelen, cpu->S);
        return 2;
    }
    case 0x8A: { // TXA
        cpu->A = cpu->X;

        set_flag_mut(cpu, FLAG_Z, cpu->A == 0);
        set_flag_mut(cpu, FLAG_N, cpu->A & 0x80);

        return 2;
    }


    case 0xFE: { // INC abs,X
        uint8_t lo = INCPC;
        uint8_t hi = INCPC;

        uint16_t base = lo | (hi << 8);
        uint16_t addr = base + cpu->X;

        uint8_t value = cpu_read(cpu, ppu, addr);
        value++;

        cpu_write(cpu, ppu, addr, value);

        set_flag_mut(cpu, FLAG_Z, value == 0);
        set_flag_mut(cpu, FLAG_N, value & 0x80);

        return 7;
    }
    case 0xEE: { // INC abs
        uint8_t lo = INCPC;
        uint8_t hi = INCPC;

        uint16_t addr= lo | (hi << 8);

        uint8_t value = cpu_read(cpu, ppu, addr);
        value++;

        cpu_write(cpu, ppu, addr, value);

        set_flag_mut(cpu, FLAG_Z, value == 0);
        set_flag_mut(cpu, FLAG_N, value & 0x80);

        return 6;
    }

    case 0x00: { // BRK
        INCPC;

        uint16_t ret = cpu->PC + 1;

        PUSH(ret >> 8);
        PUSH(ret & 0xFF);

        uint8_t p = set_flag(cpu->P, FLAG_B, true);
        PUSH(p);

        set_flag_mut(cpu, FLAG_I, true);
        uint16_t lo = cpu_read(cpu, ppu, 0xFFFE);
        uint16_t hi = cpu_read(cpu, ppu, 0xFFFF);

        debug("%04X: BRK triggered => %04X\n", cpu->PC - opcodelen,
              lo | (hi << 8));
        cpu->PC = lo | (hi << 8);

        return 7;
    }

    case 0x4C: { // JMP
        uint8_t lo = INCPC;
        uint8_t hi = INCPC;
        uint16_t addr = lo | (hi << 8);
        debug("%04X: JMP $%04X\n", cpu->PC - opcodelen, addr);
        cpu->PC = addr;
        return 3;
    }
    case 0x20: { // JSR absolute
        uint8_t lo = INCPC;
        uint8_t hi = INCPC;
        uint16_t addr = lo | (hi << 8);

        uint16_t return_addr = cpu->PC - 1;

        PUSH(return_addr >> 8);
        PUSH(return_addr & 0xFF);

        debug("%04X: JSR $%04X (ret=$%04X)\n", cpu->PC - opcodelen, addr,
              return_addr);
        cpu->PC = addr;
        return 6;
    }
    case 0x60: { // RTS
        uint8_t lo = POP;

        uint8_t hi = POP;
        uint16_t addr = (lo | (hi << 8)) + 1;

        debug("%04X: RTS (ret=$%04X)\n", cpu->PC - opcodelen, addr);
        cpu->PC = addr;
        return 6;
    }
    // flag branching
    case 0xF0: { // BEQ
        int8_t offset = (int8_t)INCPC;
        debug("%04X: BEQ %d\n", cpu->PC - opcodelen, offset);
        if (get_flag(cpu, FLAG_Z)) {
            cpu->PC += offset;
            debug("; Branched to $%04X\n", cpu->PC);
            return 3;
        }
        return 2;
    }
    case 0xD0: { // BNE
        int8_t offset = (int8_t)INCPC;
        debug("%04X: BNE %d\n", cpu->PC - opcodelen, offset);
        if (!get_flag(cpu, FLAG_Z)) {
            cpu->PC += offset;
            debug("; Branched to $%04X\n", cpu->PC);
            return 3;
        }
        return 2;
    }
    case 0x30: { // BMI
        int8_t offset = (int8_t)INCPC;
        debug("%04X: BMI %d\n", cpu->PC - opcodelen, offset);
        if (get_flag(cpu, FLAG_N)) {
            cpu->PC += offset;
            debug("; Branched to $%04X\n", cpu->PC);
            return 3;
        }
        return 2;
    }
    case 0x10: { // BPL
        int8_t offset = (int8_t)INCPC;
        debug("%04X: BPL %d\n", cpu->PC - opcodelen, offset);
        if (!get_flag(cpu, FLAG_N)) {
            cpu->PC += offset;
            debug("; Branched to $%04X\n", cpu->PC);
            return 3;
        }
        return 2;
    }
    case 0x90: { // BCC
        int8_t offset = (int8_t)INCPC;
        debug("%04X: BCC %d\n", cpu->PC - opcodelen, offset);
        if (!get_flag(cpu, FLAG_C)) {
            cpu->PC += offset;
            debug("; Branched to $%04X\n", cpu->PC);
            return 3;
        }
        return 2;
    }
    case 0xB0: { // BCS
        int8_t offset = (int8_t)INCPC;
        debug("%04X: BCS %d\n", cpu->PC - opcodelen, offset);
        if (get_flag(cpu, FLAG_C)) {
            cpu->PC += offset;
            debug("; Branched to $%04X\n", cpu->PC);
            return 3;
        }
        return 2;
    }

    case 0x78: { // SEI
        set_flag_mut(cpu, FLAG_I, true);
        debug("%04X: SEI\n", cpu->PC - opcodelen);
        return 2;
    }
    case 0xD8: { // CLD
        set_flag_mut(cpu, FLAG_D, false);
        debug("%04X: CLD\n", cpu->PC - opcodelen);
        return 2;
    }
    case 0xC9: { // CMP IMMEDIATE
        uint8_t value = INCPC;

        uint8_t result = cpu->A - value;

        set_flag_mut(cpu, FLAG_C, cpu->A >= value);
        set_flag_mut(cpu, FLAG_Z, result == 0);
        set_flag_mut(cpu, FLAG_N, result & 0x80);

        debug("%04X: CMP #$%02X\n", cpu->PC - opcodelen, value);
        return 2;
    }

    case 0x1E: { // ASL abs,X
        uint8_t lo = INCPC;
        uint8_t hi = INCPC;
        uint16_t base = lo | (hi << 8);

        uint16_t addr = base + cpu->X;

        uint8_t value = cpu_read(cpu, ppu, addr);

        bool crossed = page_crossed(base, addr);

        set_flag_mut(cpu, FLAG_C, value & 0x80);

        value <<= 1;

        cpu_write(cpu, ppu, addr, value);

        set_flag_mut(cpu, FLAG_Z, value == 0);
        set_flag_mut(cpu, FLAG_N, value & 0x80);

        debug("%04X: ASL $%04X,X => %02X\n", cpu->PC - opcodelen, base, value);
        return crossed ? 8 : 7;
    }

    case 0x1C: // NOP abs X (unofficial)
        INCPC;
        INCPC;
        debug("%04X: NOP\n", cpu->PC - opcodelen);
        return 4;
    case 0x82: // NOP immediate (unnoficial)
        INCPC;
        /*fallthrough*/
    case 0x1A:
        /*fallthrough*/
    case 0xEA: // NOP
        debug("%04X: NOP\n", cpu->PC - opcodelen);
        return 2;
    case 0x2C: { // BIT absolute
        uint8_t lo = INCPC;
        uint8_t hi = INCPC;
        uint16_t addr = lo | (hi << 8);

        uint8_t m = cpu_read(cpu, ppu, addr);

        set_flag_mut(cpu, FLAG_Z, (cpu->A & m) == 0);
        set_flag_mut(cpu, FLAG_N, m & 0x80);
        set_flag_mut(cpu, FLAG_V, m & 0x40);

        return 4;
    }
    default:
        printf("%04X: %02X; was not implimented\n", cpu->PC - opcodelen,
               opcode);
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
