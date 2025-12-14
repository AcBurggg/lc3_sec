// NOT MY OWN WORK. BASED OFF TUTORIAL HERE https://www.jmeiners.com/lc3-vm/index.html.
// FOLLOWING ALONG FOR FUN/EDUCATION AND (maybe) ADDING MY OWN TRAPS / STUFF LATER

#include <stdio.h>
#include <stdint.h>
#include <signal.h>
/* unix only */
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

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

enum TrapVectors {
    TRAP_GETC = 0x20,
    TRAP_OUT = 0x21, 
    TRAP_PUTS = 0x22,
    TRAP_IN = 0x23,
    TRAP_PUTSP = 0x24,
    TRAP_HALT = 0x25
};

void flush_input_buffer()
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

//used for endianness
uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

void read_image_file(FILE* file)
{
    /* the origin tells us where in memory to place the image */
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    /* we know the maximum file size so we only need one fread */
    uint16_t max_read = MEMORY_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    /* swap to little endian */
    while (read-- > 0)
    {
        *p = swap16(*p);
        ++p;
    }
}

int read_image(const char* image_path)
{
    FILE* file = fopen(image_path, "rb");
    if (!file) { return 0; };
    read_image_file(file);
    fclose(file);
    return 1;
}


struct termios original_tio;

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

uint16_t check_key()
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}
void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

//TO-DO: add DSR and DDR 
enum MMIO {
    KBSR = 0xFE00,
    KBDR = 0xFE02
};

void mem_write(uint16_t addr, uint16_t val) {
    memory[addr] = val;
}

uint16_t mem_read(uint16_t addr) {
    if (addr == KBDR) {
        if (check_key()) {
            memory[KBSR] = (1 << 15);
            memory[KBDR] = getchar();
        } else {
            memory[KBSR] = 0;
        }
    }
    return memory[addr];
}



/// @brief sets the condition codes register
/// @param busValue the actual value on the bus 
void set_condition_codes(uint16_t busValue) {
    if (busValue == 0) {
        reg[R_COND] = ZERO;
    } else if ((busValue >> 15) & 0x1) {
        reg[R_COND] = NEG;
    } else {
        reg[R_COND] = POS;
    }
}

/// @brief sign extends to 16 bits
/// @param val the value getting sext'd
/// @param bitsize its current bit size
/// @return 
// uint16_t sign_extend(uint16_t val, int bitsize) {
//     uint16_t msb = (val >> (bitsize - 1)) & 0x1;
//     if (msb) {
//         //0xffff is enough to ensure it works with any valid bitsize
//         return val | (0xFFFF << bitsize);
//     } else { 
//         return val;
//     }
// }
uint16_t sign_extend(uint16_t x, int bit_count)
{
    if ((x >> (bit_count - 1)) & 1) { // Check if the MSB of the field is 1
        x |= (0xFFFF << bit_count); // If it is, OR it with a mask of all 1s above that bit
    }
    return x;
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

    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

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
            case OP_ADD: {
                //determine mode
                uint16_t mode = (instr >> 5) & 0x1;
                switch(mode) {
                    case 0: {
                        //two registers
                        uint16_t sr1 = (instr >> 6) & 0x7;
                        uint16_t sr2 = instr & 0x7;
                        uint16_t dr = (instr >> 9) & 0x7;

                        reg[dr] = reg[sr1] + reg[sr2];
                        set_condition_codes(reg[dr]);
                        break;
                    }
                    case 1: {
                        //imm5
                        uint16_t sr1 = (instr >> 6) & 0x7;
                        uint16_t dr = (instr >> 9) & 0x7;
                        reg[dr] = reg[sr1] + sign_extend((instr & 0x1F), 5);
                        set_condition_codes(reg[dr]);
                        break;
                    }
                    default:
                        printf("bad ADD instruction at %d\n", reg[R_PC]);
                        break;
                }
                break;
            }
            case OP_AND: {
                uint16_t mode = (instr >> 5) & 0x1;
                switch(mode) {
                    case 0: {
                        //two registers
                        uint16_t sr1 = (instr >> 6) & 0x7;
                        uint16_t sr2 = instr & 0x7;
                        uint16_t dr = (instr >> 9) & 0x7;

                        reg[dr] = reg[sr1] & reg[sr2];
                        set_condition_codes(reg[dr]);
                        break;
                    }
                    case 1: {
                        //imm5
                        uint16_t sr1 = (instr >> 6) & 0x7;
                        uint16_t dr = (instr >> 9) & 0x7;
                        reg[dr] = reg[sr1] & sign_extend((instr & 0x1F), 5);
                        set_condition_codes(reg[dr]);
                        break;
                    }
                    default:
                        printf("bad AND instruction at %d\n", reg[R_PC]);
                        break;
                }
                break;
            }
            case OP_NOT: {
                //bitwise invert the contents of the SR1 and put in DR
                uint16_t sr1 = (instr >> 6) & 0x7;
                uint16_t dr = (instr >> 9) & 0x7;
                
                reg[dr] = ~reg[sr1];
                set_condition_codes(reg[dr]);
                break;
            }
            case OP_BR: {
                //need to set PC to be current value plus SEXT(off9) ONLY if NZP config checks out 
                uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);
                uint16_t nzp = (instr >> 9) & 0x7;
                
                if ((nzp & reg[R_COND]) != 0) {
                    //we have a match
                    reg[R_PC] += pc_offset9;
                }
                break;
            }
            case OP_JMP: {
                uint16_t baseR = (instr >> 6) & 0x7;
                reg[R_PC] = reg[baseR];
                break;
            }
            case OP_JSR: {
                //R7 = PC
                // if bit 11 == 0, then PC = BaseR. Else = PC* + SEXT(PCoffset11)
                uint16_t mode = (instr >> 11) & 0x1;
                reg[R_R7] = reg[R_PC];
                if (mode) {
                    //pcoffset
                    uint16_t pc_offset11 = sign_extend(instr & 0x7FF, 11);
                    reg[R_PC] += pc_offset11;
                } else {
                    //baseR
                    uint16_t baseR = (instr >> 6) & 0x7;
                    reg[R_PC] = reg[baseR];
                }
                break;
            }
            case OP_LD: {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);

                reg[dr] = mem_read(reg[R_PC] + pc_offset9);

                set_condition_codes(reg[dr]);
                break;
            }
            case OP_LDI: {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);
                reg[dr] = mem_read(mem_read(reg[R_PC] + pc_offset9));
                set_condition_codes(reg[dr]);
                break;
            }
            case OP_LDR: {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t baseR = (instr >> 6) & 0x7;
                uint16_t pc_offset6 = sign_extend(instr & 0x3F, 6);

                reg[dr] = mem_read(reg[baseR] + pc_offset6);
                set_condition_codes(reg[dr]);
                break;
            }
            case OP_LEA: {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);

                reg[dr] = reg[R_PC] + pc_offset9;
                break;
            }
            case OP_ST: {
                uint16_t sr = (instr >> 9) & 0x7;
                uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);

                mem_write(reg[R_PC] + pc_offset9, reg[sr]);
                break;
            }
            case OP_STI: {
                uint16_t sr = (instr >> 9) & 0x7;
                uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);

                mem_write(mem_read(reg[R_PC] + pc_offset9), reg[sr]);
                break;
            }
            case OP_STR: {
                uint16_t sr = (instr >> 9) & 0x7;
                uint16_t baseR = (instr >> 6) & 0x7;
                uint16_t pc_offset6 = sign_extend(instr & 0x3F, 6);

                mem_write(reg[baseR] + pc_offset6, reg[sr]);
                break;
            }
            case OP_TRAP: {
                // currently just handle all traps here based on known vector. might add TVT / IVT to memory later if bored
                // reg[R_R7] = reg[R_PC];
                //note how we aren't changing PC at all actually but to virtualize exact behavior we must overwrite R7 still 
                //if I add TVT and assembled binary in system memory we can actually do it 
                //...honestly this would just be for shits n gigs - VM shouldn't do it how real computer would
                
                // uint16_t trapvect8 = instr & 0xFF;
                // switch(trapvect8) {
                //     case TRAP_GETC: {
                //         //takes a character from stdin and stores it in R0
                //         reg[R_R0] = (uint16_t)getchar();
                //         set_condition_codes(reg[R_R0]);
                //         break;
                //     }
                //     case TRAP_OUT: {
                //         //print char in R0 to console
                //         putchar((char)reg[R_R0]); //DO WE NEED TO CAST TO CHAR WITH putchar or is uint16 chill
                //         fflush(stdout);
                //         break;
                //     }
                //     case TRAP_PUTS: {
                //         //print a string of characters, starting at the address in R0 and ending at a null terminator
                //         //can use putchar()
                        
                //         uint16_t* currAddr = memory + reg[R_R0];
                //         while (*currAddr != 0) {
                //             putchar((char)*currAddr); //ditto from out casting question
                //             currAddr++;
                //         }
                //         fflush(stdout);
                //         break;
                //     }
                //     case TRAP_IN: {
                //         //gets a character into R0 getc BUT also echoes to the terminal
                //         reg[R_R0] = (uint16_t)getchar();
                //         putchar((char)reg[R_R0]);
                //         fflush(stdout);
                //         set_condition_codes(reg[R_R0]);
                //         break;
                //     }
                //     case TRAP_HALT: {
                //         running = 0;
                //         break;
                //     }
                // }
                // break;
                reg[R_R7] = reg[R_PC];
                
                switch (instr & 0xFF)
                {
                    case TRAP_GETC:
                        /* read a single ASCII char */
                        reg[R_R0] = (uint16_t)getchar();
                        set_condition_codes(reg[R_R0]);
                        break;
                    case TRAP_OUT:
                        putc((char)reg[R_R0], stdout);
                        fflush(stdout);
                        break;
                    case TRAP_PUTS:
                        {
                            /* one char per word */
                            uint16_t* c = memory + reg[R_R0];
                            while (*c)
                            {
                                putc((char)*c, stdout);
                                ++c;
                            }
                            fflush(stdout);
                        }
                        break;
                    case TRAP_IN:
                        {
                            printf("Enter a character: ");
                            char c = getchar();
                            putc(c, stdout);
                            fflush(stdout);
                            reg[R_R0] = (uint16_t)c;
                            set_condition_codes(reg[R_R0]);

                            flush_input_buffer();
                        }
                        break;
                    case TRAP_PUTSP:
                        {
                            /* one char per byte (two bytes per word)
                               here we need to swap back to
                               big endian format */
                            uint16_t* c = memory + reg[R_R0];
                            while (*c)
                            {
                                char char1 = (*c) & 0xFF;
                                putc(char1, stdout);
                                char char2 = (*c) >> 8;
                                if (char2) putc(char2, stdout);
                                ++c;
                            }
                            fflush(stdout);
                        }
                        break;
                    case TRAP_HALT:
                        puts("HALT");
                        fflush(stdout);
                        running = 0;
                        break;
                }
                break;
            }
            case OP_RES:
            case OP_RTI:
            default:
                exit(1);
                break;
        }

        //execute
    }
    restore_input_buffering();
}