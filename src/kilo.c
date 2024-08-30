/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/
struct editorConfig {
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

void disableRawMode(void) {
    /* Restore terminal flags */
    int code = tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios);
    if (code == -1) die("tcsetattr");
}

void enableRawMode(void) {
    /* Put the terminal in raw mode.
    
    Remove all preprocessing of input text.
    */
    // Store original terminal settings 
    int code = tcgetattr(STDIN_FILENO, &E.orig_termios);
    if (code == -1) die("tcgetattr");
    // Restore terminal at end of session
    atexit(disableRawMode);
    // We will store the raw settings here
    struct termios raw = E.orig_termios;
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

int getCursorPosition(int *rows, int *cols) {
    char buf[36];

    int code = write(STDOUT_FILENO, "\x1b[6n", 4);
    if (code == -1) return -1;
    unsigned int i = 0;
    for(i = 0; i < sizeof(buf) - 1; i++) {
        code = read(STDIN_FILENO, &buf[i], 1);
        if (code != 1) break;
        if (buf[i] == 'R') break;
    }
    if (buf[0] != '\x1f' || buf[1] != '[') return -1;
    code = sscanf(&buf[2], "%d;%d", rows, cols);
    if (code != 2) return -1;
    return 0;
}

int getWindowSize(int *rows, int *cols) {
    int code;
    struct winsize ws;
    code = ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    if (code == -1 || ws.ws_col == 0) {
        return -1;
    } else {
       *rows = ws.ws_row;
       *cols = ws.ws_col;
       return 0;
    }
}

/*** append buffer ***/
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

/*** output ***/
void editorDrawRows(struct abuf *ab) {
    for (int y = 0; y < E.screenrows; y++) {
        abAppend(ab, "~", 1);
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen(void) {
    // ANSI Escape Codes
    // https://vt100.net/docs/vt100-ug/chapter3.html
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[2J", 4);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    abAppend(&ab, "\x1b[H", 3);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
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

void initEditor(void) {
    /* Initialize the E struct */
    int code = getWindowSize(&E.screenrows, &E.screencols);
    if (code == -1) die("getWindowSize");
}

int main(void) {
    initEditor();
    enableRawMode();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
	return 0;
}
