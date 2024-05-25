#include <termios.h>
#include <time.h>

typedef struct erow
{
    int idx;
    int size;
    int rsize;
    char *chars;
    char *render;
    unsigned char *hl;
    int hl_open_comment;
} erow;

enum mode
{
    NORMAL,
    INSERT,
    VISUAL
};

struct config
{
    int cx, cy, rx;
    int rowOff, colOff;
    int screenRws, screenCls;
    int numRws;
    erow *row;

    int dirty;

    enum mode current_mode;

    char *current_file_extension;
    char *current_file_name;

    char statusmsg[80];
    time_t statusmsg_time;

    struct syntax *syntax;
    struct termios orig_termios;
};

struct abuf
{
    char *b;
    int len;
};

void abAppend(struct abuf *ab, const char *s, int len);
