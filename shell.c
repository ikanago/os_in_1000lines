#include "user.h"

void main(void) {
    *((volatile int *)0x80200000) = 0xdeadbeef;
    for (;;);
}
