/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/
struct termios orig_termios;

/*** terminal ***/
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

void disableRawMode(void) {
    /* Restore terminal flags */
    int code = tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    if (code == -1) die("tcsetattr");
}

void enableRawMode(void) {
    /* Put the terminal in raw mode.
    
    Remove all preprocessing of input text.
    */
    // Store original terminal settings 
    int code = tcgetattr(STDIN_FILENO, &orig_termios);
    if (code == -1) die("tcgetattr");
    // Restore terminal at end of session
    atexit(disableRawMode);
    // We will store the raw settings here
    struct termios raw = orig_termios;
    // Get the current settings
    code = tcgetattr(STDIN_FILENO, &raw);
    if (code == -1) die("tcgetattr");

    ///////////
    // Flags //
    ///////////
    // Raw mode requires setting certain termios flags
    // https://man7.org/linux/man-pages/man3/termios.3.html
    raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                           | INLCR | IGNCR | ICRNL | IXON);
    raw.c_oflag &= ~OPOST;
    raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= ~(CSIZE | PARENB);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

char editorReadKey(void) {
    int code = 0;
    char c;
    while (code != 1) {
        code = read(STDIN_FILENO, &c, 1);
        if (code == -1) die("read");
    }
    return c;
}

/*** output ***/
void editorRefreshScreen(void) {
    // ANSI Escape Codes
    // https://vt100.net/docs/vt100-ug/chapter3.html
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** input ***/
void editorProcessKeypress(void) {
    char c = editorReadKey();
    switch(c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

/*** init ***/
int main(void) {
    enableRawMode();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
	return 0;
}
