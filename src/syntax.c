#include <stdio.h>

struct syntax
{
    char *filetype;
    char **filematch;

    char **keywords;
    char *singleline_comment_start;
    char *multiline_comment_start;
    char *multiline_comment_end;

    int flags;
};

// data
#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)

// filetypes
char *C_HL_extensions[] = {".c", ".h", ".cpp"};

char *C_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case",

    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", "struct|", "enum|", "const|", "#define|", "#include|", NULL};

struct syntax HLDB[] = {
    {"c",
     C_HL_extensions,
     C_HL_keywords,
     "//",
     "/*", "*/",
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

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
