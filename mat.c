// includes
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <sys/ioctl.h>

// defines
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey
{
	KEY_K,
	KEY_J,
	KEY_L,
	KEY_H
};

// data
struct editorConfig
{
	int cx, cy;
	int screenRws, screenCls;

	struct termios orig_termios;
};

struct editorConfig E;

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
		*rws = ws.ws_col;
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

// append buffer
struct abuf
{
	char *b;
	int len;
};

#define ABUF_INIT                                                              \
	{                                                                          \
		NULL, 0                                                                \
	}

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
void drawRows(struct abuf *ab)
{
	int y;
	for (y = 0; y < E.screenRws; y++)
	{
		if (y == E.screenRws / 3)
		{
			char message[80];
			int messageLen = snprintf(message, sizeof(message), "Mat ", "WWW");

			if (messageLen > E.screenCls)
				messageLen = E.screenCls;

			int padding = (E.screenCls - messageLen) / 2;
			if (padding)
			{
				abAppend(ab, "~", 1);
				padding--;
			}

			while (padding--)
				abAppend(ab, " ", 1);
			abAppend(ab, message, messageLen);
		}
		else
		{
			abAppend(ab, "~", 1);
		}
		abAppend(ab, "\x1b[K", 3);
		if (y < E.screenRws - 1)
		{
			abAppend(ab, "\r\n", 2);
		}
	}
}

void refreshScreen()
{
	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);

	drawRows(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

// input
void moveCursor(int key)
{
	switch (key)
	{

	case KEY_K:
		if (E.cy != 0)
		{
			E.cy--;
		}
		break;
	case KEY_J:
		if (E.cy != E.screenCls - 1)
		{
			E.cy++;
		}
		break;
	case KEY_H:
		if (E.cx != 0)
		{
			E.cx--;
		}
		break;
	case KEY_L:
		if (E.cx != E.screenCls - 1)
		{
			E.cx++;
		}
		break;
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
		char seq[3];
		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1)
			return '\x1b';
		if (seq[0] == '[')
		{
		}
		return '\x1b';
	}
	else
	{
		switch (c)
		{
		case 'k':
			return KEY_K;
		case 'j':
			return KEY_J;
		case 'h':
			return KEY_H;
		case 'l':
			return KEY_L;
		}
		return c;
	}
}

void handleKeyPress()
{
	int c = readKey();

	switch (c)
	{
	case CTRL_KEY('q'):
		write(STDOUT_FILENO, "\x1b[2J", 4);
		write(STDOUT_FILENO, "\x1b[H", 3);
		exit(0);
		break;
	case KEY_K:
	case KEY_J:
	case KEY_H:
	case KEY_L:
		moveCursor(c);
		break;
	}
}

// init
void init()
{
	E.cx = 0;
	E.cy = 0;

	if (getWindowSize(&E.screenRws, &E.screenCls) == -1)
	{
		die("getWindowSize");
	}
}

int main(int argc, char *argv[])
{
	enableRawMode();
	init();

	while (1)
	{
		refreshScreen();
		handleKeyPress();
	}

	disableRawMode();
	return 0;
}
