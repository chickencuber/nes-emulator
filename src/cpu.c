#include <cpu.h>
#include <ints.h>

u16 PC;
const Mode* MODE;

#define PUSH(s) cpu_write(cpu, ppu, 0x0100 + (cpu->S--), (s))

#define PUSH16(s)                                                              \
    do {                                                                       \
        PUSH((s) >> 8);                                                        \
        PUSH((s) & 0xFF);                                                      \
    } while (0)

#define POP cpu_read(cpu, ppu, 0x0100 + (++cpu->S))
#define POP16                                                                  \
    ({                                                                         \
        u8 lo = POP;                                                           \
        u8 hi = POP;                                                           \
        to16(hi, lo);                                                          \
    })

#define to16(hi, lo) ((hi) << 8) | (lo)

#define set_nz(v)                                                              \
    do {                                                                       \
        set_flag_mut(cpu, FLAG_Z, (v) == 0);                                   \
        set_flag_mut(cpu, FLAG_N, (v) >> 7);                                   \
    } while (0)

void ppu_vram_write(PPU* ppu, u16 addr, u8 val) {
    addr &= 0x3FFF;

    if (addr < 0x2000) {
        return;
    }
    if (addr < 0x3F00) {
        u16 vram_addr = addr - 0x2000;

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

u8 ppu_vram_read(const PPU* ppu, u16 addr) {
    addr &= 0x3FFF;

    if (addr < 0x2000) {
        return ppu->chr_rom->buf[addr];
    }
    if (addr < 0x3F00) {
        u16 vram_addr = addr - 0x2000;

        if (vram_addr & 0x0800) {
            vram_addr = (vram_addr & 0x03FF) | 0x0400;
        } else {
            vram_addr &= 0x03FF;
        }

        return ppu->vram[vram_addr];
    }

    if (addr < 0x4000) {
        return ppu->palette[(addr - 0x3F00) % 32];
    }
    return 0;
}

u8 cpu_read(const CPU* cpu, PPU* ppu, u16 addr) {
    // 0x0000–0x1FFF: internal RAM (mirror every 2KB)
    if (addr < 0x2000) {
        return cpu->cpu_ram[addr & 0x07FF];
    }

    // 0x2000–0x3FFF: PPU registers (mirrored every 8 bytes)
    if (addr < 0x4000) {
        u16 reg = 0x2000 + (addr & 7);

        switch (reg) {
        case 0x2002: {
            u8 val = ppu->status;
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
    // TASK(20260510-203758-089-n6-087): add controller support
    if (addr == 0x4016) { // p1
        return ((cpu->p1.right << 7) | (cpu->p1.left << 6) |
                (cpu->p1.down << 5) | (cpu->p1.up << 4) | (cpu->p1.start << 3) |
                (cpu->p1.select << 2) | (cpu->p1.b << 1) | (cpu->p1.a));
    }
    if (addr == 0x4017) { // p2
        return ((cpu->p2.right << 7) | (cpu->p2.left << 6) |
                (cpu->p2.down << 5) | (cpu->p2.up << 4) | (cpu->p2.start << 3) |
                (cpu->p2.select << 2) | (cpu->p2.b << 1) | (cpu->p2.a));
    }

    // TASK(20260502-134138-467-n6-189): handle other addresses in read
    printf(";asking for unimplemented %04X in read\n", addr);
    return 0;
}
void cpu_write(CPU* cpu, PPU* ppu, u16 addr, u8 value) {
    // 0x0000–0x1FFF: internal RAM (includes stack!)
    if (addr < 0x2000) {
        cpu->cpu_ram[addr & 0x07FF] = value;
        return;
    }

    // 0x2000–0x3FFF: PPU registers (mirrored every 8 bytes)
    if (addr < 0x4000) {
        u16 reg = 0x2000 + (addr & 7);

        switch (reg) {

        case 0x2000:
            ppu->control = value;
            return;

        case 0x2005:
            if (!ppu->latch) {
                ppu->scroll_x = value;
            } else {
                ppu->scroll_y = value;
            }
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
        case 0x4014: {
            u16 base = value << 8;

            for (int i = 0; i < 256; i++) {
                ppu->oam[i] = cpu_read(cpu, ppu, base + i);
            }
            return;
        }
        default:
            return;
        }
    }

    if (addr >= 0x4000 && addr <= 0x4017) {
        // TASK(20260505-162005-313-n6-469): implement sound
        debug("; sound not implemented\n");
        return;
    }
    // 0x8000–0xFFFF: ROM (ignored writes)
    if (addr >= 0x8000) {
        return;
    }

    // TASK(20260502-141946-173-n6-290): handle other addresses for writes
    printf(";asking for unimplemented %04X in write\n", addr);
}

u16 get_addr(const CPU* cpu, PPU* ppu) {
    // TASK(20260507-214238-186-n6-343): handle page crossed
    switch (*MODE) {
    case IMPL: {
        return 0;
    }
    case ACC: {
        return 0;
    }
    case IMM: {
        return PC;
    }
    case ZP: {
        u16 v = cpu_read(cpu, ppu, PC);
        return v;
    }
    case ZP_X: {
        u16 base = cpu_read(cpu, ppu, PC);
        return (base + cpu->X) & 0xFF;
    }
    case ZP_Y: {
        u16 base = cpu_read(cpu, ppu, PC);
        return (base + cpu->Y) & 0xFF;
    }
    case REL: {
        i8 v = (i8)cpu_read(cpu, ppu, PC);
        return cpu->PC + v;
    }
    case ABS: {
        u8 lo = cpu_read(cpu, ppu, PC);
        u8 hi = cpu_read(cpu, ppu, PC + 1);
        return to16(hi, lo);
    }
    case ABS_X: {
        u8 lo = cpu_read(cpu, ppu, PC);
        u8 hi = cpu_read(cpu, ppu, PC + 1);
        u16 base = to16(hi, lo);
        return base + cpu->X;
    }
    case ABS_Y: {
        u8 lo = cpu_read(cpu, ppu, PC);
        u8 hi = cpu_read(cpu, ppu, PC + 1);
        u16 base = to16(hi, lo);
        return base + cpu->Y;
    }
    case IND: {
        u8 lo = cpu_read(cpu, ppu, PC);
        u8 hi = cpu_read(cpu, ppu, PC + 1);
        u16 base = to16(hi, lo);

        u16 lo_addr = base;
        u16 hi_addr = (base & 0xFF00) | ((base + 1) & 0x00FF);

        u8 lo2 = cpu_read(cpu, ppu, lo_addr);
        u8 hi2 = cpu_read(cpu, ppu, hi_addr);

        return to16(hi2, lo2);
    }
    case IDX_IND: {
        u8 base = cpu_read(cpu, ppu, PC) + cpu->X;

        u8 lo = cpu_read(cpu, ppu, base);
        u8 hi = cpu_read(cpu, ppu, (base + 1) & 0xFF);
        return to16(hi, lo);
    }
    case IND_IDX: {
        u8 zp = cpu_read(cpu, ppu, PC);

        u8 lo = cpu_read(cpu, ppu, zp);
        u8 hi = cpu_read(cpu, ppu, (zp + 1) & 0xFF);
        u16 base = to16(hi, lo) + cpu->Y;
        return base;
    }
    }
}

void inc_pc(CPU* cpu, const Mode* mode) {
    PC = cpu->PC;
    MODE = mode;
    switch (*mode) {
    case IMPL: {
        return;
    }
    case ACC: {
        return;
    }
    case IMM: {
        cpu->PC++;
        return;
    }
    case ZP: {
        cpu->PC++;
        return;
    }
    case ZP_X: {
        cpu->PC++;
        return;
    }
    case ZP_Y: {
        cpu->PC++;
        return;
    }
    case REL: {
        cpu->PC++;
        return;
    }
    case ABS: {
        cpu->PC += 2;
        return;
    }
    case ABS_X: {
        cpu->PC += 2;
        return;
    }
    case ABS_Y: {
        cpu->PC += 2;
        return;
    }
    case IND: {
        cpu->PC += 2;
        return;
    }
    case IDX_IND: {
        cpu->PC++;
        return;
    }
    case IND_IDX: {
        cpu->PC++;
        return;
    }
    }
}

void set_value(CPU* cpu, PPU* ppu, u8 val) {
    switch (*MODE) {
    case IMPL:
        break;
    case ACC:
        cpu->A = val;
        break;
    default: {
        u16 addr = get_addr(cpu, ppu);
        cpu_write(cpu, ppu, addr, val);
    }
    }
}

u8 get_value(CPU* cpu, PPU* ppu) {
    switch (*MODE) {
    case IMPL:
        return 0;
    case ACC:
        return cpu->A;
    default: {
        u16 addr = get_addr(cpu, ppu);
        return cpu_read(cpu, ppu, addr);
    }
    }
}

int cpu_step(CPU* cpu, PPU* ppu) {
    if (cpu->nmi_pending) {
        cpu->nmi_pending = false;

        PUSH(cpu->PC >> 8);
        PUSH(cpu->PC & 0xFF);
        PUSH(cpu->P);
        set_flag_mut(cpu, FLAG_I, true);
        u16 addr =
            cpu_read(cpu, ppu, 0xFFFA) | (cpu_read(cpu, ppu, 0xFFFB) << 8);
        cpu->PC = addr;
        debug("%04X\n", addr);
    }

    u16 opcode_addr = cpu->PC;
    u8 op = cpu_read(cpu, ppu, cpu->PC++); // the raw opcode
    Opcode opcode = instructions[op].opcode;
    Mode mode = instructions[op].mode;
    int cycles = cycles_base[op];
    inc_pc(cpu, &mode); // increments pc by the operand size

    // TASK(20260508-141040-279-n6-549): reorganize this
    switch (opcode) {
    case BRK: {
        u16 addr = cpu->PC + 1;
        PUSH16(addr);
        PUSH(set_flag(cpu->P, FLAG_B, true));
        set_flag_mut(cpu, FLAG_I, true);

        u8 lo = cpu_read(cpu, ppu, 0xFFFE);
        u8 hi = cpu_read(cpu, ppu, 0xFFFF);

        cpu->PC = to16(hi, lo);
        debug("%04X: BRK\n", opcode_addr);
        break;
    }
    case STP: {
        debug("%04X: STP; stopping emulator\n", opcode_addr);
        exit(0);
    }
    case SLO: {
        u8 val = get_value(cpu, ppu);
        debug("%04X: SLO %02X\n", opcode_addr, val);
        set_flag_mut(cpu, FLAG_C, val >> 7);
        val <<= 1;
        set_value(cpu, ppu, val);
        cpu->A |= val;
        set_nz(cpu->A);
        break;
    }
    case NOP: {
        debug("%04X: NOP\n", opcode_addr);
        break;
    }
    case ASL: {
        u8 val = get_value(cpu, ppu);
        debug("%04X: ASL %02X\n", opcode_addr, val);
        set_flag_mut(cpu, FLAG_C, val >> 7);
        val <<= 1;
        set_nz(val);
        set_value(cpu, ppu, val);
        break;
    }
    case ANC: {
        cpu->A &= get_value(cpu, ppu);
        set_flag_mut(cpu, FLAG_C, cpu->A >> 7);
        set_nz(cpu->A);
        debug("%04X: ANC %02X\n", opcode_addr, get_value(cpu, ppu));
        break;
    }
    case JMP: {
        cpu->PC = get_addr(cpu, ppu);
        debug("%04X: JMP %04X\n", opcode_addr, cpu->PC);
        break;
    }
    case JSR: {
        u16 addr = get_addr(cpu, ppu);
        u16 ret = cpu->PC - 1;
        PUSH16(ret);
        cpu->PC = addr;
        debug("%04X: JSR %04X; ret=%04X\n", opcode_addr, addr, ret);
        break;
    }
    case RTI: {
        cpu->P = POP;
        cpu->PC = POP16;
        debug("%04X: RTI; ret=%04X\n", opcode_addr, cpu->PC);
        break;
    }
    case RTS: {
        cpu->PC = POP16 + 1;
        debug("%04X: RTS; ret=%04X\n", opcode_addr, cpu->PC);
        break;
    }
    case SEC: {
        set_flag_mut(cpu, FLAG_C, true);
        debug("%04X: SEC\n", opcode_addr);
        break;
    }
    case CLC: {
        set_flag_mut(cpu, FLAG_C, false);
        debug("%04X: CLC\n", opcode_addr);
        break;
    }
    case CLI: {
        set_flag_mut(cpu, FLAG_I, false);
        debug("%04X: CLI\n", opcode_addr);
        break;
    }
    case SEI: {
        set_flag_mut(cpu, FLAG_I, true);
        debug("%04X: SEI\n", opcode_addr);
        break;
    }
    case CLV: {
        set_flag_mut(cpu, FLAG_V, false);
        debug("%04X: CLV\n", opcode_addr);
        break;
    }
    case CLD: {
        set_flag_mut(cpu, FLAG_D, false);
        debug("%04X: CLD\n", opcode_addr);
        break;
    }
    case SED: {
        set_flag_mut(cpu, FLAG_D, true);
        debug("%04X: SED\n", opcode_addr);
        break;
    }
    case LDA: {
        u8 value = get_value(cpu, ppu);
        set_nz(value);
        cpu->A = value;
        debug("%04X: LDA %02X\n", opcode_addr, value);
        break;
    }
    case LDY: {
        u8 value = get_value(cpu, ppu);
        set_nz(value);
        cpu->Y = value;
        debug("%04X: LDY %02X\n", opcode_addr, value);
        break;
    }
    case LDX: {
        u8 value = get_value(cpu, ppu);
        set_nz(value);
        cpu->X = value;
        debug("%04X: LDX %02X\n", opcode_addr, value);
        break;
    }
    case STA: {
        set_value(cpu, ppu, cpu->A);
        debug("%04X: STA %04X\n", opcode_addr, get_addr(cpu, ppu));
        break;
    }
    case STY: {
        set_value(cpu, ppu, cpu->Y);
        debug("%04X: STY %04X\n", opcode_addr, get_addr(cpu, ppu));
        break;
    }
    case STX: {
        set_value(cpu, ppu, cpu->X);
        debug("%04X: STX %04X\n", opcode_addr, get_addr(cpu, ppu));
        break;
    }
    case TXA: {
        cpu->A = cpu->X;
        set_nz(cpu->A);
        debug("%04X: TXA\n", opcode_addr);
        break;
    }
    case TAX: {
        cpu->X = cpu->A;
        set_nz(cpu->X);
        debug("%04X: TXA\n", opcode_addr);
        break;
    }
    case TYA: {
        cpu->A = cpu->Y;
        set_nz(cpu->A);
        debug("%04X: TYA\n", opcode_addr);
        break;
    }
    case TXS: {
        cpu->S = cpu->X;
        debug("%04X: TXS\n", opcode_addr);
        break;
    }
    case TAS: {
        cpu->S = cpu->A & cpu->X;
        debug("%04X: TAS\n", opcode_addr);
        break;
    }
    case TAY: {
        cpu->Y = cpu->A;
        set_nz(cpu->Y);
        debug("%04X: TAY\n", opcode_addr);
        break;
    }
    case TSX: {
        cpu->X = cpu->S;
        set_nz(cpu->X);
        debug("%04X: TSX\n", opcode_addr);
        break;
    }
    case BPL: {
        u16 addr = get_addr(cpu, ppu);
        debug("%04X: BPL %04X\n", opcode_addr, addr);
        if (!get_flag(cpu, FLAG_N)) {
            cycles++;
            debug(";branched\n");
            cpu->PC = addr;
        }
        break;
    }
    case BCS: {
        u16 addr = get_addr(cpu, ppu);
        debug("%04X: BCS %04X\n", opcode_addr, addr);
        if (get_flag(cpu, FLAG_C)) {
            cycles++;
            debug(";branched\n");
            cpu->PC = addr;
        }
        break;
    }
    case BMI: {
        u16 addr = get_addr(cpu, ppu);
        debug("%04X: BMI %04X\n", opcode_addr, addr);
        if (get_flag(cpu, FLAG_N)) {
            cycles++;
            debug(";branched\n");
            cpu->PC = addr;
        }
        break;
    }
    case BVC: {
        u16 addr = get_addr(cpu, ppu);
        debug("%04X: BVC %04X\n", opcode_addr, addr);
        if (!get_flag(cpu, FLAG_V)) {
            cycles++;
            debug(";branched\n");
            cpu->PC = addr;
        }
        break;
    }
    case BVS: {
        u16 addr = get_addr(cpu, ppu);
        debug("%04X: BVS %04X\n", opcode_addr, addr);
        if (get_flag(cpu, FLAG_V)) {
            cycles++;
            debug(";branched\n");
            cpu->PC = addr;
        }
        break;
    }
    case BCC: {
        u16 addr = get_addr(cpu, ppu);
        debug("%04X: BCC %04X\n", opcode_addr, addr);
        if (!get_flag(cpu, FLAG_C)) {
            cycles++;
            debug(";branched\n");
            cpu->PC = addr;
        }
        break;
    }
    case BNE: {
        u16 addr = get_addr(cpu, ppu);
        debug("%04X: BNE %04X\n", opcode_addr, addr);
        if (!get_flag(cpu, FLAG_Z)) {
            cycles++;
            debug(";branched\n");
            cpu->PC = addr;
        }
        break;
    }
    case BEQ: {
        u16 addr = get_addr(cpu, ppu);
        debug("%04X: BEQ %04X\n", opcode_addr, addr);
        if (get_flag(cpu, FLAG_Z)) {
            cycles++;
            debug(";branched\n");
            cpu->PC = addr;
        }
        break;
    }
    case DEX: {
        cpu->X--;
        set_nz(cpu->X);
        debug("%04X: DEX\n", opcode_addr);
        break;
    }
    case INX: {
        cpu->X++;
        set_nz(cpu->X);
        debug("%04X: INX\n", opcode_addr);
        break;
    }
    case INY: {
        cpu->Y++;
        set_nz(cpu->Y);
        debug("%04X: INY\n", opcode_addr);
        break;
    }
    case DEY: {
        cpu->Y--;
        set_nz(cpu->Y);
        debug("%04X: DEY\n", opcode_addr);
        break;
    }
    case INC: {
        u8 v = get_value(cpu, ppu);
        v++;
        set_nz(v);
        set_value(cpu, ppu, v);
        debug("%04X: INC %04X\n", opcode_addr, get_addr(cpu, ppu));
        break;
    }
    case DEC: {
        u8 v = get_value(cpu, ppu);
        v--;
        set_nz(v);
        set_value(cpu, ppu, v);
        debug("%04X: DEC %04X\n", opcode_addr, get_addr(cpu, ppu));
        break;
    }
    case CMP: {
        u8 value = get_value(cpu, ppu);
        u8 res = cpu->A - value;
        set_flag_mut(cpu, FLAG_C, cpu->A >= value);
        set_nz(res); // sets n and z
        debug("%04X: CMP %02X\n", opcode_addr, value);
        break;
    }
    case CPX: {
        u8 value = get_value(cpu, ppu);
        u8 res = cpu->X - value;
        set_flag_mut(cpu, FLAG_C, cpu->X >= value);
        set_nz(res); // sets n and z
        debug("%04X: CPX %02X\n", opcode_addr, value);
        break;
    }
    case CPY: {
        u8 value = get_value(cpu, ppu);
        u8 res = cpu->Y - value;
        set_flag_mut(cpu, FLAG_C, cpu->Y >= value);
        set_nz(res); // sets n and z
        debug("%04X: CPY %02X\n", opcode_addr, value);
        break;
    }
    case BIT: {
        u8 value = get_value(cpu, ppu);

        u8 result = cpu->A & value;

        set_flag_mut(cpu, FLAG_V, value & 0x40);
        set_nz(value);

        debug("%04X: BIT\n", opcode_addr);
        break;
    }
    case ORA: {
        u8 val = get_value(cpu, ppu);

        cpu->A |= val;

        set_nz(cpu->A);

        debug("%04X: ORA %02X\n", opcode_addr, val);
        break;
    }
    case AND: {
        u8 value = get_value(cpu, ppu);

        cpu->A = cpu->A & value;

        set_nz(cpu->A);

        debug("%04X: AND %02X\n", opcode_addr, value);
        break;
    }
    case EOR: {
        u8 value = get_value(cpu, ppu);

        cpu->A = cpu->A ^ value;

        set_nz(cpu->A);

        debug("%04X: EOR\n", opcode_addr);
        break;
    }
    case LSR: {
        u8 value = get_value(cpu, ppu);

        set_flag_mut(cpu, FLAG_C, value & 0x01);

        value >>= 1;
        value &= 0x7F;

        set_value(cpu, ppu, value);

        set_flag_mut(cpu, FLAG_Z, value == 0);
        set_flag_mut(cpu, FLAG_N, 0);

        debug("%04X: LSR %04X\n", opcode_addr, value);
        break;
    }
    case ROL: {
        u8 value = get_value(cpu, ppu);

        u8 old_c = get_flag(cpu, FLAG_C);

        set_flag_mut(cpu, FLAG_C, value >> 7);

        value = (value << 1) | old_c;

        set_value(cpu, ppu, value);

        set_nz(value);

        debug("%04X: ROL %04X\n", opcode_addr, value);

        break;
    }
    case ROR: {
        u8 value = get_value(cpu, ppu);

        u8 old_c = get_flag(cpu, FLAG_C);

        set_flag_mut(cpu, FLAG_C, value & 0x01);

        value = (value >> 1) | (old_c << 7);

        set_value(cpu, ppu, value);

        set_nz(value);

        debug("%04X: ROR %04X\n", opcode_addr, value);
        break;
    }
    case PHP: {
        PUSH(set_flag(cpu->P, FLAG_B, true));
        debug("%04X: PHP\n", opcode_addr);
        break;
    }
    case PHA: {
        PUSH(cpu->A);
        debug("%04X: PHA\n", opcode_addr);
        break;
    }
    case PLA: {
        cpu->A = POP;
        set_nz(cpu->A);
        debug("%04X: PLA\n", opcode_addr);
        break;
    }
    case PLP: {
        cpu->P = POP;
        debug("%04X: PLP\n", opcode_addr);
        break;
    }
    case SBC: {
        u8 value = get_value(cpu, ppu) ^ 0xFF; // invert

        u16 sum = cpu->A + value + get_flag(cpu, FLAG_C);

        set_flag_mut(cpu, FLAG_C, sum > 0xFF);

        u8 result = sum & 0xFF;

        set_nz(result);

        set_flag_mut(cpu, FLAG_V, (~(cpu->A ^ value) & (cpu->A ^ result) >> 7));

        cpu->A = result;

        debug("%04X: SBC %02X\n", opcode_addr, value);
        break;
    }
    case ADC: {
        u8 value = get_value(cpu, ppu);

        u16 sum = cpu->A + value + get_flag(cpu, FLAG_C);

        set_flag_mut(cpu, FLAG_C, sum > 0xFF);

        u8 result = sum & 0xFF;

        set_nz(result);
        set_flag_mut(cpu, FLAG_V, (~(cpu->A ^ value) & (cpu->A ^ result) >> 7));

        cpu->A = result;

        debug("%04X: ADC %02X\n", opcode_addr, value);

        break;
    }

    case RLA: {
    }
    case SRE: {
    }
    case ALR: {
    }
    case RRA: {
    }
    case ARR: {
    }
    case SAX: {
    }
    case XAA: {
    }
    case AHX: {
    }
    case SHY: {
    }
    case SHX: {
    }
    case LAX: {
    }
    case LAS: {
    }
    case DCP: {
    }
    case AXS: {
    }
    case ISC: {
    }
    default:
#ifdef DEBUG_BUILD
        if (opcode < opcode_count) {
            panic("%04X: %s; not implemented\n", opcode_addr,
                  opcode_list[opcode]);
        } else {
            panic("%04X: %02X; tf did you do!?\n", opcode_addr,
                  opcode); // this should literally never happen
        }
#else
        panic("%04X: %02X; not implemented\n", opcode_addr, opcode);
#endif
    }

    return cycles;
}
