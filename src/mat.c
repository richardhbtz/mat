#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include "input.c"
#include "statusline.c"
#include "syntax.c"

#include <ctype.h>
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

#define MAT_VERSION "0.0.1"

// globals
int MAT_TABSTOP = 4;

enum highlight
{
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};

// proto
int getWindowSize(int *rws, int *cls);
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

void setCursorShape(const char *shape)
{
    write(STDOUT_FILENO, shape, 6);
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

void handleWindowSizeChange()
{
    if (getWindowSize(&E.screenRws, &E.screenCls) == -1)
        die("getWindowSize");

    if (E.cy > E.screenRws)
        E.cy = E.screenRws - 1;
    if (E.cx > E.screenCls)
        E.cx = E.screenCls - 1;

    refreshScreen();
}

int getWindowSize(int *rws, int *cls)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
        return -1;
    else
    {
        *rws = ws.ws_row;
        *cls = ws.ws_col + 4;
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

// syntax

int is_separator(int c)
{
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[]{};", c) != NULL;
}

void updateSyntax(erow *row)
{
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);

    if (E.syntax == NULL)
        return;

    char **keywords = E.syntax->keywords;

    char *scs = E.syntax->singleline_comment_start;
    char *mcs = E.syntax->multiline_comment_start;
    char *mce = E.syntax->multiline_comment_end;

    int scs_len = scs ? strlen(scs) : 0;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    int prev_sep = 1;
    int in_string = 0;
    int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

    int i = 0;
    while (i < row->rsize)
    {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

        if (scs_len && !in_string && !in_comment)
        {
            if (!strncmp(&row->render[i], scs, scs_len))
            {
                memset(&row->hl[i], HL_COMMENT, row->rsize - i);
                break;
            }
        }

        if (mcs_len && mce_len && !in_string)
        {
            if (in_comment)
            {
                row->hl[i] = HL_MLCOMMENT;
                if (!strncmp(&row->render[i], mce, mce_len))
                {
                    memset(&row->hl[i], HL_MLCOMMENT, mce_len);
                    i += mce_len;
                    in_comment = 0;
                    prev_sep = 1;
                    continue;
                }
                else
                {
                    i++;
                    continue;
                }
            }
            else if (!strncmp(&row->render[i], mcs, mcs_len))
            {
                memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
                i += mcs_len;
                in_comment = 1;
                continue;
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS)
        {
            if (in_string)
            {
                row->hl[i] = HL_STRING;

                if (c == '\\' && i + 1 < row->rsize)
                {
                    row->hl[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }

                if (c == in_string)
                    in_string = 0;

                i++;
                prev_sep = 1;
                continue;
            }
            else
            {
                if (c == '"' || c == '\'')
                {
                    in_string = c;
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS)
        {
            if (isdigit(c) && (prev_sep || prev_hl == HL_NUMBER) || (c == '.' && prev_hl == HL_NUMBER))
            {
                row->hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        if (prev_sep)
        {
            int j;
            for (j = 0; keywords[j]; j++)
            {
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen - 1] == '|';
                if (kw2)
                    klen--;
                if (!strncmp(&row->render[i], keywords[j], klen) &&
                    is_separator(row->render[i + klen]))
                {
                    memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL)
            {
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_separator(c);
        i++;
    }
    int changed = (row->hl_open_comment != in_comment);
    row->hl_open_comment = in_comment;
    if (changed && row->idx + 1 < E.numRws)
        updateSyntax(&E.row[row->idx + 1]);
}

const char *syntaxToColor(int hl)
{
    switch (hl)
    {
    case HL_COMMENT:
    case HL_MLCOMMENT:
        return hexToAnsiBackground("#45475a");
    case HL_KEYWORD1:
        return hexToAnsiBackground("#f9e2af");
    case HL_KEYWORD2:
        return hexToAnsiBackground("#cba6f7");
    case HL_STRING:
        return hexToAnsiBackground("#a6e3a1");
    case HL_NUMBER:
        return hexToAnsiBackground("#fab387");
    case HL_MATCH:
        return hexToAnsiBackground("#f38ba8");
    default:
        return hexToAnsiBackground("#f38ba8");
    }
}

void selectSyntaxHighlight()
{
    E.syntax = NULL;

    if (E.current_file_name == NULL)
        return;

    char *ext = strrchr(E.current_file_name, '.');

    for (unsigned int j = 0; j < HLDB_ENTRIES; j++)
    {
        struct syntax *s = &HLDB[j];
        unsigned int i = 0;

        while (s->filematch[i])
        {
            int is_ext = (s->filematch[i][0] == '.');
            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
                (!is_ext && strstr(E.current_file_name, s->filematch[i])))
            {
                E.syntax = s;
                int filerow;
                for (filerow = 0; filerow < E.numRws; filerow++)
                {
                    updateSyntax(&E.row[filerow]);
                }

                return;
            }
            i++;
        }
    }
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

    updateSyntax(row);
}

// operations
void insertRws(int at, char *s, size_t len)
{
    if (at < 0 || at > E.numRws)
        return;

    E.row = realloc(E.row, sizeof(erow) * (E.numRws + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numRws - at));

    for (int j = at + 1; j <= E.numRws; j++)
        E.row[j].idx++;

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].hl_open_comment = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;

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
    free(row->hl);
}

void deleteRws(int at)
{
    if (at < 0 || at >= E.numRws)
        return;
    freeRws(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numRws - at - 1));
    for (int j = at; j < E.numRws - 1; j++)
        E.row[j].idx--;

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
        deleteRws(E.cy);
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
    selectSyntaxHighlight();

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
#define ABUF_INIT {NULL, 0}

struct config E;

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

void drawRws(struct abuf *ab)
{
    int y;

    const char *foreground_color = hexToAnsiFore("#1E1D2D");
    abAppend(ab, foreground_color, strlen(foreground_color)); // Set the global background color

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
            unsigned char *hl = &E.row[filerow].hl[E.colOff];
            const char *current_color = NULL;

            for (int j = 0; j < len; j++)
            {
                if (hl[j] == HL_NORMAL)
                {
                    if (current_color != NULL)
                    {
                        abAppend(ab, "\033[39m", 5); // Reset foreground color
                        current_color = NULL;
                    }
                    abAppend(ab, &c[j], 1);
                }
                else
                {
                    const char *color = syntaxToColor(hl[j]);
                    if (color != current_color)
                    {
                        current_color = color;
                        abAppend(ab, color, strlen(color));
                    }
                    abAppend(ab, &c[j], 1);
                }
            }
            if (current_color != NULL)
            {

                abAppend(ab, "\033[39m", 5); // Reset foreground color
            }
        }

        abAppend(ab, "\x1b[K", 3); // Clear to the end of the line
        abAppend(ab, "\r\n", 2);   // New line
    }
    abAppend(ab, "\x1b[0m", 4); // Reset all attributes at the end
}

void searchCallback(char *query, int key)
{
    static int last_match = -1;
    static int direction = 1;

    static int saved_hl_line;
    static char *saved_hl = NULL;

    if (saved_hl)
    {
        memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }

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

            saved_hl_line = i;
            saved_hl = malloc(row->rsize);

            memcpy(saved_hl, row->hl, row->rsize);

            memset(&row->hl[match - row->render], HL_MATCH, strlen(query));

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
    if (E.current_file_name == NULL)
        return;

    if (E.dirty)
    {
        char *response = prompt("File not saved. Save? (y/n) ", NULL);
        if (strcmp(response, "y") != 0)
        {
            free(response);
            return;
        }
        selectSyntaxHighlight();
    }

    int len;
    char *buf = rwsToString(&len);

    FILE *file = fopen(E.current_file_name, "w");
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

    setStatusMessage("File saved: %s", E.current_file_name);
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

// init
void init()
{
    E.cx = 0;
    E.cy = 0;
    E.cx = 0;

    E.row = NULL;
    E.syntax = NULL;

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

    signal(SIGWINCH, handleWindowSizeChange);

    E.screenRws -= 2;
}

int main(int argc, char *argv[])
{
    enableRawMode();
    init();

    if (argc >= 2)
    {
        E.current_file_name = argv[1];

        open(E.current_file_name);
        E.current_file_extension = get_file_extension(E.current_file_name);
    }

    setStatusMessage("%s", E.current_file_name);

    while (1)
    {
        refreshScreen();
        handleKeyPress();
    }

    disableRawMode();

    return 0;
}
