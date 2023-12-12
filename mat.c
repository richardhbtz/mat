// includes
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include <sys/ioctl.h>

// defines
#define CTRL_KEY(k) ((k) & 0x1f)

// data
struct editorConfig
{
	int screenRws, screencls;
	
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

// output
void drawRows()
{
	int y;
	for (y = 0; y < E.screenRws; y++)
	{
		write(STDOUT_FILENO, "~\r\n", 3);
	}
}

void refreshScreen()
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	drawRows();

	write(STDOUT_FILENO, "\x1b[H", 3);
}

// input
char readKey()
{
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
	{
		if (nread == -1 && errno != EAGAIN)
			die("read");
	}
	return c;
}

void handleKeyPress()
{
	char c = readKey();

	switch (c)
	{
	case CTRL_KEY('q'):
		write(STDOUT_FILENO, "\x1b[2J", 4);
		write(STDOUT_FILENO, "\x1b[H", 3);
		exit(0);
		break;
	}
}

// init
void init()
{
	if (getWindowSize(&E.screenRws, &E.screencls) == -1)
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
