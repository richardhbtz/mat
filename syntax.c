#include <stdio.h>

const char *hexToAnsiBackground(const char *hex)
{
    static char ansi[20];
    int r, g, b;
    sscanf(hex, "#%02x%02x%02x", &r, &g, &b);
    snprintf(ansi, sizeof(ansi), "\033[38;2;%d;%d;%dm", r, g, b);
    return ansi;
}

const char *hexToAnsiFore(const char *hex)
{
    static char ansi[20];
    int r, g, b;
    sscanf(hex, "#%02x%02x%02x", &r, &g, &b);
    snprintf(ansi, sizeof(ansi), "\x1b[48;2;%d;%d;%dm", r, g, b);
    return ansi;
}

void resetForeground()
{
    printf("\x1b[39m");
}

void resetBackground()
{
    printf("\x1b[49m");
}
