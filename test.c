#include <stdio.h>
#include <stdint.h>


uint16_t sign_extend(uint16_t val, int bitsize) {
    uint16_t msb = (val >> (bitsize - 1)) & 0x1;
    printf("mode is a %d\n", msb);
    if (msb) {
        //0xffff is enough to ensure it works with any valid bitsize
        return val | (0xFFFF << bitsize);
    } else { 
        return val;
    }
}

int main() {
    uint16_t instr = 0x190F;

    uint16_t imm5 = instr & 0x1F; 

    printf("preshift: %x\n",imm5);
    
    imm5 = sign_extend(imm5, 5);

    printf("postshift: %x\n",imm5);
    
}