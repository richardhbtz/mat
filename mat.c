// includes
#include <ctype.h>
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
// asoidjasodijasoidjaoisdjqa
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/types.h>

// defines
#define CTRL_KEY(k) ((k) & 0x1f)

#define KEY_ESC 27
#define ESC_K -1

#define MAT_VERSION "0.0.1"

// globals
int MAT_TABSTOP = 4;

char *current_file_extension;
char *current_filename;

enum mode
{
    NORMAL,
    INSERT,
    VISUAL
};

enum key
{
    BACKSPACE = 127,

    KEY_SLASH = '/',
    KEY_K = 'k',
    KEY_J = 'j',
    KEY_L = 'l',
    KEY_H = 'h',

    KEY_I = 'i',
    KEY_A = 'a',
    KEY_V = 'v',

    KEY_X = 'x',

    KEY_Q = 'q',
};

// data
typedef struct erow
{
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;

struct config
{
    int cx, cy, rx;
    int rowOff, colOff;
    int screenRws, screenCls;
    int numRws;
    erow *row;

    int dirty;

    enum mode current_mode;

    char statusmsg[80];
    time_t statusmsg_time;

    struct termios orig_termios;
};

struct config E;

// proto
int readKey();
void setStatusMessage(const char *fmt, ...);
void refreshScreen();
char *prompt(char *prompt, void (*callback)(char *, int));

// terminal
void die(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");

    atexit(disableRawMode);

    struct termios raw = E.orig_termios;

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

int getWindowSize(int *rws, int *cls)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        return -1;
    }
    else
    {
        *rws = ws.ws_row;
        *cls = ws.ws_col;
        return 0;
    }
}

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;
    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
    }
    buf[i] = '\0';
    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;
    return 0;
}

int rwsRxToCx(erow *row, int rx)
{
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++)
    {
        if (row->chars[cx] == '\t')
            cur_rx += (MAT_TABSTOP - 1) - (cur_rx % MAT_TABSTOP);

        cur_rx++;
        if (cur_rx > rx)
            return cx;
    }
    return cx;
}

int rwsCxToRx(erow *row, int cx)
{
    int rx = 0;
    for (int j = 0; j < cx; j++)
    {
        if (row->chars[j] == '\t')
        {
            rx += (MAT_TABSTOP - 1) - (rx % MAT_TABSTOP);
        }
        rx++;
    }
    return rx;
}

void updateRws(erow *row)
{
    int tabs = 0;

    int j;

    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t')
            tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs * (MAT_TABSTOP - 1) + 1);

    int idx = 0;

    for (j = 0; j < row->size; j++)
    {

        if (row->chars[j] == '\t')
        {
            row->render[idx++] = ' ';
            while (idx % MAT_TABSTOP != 0)
            {
                row->render[idx++] = ' ';
            }
        }
        else
            row->render[idx++] = row->chars[j];
    }

    row->render[idx] = '\0';
    row->rsize = idx;
}

// operations
void insertRws(int at, char *s, size_t len)
{
    if (at < 0 || at > E.numRws)
        return;

    E.row = realloc(E.row, sizeof(erow) * (E.numRws + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numRws - at));

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    updateRws(&E.row[at]);

    E.numRws++;
    E.dirty++;
}

void insertNewLine()
{
    if (E.cx == 0)
    {
        insertRws(E.cy, "", 0);
    }
    else
    {
        erow *row = &E.row[E.cy];
        insertRws(E.cy + 1, row->chars + E.cx, row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        updateRws(row);
    }

    E.cy++;
    E.cx = 0;
}

void rwsDeleteChar(erow *row, int at)
{
    if (at < 0 || at >= row->size)
        return;

    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    updateRws(row);
    E.dirty++;
}
void rwsInsertChar(erow *row, int at, int c)
{
    if (at < 0 || at > row->size)
        at = row->size;

    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->chars[at] = c;
    row->size++;
    updateRws(row);
    E.dirty++;
}

void rwsAppendString(erow *row, char *s, size_t len)
{
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    updateRws(row);
    E.dirty++;
}

void freeRws(erow *row)
{
    free(row->render);
    free(row->chars);
}

void delRws(int at)
{
    if (at < 0 || at >= E.numRws)
        return;
    freeRws(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numRws - at - 1));
    E.numRws--;
    E.dirty++;
}

void deleteChar()
{
    if (E.cy == E.numRws)
        return;
    if (E.cx == 0 && E.cy == 0)
        return;

    erow *row = &E.row[E.cy];
    if (E.cx > 0)
    {
        rwsDeleteChar(row, E.cx - 1);
        E.cx--;
    }
    else
    {
        E.cx = E.row[E.cy - 1].size;
        rwsAppendString(&E.row[E.cy - 1], row->chars, row->size);
        delRws(E.cy);
        E.cy--;
    }
}

void insertChar(int c)
{
    if (E.cy == E.numRws)
    {
        insertRws(E.numRws, "", 0);
    }

    rwsInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

// file io
char *rwsToString(int *buflen)
{
    int totlen = 0;
    int j;
    for (j = 0; j < E.numRws; j++)
    {
        totlen += E.row[j].size + 1;
    }

    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf;
    for (j = 0; j < E.numRws; j++)
    {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}
char *get_file_extension(const char *filename)
{
    char *dot = strrchr(filename, '.');
    if (!dot || dot == filename)
    {
        return "";
    }
    return dot + 1;
}

void open(char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file)
        die("file open");

    char *line = NULL;
    size_t cap = 0;
    size_t len;

    while ((len = getline(&line, &cap, file)) != -1)
    {
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        {
            len--;
            insertRws(E.numRws, line, len);
        }
    }

    free(line);
    fclose(file);

    E.dirty = 0;
}

// append buffer
struct abuf
{
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);
    if (new == NULL)
        return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) { free(ab->b); }

// output
void scroll()
{
    E.rx = 0;
    if (E.cy < E.numRws)
    {
        E.rx = rwsCxToRx(&E.row[E.cy], E.cx);
    }

    if (E.cy < E.rowOff)
    {
        E.rowOff = E.cy;
    }
    if (E.cy >= E.rowOff + E.screenRws)
    {
        E.rowOff = E.cy - E.screenRws + 1;
    }

    if (E.cx < E.colOff)
    {
        E.colOff = E.rx;
    }
    if (E.cx >= E.colOff + E.screenCls)
    {
        E.colOff = E.rx - E.screenCls + 1;
    }
}

void setStatusMessage(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

void drawMessage(struct abuf *ab)
{
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);

    if (msglen > E.screenCls)
        msglen = E.screenCls;

    if (msglen && time(NULL) - E.statusmsg_time < 5)
    {
        abAppend(ab, E.statusmsg, msglen);
    }
}

void drawStatus(struct abuf *ab)
{
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    char *language_symbol;

    if (current_file_extension == NULL)
        language_symbol = "󰈙";
    else if (strcmp(current_file_extension, "c") == 0)
        language_symbol = "";
    else if (strcmp(current_file_extension, "cpp") == 0)
        language_symbol = "";
    else if (strcmp(current_file_extension, "py") == 0)
        language_symbol = "";
    else
        language_symbol = "󰈙";

    int len = snprintf(status, sizeof(status), "  Mat | %s ", E.current_mode == NORMAL ? "NORMAL" : "INSERT");

    int rlen = snprintf(rstatus, sizeof(rstatus), "%s %s%s - %d/%d",
                        language_symbol, current_filename, E.dirty ? " *" : "", E.cy, E.numRws);

    if (len > E.screenCls)
        len = E.screenCls;

    abAppend(ab, status, len);

    while (len < E.screenCls)
    {
        if (E.screenCls - len == rlen)
        {
            abAppend(ab, rstatus, rlen);
            break;
        }
        else
        {
            abAppend(ab, " ", 1);
            len++;
        }
    }

    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void drawRws(struct abuf *ab)
{
    int y;
    for (y = 0; y < E.screenRws; y++)
    {
        int filerow = y + E.rowOff;
        if (filerow >= E.numRws)
        {
            abAppend(ab, "~", 1);
        }
        else
        {
            int len = E.row[filerow].rsize - E.colOff;

            if (len < 0)
                len = 0;

            if (len > E.screenCls)
                len = E.screenCls;
            char *c = &E.row[filerow].render[E.colOff];
            int j;

            for (j = 0; j < len; j++)
            {
                if (c[j] == '\t')
                {
                    abAppend(ab, "\x1b[7m", 4);
                    abAppend(ab, " ", 1);
                    abAppend(ab, "\x1b[m", 3);
                }
                else
                {
                    abAppend(ab, &c[j], 1);
                }
            }

            abAppend(ab, &E.row[filerow].render[E.colOff], len);
        }
        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

void searchCallback(char *query, int key)
{
    static int last_match = -1;
    static int direction = 1;

    if (key == '\r' || key == '\n')
    {
        last_match = -1;
        direction = 1;
        return;
    }

    for (int i = 0; i < E.numRws; i++)
    {
        erow *row = &E.row[i];
        char *match = strstr(row->render, query);
        if (match)
        {
            E.cy = i;
            E.cx = rwsRxToCx(row, match - row->render);
            E.rowOff = E.numRws;
            break;
        }
    }
}

void search()
{
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_colOff = E.colOff;
    int saved_rowOff = E.rowOff;

    char *query = prompt("/", searchCallback);

    if (query)
        free(query);
    else
    {
        E.cy = saved_cy;
        E.cx = saved_cx;
        E.colOff = saved_colOff;
        E.rowOff = saved_rowOff;
    }
}

void save()
{
    if (current_filename == NULL)
        return;

    if (E.dirty)
    {
        char *response = prompt("File not saved. Save? (y/n) ", NULL);
        if (strcmp(response, "y") != 0)
        {
            free(response);
            return;
        }
    }

    int len;
    char *buf = rwsToString(&len);

    FILE *file = fopen(current_filename, "w");
    if (!file)
    {
        setStatusMessage("Failed to open file for writing: %s", strerror(errno));
        return;
    }

    if (fwrite(buf, len, 1, file) != 1)
    {
        setStatusMessage("Failed to write file: %s", strerror(errno));
        fclose(file);
        return;
    }

    free(buf);
    fclose(file);

    setStatusMessage("File saved: %s", current_filename);
    E.dirty = 0;
}

void refreshScreen()
{
    scroll();

    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    drawRws(&ab);
    drawStatus(&ab);
    drawMessage(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowOff) + 1,
             (E.rx - E.colOff) + 1);

    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

// input
char *prompt(char *prompt, void (*callback)(char *, int))
{
    E.current_mode = INSERT;

    size_t bufsize = 128;
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while (1)
    {
        setStatusMessage(prompt, buf);
        refreshScreen();

        int c = readKey();
        if (c == BACKSPACE)
        {
            if (buflen != 0)
                buf[--buflen] = '\0';
        }
        else if (c == '\x1b')
        {
            setStatusMessage("");

            E.current_mode = NORMAL;

            if (callback)
                callback(buf, c);

            free(buf);
            return NULL;
        }
        else if (c == '\r')
        {
            if (buflen != 0)
            {
                setStatusMessage("");

                E.current_mode = NORMAL;

                if (callback)
                    callback(buf, c);

                return buf;
            }
        }
        else if (!iscntrl(c) && c < 128)
        {
            if (buflen == bufsize - 1)
            {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }

            buf[buflen++] = c;
            buf[buflen] = '\0';
        }

        E.current_mode = NORMAL;

        if (callback)
            callback(buf, c);
    }
}
void moveCursor(int key)
{
    erow *row = (E.cy >= E.numRws) ? NULL : &E.row[E.cy];
    switch (key)
    {

    case KEY_K: // UP
        if (E.cy != 0)
        {
            E.cy--;
        }
        else if (E.cy > 0)
        {
            E.cy--;
            E.cx = E.row[E.cy].size;
        }
        break;

    case KEY_J: // DOWN
        if (E.cy < E.numRws)
        {
            E.cy++;
        }
        break;

    case KEY_H: // LEFT
        if (E.cx != 0)
        {
            E.cx--;
        }
        break;

    case KEY_L: // RIGHT
        if (row && E.cx < row->size)
        {
            E.cx++;
        }
        else if (row && E.cx == row->size)
        {
            E.cy++;
            E.cx = 0;
        }
        break;
    }
    row = (E.cy >= E.numRws) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen)
    {
        E.cx = rowlen;
    }
}

int readKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }
    if (c == '\x1b')
    {
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) == 0)
        {
            return KEY_ESC;
        }
        else
        {
            read(STDIN_FILENO, &seq[1], 1);
            return -1;
        }
    }
    else
    {
        switch (c)
        {

        case '/':
            return KEY_SLASH;
        case 'k':
            return KEY_K;
        case 'j':
            return KEY_J;
        case 'h':
            return KEY_H;
        case 'l':
            return KEY_L;

        case 'q':
            return KEY_Q;

        case 'i':
            return KEY_I;

        case 'v':
            return KEY_V;

        case 'x':
            return KEY_X;
        }
        return c;
    }
}

void handleKeyPress()
{
    int c = readKey();

    if (c == ESC_K)
        return;

    if (c == KEY_ESC && E.current_mode == INSERT)
    {
        E.current_mode = NORMAL;
        return;
    }

    if (E.current_mode == NORMAL)
    {
        switch (c)
        {

        case CTRL_KEY('s'):
            save();
            break;

        case CTRL_KEY('u'):
            for (int y = 0; y < 4; y++)
            {
                if (E.cy - 1 <= E.numRws)
                {
                    moveCursor(KEY_K);
                }
            }

            break;
        case CTRL_KEY('d'):
            for (int y = 0; y < 4; y++)
            {
                if (E.cy + 1 <= E.numRws)
                {
                    moveCursor(KEY_J);
                }
            }
            break;

        case KEY_Q:
            if (E.current_mode != INSERT)
            {
                write(STDOUT_FILENO, "\x1b[2J", 4);
                write(STDOUT_FILENO, "\x1b[H", 3);
                exit(0);
            }
            break;

        case KEY_X:
            deleteChar();
            break;

        case KEY_A:
            moveCursor(KEY_L);
            E.current_mode = INSERT;
            break;

        case KEY_I:
            E.current_mode = INSERT;
            break;

        case KEY_SLASH:
            search();
            break;

        case KEY_K:
        case KEY_J:
        case KEY_H:
        case KEY_L:
            moveCursor(c);
            break;
        }
    }

    else
        switch (c)
        {
        case '\x1b':
            break;
        case '\r':
            insertNewLine();
            break;

        case BACKSPACE:
            deleteChar();
            break;

        default:
            insertChar(c);
            break;
        }
}

// init
void init()
{
    E.cx = 0;
    E.cy = 0;
    E.cx = 0;

    E.row = NULL;

    E.numRws = 0;
    E.rowOff = 0;
    E.colOff = 0;
    E.dirty = 0;

    E.current_mode = NORMAL;

    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;

    if (getWindowSize(&E.screenRws, &E.screenCls) == -1)
    {
        die("getWindowSize");
    }

    E.screenRws -= 2;
}

int main(int argc, char *argv[])
{
    enableRawMode();
    init();

    if (argc >= 2)
    {
        current_filename = argv[1];

        open(current_filename);
        current_file_extension = get_file_extension(current_filename);
    }

    setStatusMessage("Sigma skibidi toilet rizz", current_filename);

    while (1)
    {
        refreshScreen();
        handleKeyPress();
    }

    disableRawMode();

    return 0;
}
