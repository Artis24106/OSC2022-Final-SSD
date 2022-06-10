#ifndef __DEBUG_H__
#define __DEBUG_H__

#define RED "\x1b[38;5;1m[+] "
#define GREEN "\x1b[38;5;2m[+] "
#define YELLOW "\x1b[38;5;3m[+] "
#define BLUE "\x1b[38;5;4m[+] "
#define NC "\x1b[0m\n"
#define NC2 "\x1b[0m"

extern unsigned int *L2P, *P2L;

void show_L2P() {
    printf(BLUE "show_L2P()\n");
    char* t_red = "\x1b[38;5;1m";
    char* t_blue = "\x1b[38;5;4m";
    unsigned int pca;
    for (int i = 0; i < 10; i++) {
        printf("- [0x%x] ", i);
        for (int j = 0; j < 10; j++) {
            pca = L2P[10 * i + j];
            if (pca == 0xffffffff) {
                printf("%sINVALID%s, ", t_red, t_blue);
            } else {
                printf("0x%x/0x%x, ", pca >> 16, pca & 0xffff);
            }
        }
        printf("\n");
    }
    printf(NC);
}

void show_P2L() {
    printf(BLUE "show_P2L()\n");
    char* t_red = "\x1b[38;5;1m";
    char* t_yellow = "\x1b[38;5;3m";
    char* t_blue = "\x1b[38;5;4m";
    unsigned int pca;
    for (int i = 0; i < 13; i++) {
        printf("- [0x%x] ", i);
        for (int j = 0; j < 10; j++) {
            pca = P2L[10 * i + j];
            if (pca == 0xffffffff) {
                printf("%sINVALID%s, ", t_red, t_blue);
            } else if (pca == 0xfffffffd) {
                printf("%sNOT_USE%s, ", t_yellow, t_blue);
            } else {
                printf("0x%x/0x%x, ", pca / 10, pca % 10);
            }
        }
        printf("\n");
    }
    printf(NC);
}
#endif