#include <stdio.h>
#include <stdint.h>
#include <signal.h>
/* unix only */
// #include <stdlib.h>
// #include <unistd.h>
// #include <fcntl.h>
// #include <sys/time.h>
// #include <sys/types.h>
// #include <sys/termios.h>
// #include <sys/mman.h>

#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX];  /* 65536 locations */

enum Registers
{
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC, /* program counter */
    R_COND,
    R_COUNT
};
uint16_t reg[R_COUNT];

//cannot just use regular enumeration here since its one-hot not binary 
enum CondCodes
{
    POS = 1 << 0, /* P */
    ZERO = 1 << 1, /* Z */
    NEG = 1 << 2, /* N */
};

//must be in numerical order not the order presented in ISA/reference sheet (branch, add, etc.)
enum Opcodes
{
    OP_BR = 0, /* branch */
    OP_ADD,    /* add  */
    OP_LD,     /* load */
    OP_ST,     /* store */
    OP_JSR,    /* jump register */
    OP_AND,    /* bitwise and */
    OP_LDR,    /* load register */
    OP_STR,    /* store register */
    OP_RTI,    /* unused */
    OP_NOT,    /* bitwise not */
    OP_LDI,    /* load indirect */
    OP_STI,    /* store indirect */
    OP_JMP,    /* jump */
    OP_RES,    /* reserved (unused) */
    OP_LEA,    /* load effective address */
    OP_TRAP    /* execute trap */
};

/// @brief sets the condition codes register
/// @param busValue the actual value on the bus 
void set_condition_codes(uint16_t busValue) {
    if (busValue < 0) {
        reg[R_COND] = NEG;
    } else if (busValue < 0) {
        reg[R_COND] = ZERO;
    } else {
        reg[R_COND] = POS;
    }
}

/// @brief sign extends to 16 bits
/// @param val the value getting sext'd
/// @param bitsize its current bit size
/// @return 
uint16_t sign_extend(uint16_t val, int bitsize) {
    uint16_t msb = (val >> (bitsize - 1)) & 0x1;
    if (msb) {
        //0xffff is enough to ensure it works with any valid bitsize
        return val | (0xFFFF << bitsize);
    } else { 
        return val;
    }
}

int main(int argc, const char* argv[]) {
    if (argc < 2) {
        printf("args aint right brotato chip\n");
        exit(2);
    }

    for (int i = 1; i < argc; ++i){
        if (!read_image(argv[i])){
            printf("image wasn't read: %s\n", argv[i]);
            exit(1);
        }
    }


    reg[R_COND] = ZERO;

    enum
    { 
        PC_START = 0x3000 
    };
    reg[R_PC] = PC_START;

    int running = 1;

    while(running) {
        //fetch
        uint16_t instr = mem_read(reg[R_PC]);
        reg[R_PC]++;
        uint16_t op = instr >> 12;

        //decode and execute
        switch(op)
        {
            case OP_ADD:
                //determine mode
                uint16_t mode = (instr >> 5) & 0x1;
                switch(mode) {
                    case 0:
                        //two registers
                        uint16_t sr1 = (instr >> 6) & 0x7;
                        uint16_t sr2 = instr & 0x7;
                        uint16_t dr = (instr >> 9) & 0x7;

                        reg[dr] = reg[sr1] + reg[sr2];
                        set_condition_codes(reg[dr]);
                        break;
                    case 1:
                        //imm5
                        reg[dr] = reg[sr1] + sign_extend((instr & 0x1F), 5);
                        set_condition_codes(reg[dr]);
                        break;
                    default:
                        printf("bad add instruction at %d\n", reg[R_PC]);
                        break;
                }
                
                break;
            case OP_AND:
                //
                break;
            case OP_NOT:
                //bitwise invert the contents of the SR1 and put in DR
                // uint16_t sr1 = (instr & (uint16_t)0x01C0) >> 6;
                uint16_t sr1 = (instr >> 6) & 0x7;
                uint16_t dr = (instr >> 9) & 0x7;
                
                reg[dr] = ~reg[sr1];
                set_condition_codes(reg[dr]);
                break;
            case OP_BR:
                //
                break;
            case OP_JMP:
                //
                break;
            case OP_JSR:
                //
                break;
            case OP_LD:
                //
                break;
            case OP_LDI:
                //
                break;
            case OP_LDR:
                //
                break;
            case OP_LEA:
                //
                break;
            case OP_ST:
                //
                break;
            case OP_STI:
                //
                break;
            case OP_STR:
                //
                break;
            case OP_TRAP:
                //
                break;
            case OP_RES:
            case OP_RTI:
            default:
                //bad opcode
                break;
        }

        //execute
    }
}