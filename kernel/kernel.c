#include <stdint.h>

void kmain(void) {

    volatile uint16_t* VGA = (uint16_t*)0xB8000;

    VGA[0] = 0x0F41; // 'A'
    VGA[1] = 0x0F4C; // 'L'
    VGA[2] = 0x0F49; // 'I'
    VGA[3] = 0x0F4F; // 'O'
    VGA[4] = 0x0F53; // 'S'

    for(;;){}
}
