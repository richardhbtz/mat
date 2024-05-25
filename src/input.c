#include "mat.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

extern struct config E;

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
