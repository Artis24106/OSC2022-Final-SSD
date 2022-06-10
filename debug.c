#include "debug.h"

#include "stdio.h"

void show_L2P() {
    printf(GREEN "show_L2P()\n");
    for (int i = 0; i < 10; i++) {
        printf("- [%d] ", i);
        for (int j = 0; j < 10; j++) {
            printf("0x%x, ", L2P[i * j]);
        }
        printf(NC);
    }
}