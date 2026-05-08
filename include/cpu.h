#ifndef CPU_H
#define CPU_H
#include <ints.h>
#include <map.h>

#define WIDTH 256
#define HEIGHT 240

#ifdef DEBUG_BUILD
#define DEF_OPCODES(...)                                                       \
    typedef enum Opcode Opcode;                                                \
    enum Opcode { __VA_ARGS__ };                                               \
    static const char* opcode_list[] = {MAP(STR, __VA_ARGS__)};                       \
    static const size_t opcode_count = COUNT(__VA_ARGS__);

#else
#define DEF_OPCODES(...) enum Opcode { __VA_ARGS__ }
#endif

#include <stdbool.h>
#include <stdint.h>
#include <util.h>
DEF_OPCODES(BRK, ORA, STP, SLO, NOP, ASL, PHP, ANC, BPL, CLC, JSR, AND, RLA,
            BIT, ROL, PLP, BMI, SEC, RTI, EOR, SRE, LSR, PHA, ALR, JMP, BVC,
            CLI, RTS, ADC, RRA, ROR, PLA, ARR, BVS, SEI, STA, SAX, STY, STX,
            DEY, TXA, XAA, BCC, AHX, TYA, TXS, TAS, SHY, SHX, LDY, LDA, LDX,
            LAX, TAY, TAX, BCS, CLV, TSX, LAS, CPY, CMP, DCP, DEC, INY, DEX,
            AXS, BNE, CLD, CPX, SBC, ISC, INC, INX, BEQ, SED);

typedef enum Mode Mode;
enum Mode {
    IMPL,
    ACC,
    IMM,     // #i
    ZP,      // d
    ZP_X,    // d, x
    ZP_Y,    // d, y
    REL,     // * + d
    ABS,     // a
    ABS_X,   // a, x
    ABS_Y,   // a, y
    IND,     // indirect // (d)
    IDX_IND, // indexed  indirect // (d, x)
    IND_IDX, // indirect indexed // (d),y
};

typedef struct Instruction Instruction;
struct Instruction {
    Opcode opcode;
    Mode mode;
};

static Instruction instructions[] = {
    {BRK, IMPL},  {ORA, IDX_IND}, {STP, IMPL},  {SLO, IDX_IND},
    {NOP, ZP},    {ORA, ZP},      {ASL, ZP},    {SLO, ZP},
    {PHP, IMPL},  {ORA, IMM},     {ASL, ACC},   {ANC, IMM},
    {NOP, ABS},   {ORA, ABS},     {ASL, ABS},   {SLO, ABS},
    {BPL, REL},   {ORA, IND_IDX}, {STP, IMPL},  {SLO, IND_IDX},
    {NOP, ZP_X},  {ORA, ZP_X},    {ASL, ZP_X},  {SLO, ZP_X},
    {CLC, IMPL},  {ORA, ABS_Y},   {NOP, IMPL},  {SLO, ABS_Y},
    {NOP, ABS_X}, {ORA, ABS_X},   {ASL, ABS_X}, {SLO, ABS_X},
    {JSR, ABS},   {AND, IDX_IND}, {STP, IMPL},  {RLA, IDX_IND},
    {BIT, ZP},    {AND, ZP},      {ROL, ZP},    {RLA, ZP},
    {PLP, IMPL},  {AND, IMM},     {ROL, ACC},   {ANC, IMM},
    {BIT, ABS},   {AND, ABS},     {ROL, ABS},   {RLA, ABS},
    {BMI, REL},   {AND, IND_IDX}, {STP, IMPL},  {RLA, IND_IDX},
    {NOP, ZP_X},  {AND, ZP_X},    {ROL, ZP_X},  {RLA, ZP_X},
    {SEC, IMPL},  {AND, ABS_Y},   {NOP, IMPL},  {RLA, ABS_Y},
    {NOP, ABS_X}, {AND, ABS_X},   {ROL, ABS_X}, {RLA, ABS_X},
    {RTI, IMPL},  {EOR, IDX_IND}, {STP, IMPL},  {SRE, IDX_IND},
    {NOP, ZP},    {EOR, ZP},      {LSR, ZP},    {SRE, ZP},
    {PHA, IMPL},  {EOR, IMM},     {LSR, ACC},   {ALR, IMM},
    {JMP, ABS},   {EOR, ABS},     {LSR, ABS},   {SRE, ABS},
    {BVC, REL},   {EOR, IND_IDX}, {STP, IMPL},  {SRE, IND_IDX},
    {NOP, ZP_X},  {EOR, ZP_X},    {LSR, ZP_X},  {SRE, ZP_X},
    {CLI, IMPL},  {EOR, ABS_Y},   {NOP, IMPL},  {SRE, ABS_Y},
    {NOP, ABS_X}, {EOR, ABS_X},   {LSR, ABS_X}, {SRE, ABS_X},
    {RTS, IMPL},  {ADC, IDX_IND}, {STP, IMPL},  {RRA, IDX_IND},
    {NOP, ZP},    {ADC, ZP},      {ROR, ZP},    {RRA, ZP},
    {PLA, IMPL},  {ADC, IMM},     {ROR, ACC},   {ARR, IMM},
    {JMP, IND},   {ADC, ABS},     {ROR, ABS},   {RRA, ABS},
    {BVS, REL},   {ADC, IND_IDX}, {STP, IMPL},  {RRA, IND_IDX},
    {NOP, ZP_X},  {ADC, ZP_X},    {ROR, ZP_X},  {RRA, ZP_X},
    {SEI, IMPL},  {ADC, ABS_Y},   {NOP, IMPL},  {RRA, ABS_Y},
    {NOP, ABS_X}, {ADC, ABS_X},   {ROR, ABS_X}, {RRA, ABS_X},
    {NOP, IMM},   {STA, IDX_IND}, {NOP, IMM},   {SAX, IDX_IND},
    {STY, ZP},    {STA, ZP},      {STX, ZP},    {SAX, ZP},
    {DEY, IMPL},  {NOP, IMM},     {TXA, IMPL},  {XAA, IMM},
    {STY, ABS},   {STA, ABS},     {STX, ABS},   {SAX, ABS},
    {BCC, REL},   {STA, IND_IDX}, {STP, IMPL},  {AHX, IND_IDX},
    {STY, ZP_X},  {STA, ZP_X},    {STX, ZP_Y},  {SAX, ZP_Y},
    {TYA, IMPL},  {STA, ABS_Y},   {TXS, IMPL},  {TAS, ABS_Y},
    {SHY, ABS_X}, {STA, ABS_X},   {SHX, ABS_Y}, {AHX, ABS_Y},
    {LDY, IMM},   {LDA, IDX_IND}, {LDX, IMM},   {LAX, IDX_IND},
    {LDY, ZP},    {LDA, ZP},      {LDX, ZP},    {LAX, ZP},
    {TAY, IMPL},  {LDA, IMM},     {TAX, IMPL},  {LAX, IMM},
    {LDY, ABS},   {LDA, ABS},     {LDX, ABS},   {LAX, ABS},
    {BCS, REL},   {LDA, IND_IDX}, {STP, IMPL},  {LAX, IND_IDX},
    {LDY, ZP_X},  {LDA, ZP_X},    {LDX, ZP_Y},  {LAX, ZP_Y},
    {CLV, IMPL},  {LDA, ABS_Y},   {TSX, IMPL},  {LAS, ABS_Y},
    {LDY, ABS_X}, {LDA, ABS_X},   {LDX, ABS_Y}, {LAX, ABS_Y},
    {CPY, IMM},   {CMP, IDX_IND}, {NOP, IMM},   {DCP, IDX_IND},
    {CPY, ZP},    {CMP, ZP},      {DEC, ZP},    {DCP, ZP},
    {INY, IMPL},  {CMP, IMM},     {DEX, IMPL},  {AXS, IMM},
    {CPY, ABS},   {CMP, ABS},     {DEC, ABS},   {DCP, ABS},
    {BNE, REL},   {CMP, IND_IDX}, {STP, IMPL},  {DCP, IND_IDX},
    {NOP, ZP_X},  {CMP, ZP_X},    {DEC, ZP_X},  {DCP, ZP_X},
    {CLD, IMPL},  {CMP, ABS_Y},   {NOP, IMPL},  {DCP, ABS_Y},
    {NOP, ABS_X}, {CMP, ABS_X},   {DEC, ABS_X}, {DCP, ABS_X},
    {CPX, IMM},   {SBC, IDX_IND}, {NOP, IMM},   {ISC, IDX_IND},
    {CPX, ZP},    {SBC, ZP},      {INC, ZP},    {ISC, ZP},
    {INX, IMPL},  {SBC, IMM},     {NOP, IMPL},  {SBC, IMM},
    {CPX, ABS},   {SBC, ABS},     {INC, ABS},   {ISC, ABS},
    {BEQ, REL},   {SBC, IND_IDX}, {STP, IMPL},  {ISC, IND_IDX},
    {NOP, ZP_X},  {SBC, ZP_X},    {INC, ZP_X},  {ISC, ZP_X},
    {SED, IMPL},  {SBC, ABS_Y},   {NOP, IMPL},  {ISC, ABS_Y},
    {NOP, ABS_X}, {SBC, ABS_X},   {INC, ABS_X}, {ISC, ABS_X},
};

static inline bool page_crossed(u16 a, u16 b) {
    return (a & 0xFF00) != (b & 0xFF00);
}

static int cycles_base[] = {
    // TASK(20260507-214301-213-n6-798): fill this table
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

typedef struct CartridgeInfo CartridgeInfo;
struct CartridgeInfo {
    u8 prg_banks;
    u8 chr_banks;
    u8 mapper;
};

typedef struct Cartridge Cartridge;
struct Cartridge {
    ByteSlice prg_rom;
    ByteSlice chr_rom;
    CartridgeInfo info;
};

typedef struct CPU CPU;
struct CPU {
    u8 A;   //  accumulator
    u8 P;   // status (flags)
    u16 PC; // program counter
    u8 S;   // stack pointer
    u8 X;   // X Register
    u8 Y;   // Y Register
    u8 cpu_ram[2048];
    Cartridge* cart; // rom and such
    bool nmi_pending;
};

typedef struct PPU PPU;
struct PPU {
    u8 vram[2048];

    u8 palette[32];

    const ByteSlice* chr_rom;

    u8 nametable_mirror;

    int scanline;
    int cycle;
    uint32_t fb[WIDTH * HEIGHT];
    u8 control;
    u8 mask;
    u8 status;

    u16 vram_addr;
    u16 temp_addr;
    u8 fine_x;
    u8 open_bus;
    u8 latch;
};

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

static inline bool get_flag(const CPU* cpu, Flag f) {
    return (cpu->P >> f) & 1;
}

// TASK(20260508-100443-929-n6-210): make flag u only forced to be 1 when
// pushing to the stack, not when setting
static inline u8 set_flag(u8 p, Flag f, bool val) {
    if (val) {
        return p | (1 << f) | (1 << FLAG_U); // flag u always 1
    } else {
        return (p & ~(1 << f)) | (1 << FLAG_U); // flag u always 1
    }
}
static inline void set_flag_mut(CPU* cpu, Flag f, bool val) {
    cpu->P = set_flag(cpu->P, f, val);
}

int cpu_step(CPU* cpu, PPU* ppu);

u8 cpu_read(const CPU* cpu, PPU* ppu, u16 addr);
void cpu_write(CPU* cpu, PPU* ppu, u16 addr, u8 value);

void ppu_vram_write(PPU* ppu, u16 addr, u8 val);
u8 ppu_vram_read(const PPU* ppu, u16 addr);

#endif
