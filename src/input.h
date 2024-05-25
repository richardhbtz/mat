// Users/richard/Repos/c/mat/src/input.h
#pragma once

#define CTRL_KEY(k) ((k) & 0x1f)
#define KEY_ESC 27
#define ESC_K -1

int readKey();
void handleKeyPress();

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
