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
#define POP16 to16lh(POP, POP)

#define to16hl(hi, lo) ((hi) << 8) | (lo)
#define to16lh(lo, hi) ((hi) << 8) | (lo)

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
        return PC + 1;
    }
    case ZP: {
        u16 v = cpu_read(cpu, ppu, PC + 1);
        return v;
    }
    case ZP_X: {
        u16 base = cpu_read(cpu, ppu, PC + 1);
        return (base + cpu->X) & 0xFF;
    }
    case ZP_Y: {
        u16 base = cpu_read(cpu, ppu, PC + 1);
        return (base + cpu->Y) & 0xFF;
    }
    case REL: {
        i8 v = (i8)cpu_read(cpu, ppu, PC + 1);
        return cpu->PC + v;
    }
    case ABS: {
        u8 lo = cpu_read(cpu, ppu, PC + 1);
        u8 hi = cpu_read(cpu, ppu, PC + 2);
        return to16hl(hi, lo);
    }
    case ABS_X: {
        u8 lo = cpu_read(cpu, ppu, PC + 1);
        u8 hi = cpu_read(cpu, ppu, PC + 2);
        u16 base = to16hl(hi, lo);
        return base + cpu->X;
    }
    case ABS_Y: {
        u8 lo = cpu_read(cpu, ppu, PC + 1);
        u8 hi = cpu_read(cpu, ppu, PC + 2);
        u16 base = to16hl(hi, lo);
        return base + cpu->Y;
    }
    case IND: {
        u8 lo = cpu_read(cpu, ppu, PC + 1);
        u8 hi = cpu_read(cpu, ppu, PC + 2);
        u16 base = to16hl(hi, lo);

        u16 lo_addr = base;
        u16 hi_addr = (base & 0xFF00) | ((base + 1) & 0x00FF);

        u8 lo2 = cpu_read(cpu, ppu, lo_addr);
        u8 hi2 = cpu_read(cpu, ppu, hi_addr);

        return to16hl(hi2, lo2);
    }
    case IDX_IND: {
        u8 base = cpu_read(cpu, ppu, PC + 1) + cpu->X;

        u8 lo = cpu_read(cpu, ppu, base);
        u8 hi = cpu_read(cpu, ppu, (base + 1) & 0xFF);
        return to16hl(hi, lo);
    }
    case IND_IDX: {
        u8 zp = cpu_read(cpu, ppu, PC + 1);

        u8 lo = cpu_read(cpu, ppu, zp);
        u8 hi = cpu_read(cpu, ppu, (zp + 1) & 0xFF);
        u16 base = to16hl(hi, lo) + cpu->Y;
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

    //TASK(20260508-141040-279-n6-549): reorganize this
    switch (opcode) {
    case BRK: {
        u16 addr = cpu->PC + 1;
        PUSH16(addr);
        PUSH(set_flag(cpu->P, FLAG_B, true));
        set_flag_mut(cpu, FLAG_I, true);

        uint8_t lo = cpu_read(cpu, ppu, 0xFFFE);
        uint8_t hi = cpu_read(cpu, ppu, 0xFFFF);

        cpu->PC = to16hl(hi, lo);
        debug("%04X: BRK\n", opcode_addr);
        break;
    }
    case ORA: {
        u8 val = get_value(cpu, ppu);

        cpu->A |= val;

        set_flag_mut(cpu, FLAG_Z, cpu->A == 0);
        set_flag_mut(cpu, FLAG_N, cpu->A >> 7);

        debug("%04X: ORA %02X\n", opcode_addr, val);
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
        set_flag_mut(cpu, FLAG_Z, cpu->A == 0);
        set_flag_mut(cpu, FLAG_N, cpu->A & 0x80);
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
        set_flag_mut(cpu, FLAG_Z, val == 0);
        set_flag_mut(cpu, FLAG_N, val >> 7);
        set_value(cpu, ppu, val);
        break;
    }
    case PHP: {
        PUSH(set_flag(cpu->P, FLAG_B, true));
        debug("%04X: PHP\n", opcode_addr);
        break;
    }
    case ANC: {
        cpu->A &= get_value(cpu, ppu);
        set_flag_mut(cpu, FLAG_C, cpu->A >> 7);
        set_flag_mut(cpu, FLAG_Z, cpu->A == 0);
        set_flag_mut(cpu, FLAG_N, cpu->A >> 7);
        debug("%04X: ANC %02X\n", opcode_addr, get_value(cpu, ppu));
        break;
    }
    case BPL: {
        i8 offset = (i8)get_value(cpu, ppu);
        debug("%04X: BPL %d\n", opcode_addr, offset);
        if (!get_flag(cpu, FLAG_N)) {
            cycles++;
            if (page_crossed(cpu->PC, cpu->PC + offset)) {
                cycles++;
            }
            debug(";branched\n");
            cpu->PC += offset;
        }
        break;
    }
    case CLC: {
        set_flag_mut(cpu, FLAG_C, false);
        debug("%04X: CLC\n", opcode_addr);
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
    case SEC: {
        set_flag_mut(cpu, FLAG_C, true);
        debug("%04X: SEC\n", opcode_addr);
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
    case AND: {
    }
    case RLA: {
    }
    case BIT: {
    }
    case ROL: {
    }
    case PLP: {
    }
    case BMI: {
    }
    case RTI: {
    }
    case EOR: {
    }
    case SRE: {
    }
    case LSR: {
    }
    case PHA: {
    }
    case ALR: {
    }
    case JMP: {
    }
    case BVC: {
    }
    case RTS: {
    }
    case ADC: {
    }
    case RRA: {
    }
    case ROR: {
    }
    case PLA: {
    }
    case ARR: {
    }
    case BVS: {
    }
    case STA: {
    }
    case SAX: {
    }
    case STY: {
    }
    case STX: {
    }
    case DEY: {
    }
    case TXA: {
    }
    case XAA: {
    }
    case BCC: {
    }
    case AHX: {
    }
    case TYA: {
    }
    case TXS: {
    }
    case TAS: {
    }
    case SHY: {
    }
    case SHX: {
    }
    case LDY: {
    }
    case LDA: {

    }
    case LDX: {
    }
    case LAX: {
    }
    case TAY: {
    }
    case TAX: {
    }
    case BCS: {
    }
    case TSX: {
    }
    case LAS: {
    }
    case CPY: {
    }
    case CMP: {
    }
    case DCP: {
    }
    case DEC: {
    }
    case INY: {
    }
    case DEX: {
    }
    case AXS: {
    }
    case BNE: {
    }
    case CPX: {
    }
    case SBC: {
    }
    case ISC: {
    }
    case INC: {
    }
    case INX: {
    }
    case BEQ: {
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
