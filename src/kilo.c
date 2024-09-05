/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define KILO_TAB_STOP 8
#define KILO_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)

enum {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DELETE_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
};

/*** data ***/
typedef struct erow {
    int size;
    int rsize; 
    char *chars;
    char *render;
} erow;

struct editorConfig {
    int cx;
    int cy;
    int rx;
    erow *row;
    char *filename;
    char statusMessage[80];
    int rowoff;
    int coloff;
    int numrows;
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

int editorReadKey(void) {
    int code = 0;
    char c;
    while (code != 1) {
        code = read(STDIN_FILENO, &c, 1);
        if (code == -1) die("read");
    }
    // Catch escape sequences
    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if(seq[0] == '[') {
            // PAGE/HOME/END
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DELETE_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch(seq[1]) {
                    // Arrow keys
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {  
            switch(seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        // If all else fails return ESC
        return '\x1b';
    }
    return c;
}

int getCursorPosition(int *rows, int *cols) {
    char buf[36];
    unsigned int i = 0;

    int code = write(STDOUT_FILENO, "\x1b[6n", 4);
    if (code == -1) return -1;

    for (i = 0; i < sizeof(buf) - 1; i++) {
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

/*** row operations ***/
int editorCxToRx(erow *row, int cx) {
    int rx = 0;
    for(int i = 0; i < cx; i++) {
        if (row->chars[i] == '\t') {
            rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        }
        rx++;
    }
    return rx;
}

void editorUpdateRow(erow *row) {
    row->render = malloc(row->size + 1);
    int i, tabs = 0;
    // Get number of tabs in row
    for (i = 0; i < row->size; i++) {
        if (row->chars[i] == '\t') {
            tabs++;
        }
    }
    free(row->render);
    row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);

    // Fill render buffer
    int idx = 0;
    for(i = 0; i < row->size; i++) {
        if (row->chars[i] == '\t') {
            row->render[idx++] = ' ';
            while (idx % KILO_TAB_STOP != 0) {
                row->render[idx++] = ' ';
            }
        } else {
            row->render[idx++] = row->chars[i];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    E.row[E.numrows].size = len;
    E.row[E.numrows].chars = malloc(len + 1);
    memcpy(E.row[E.numrows].chars, s, len);
    E.row[E.numrows].chars[len] = '\0';

    E.row[E.numrows].render = NULL;
    E.row[E.numrows].rsize = 0;
    editorUpdateRow(&E.row[E.numrows]);

    E.numrows++;
}

void editorRowInsertChar(erow *row, int at, int c) {
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at+1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
}

/*** editor operations ***/
void editorInsertChar(int c) {
    if (E.cy == E.numrows) {
        editorAppendRow("", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

/*** file i/o ***/
char * editorRowsToString(int *len) {
    int totallength = 0;
    for(int i = 0; i < E.numrows; i++) {
        totallength += E.row[i].size + 1;
    }
    *len = totallength;
    char *buf = malloc(totallength);
    char *iter = buf;
    for (int j = 0; j < E.numrows; j++) {
        memcpy(iter, E.row[j].chars, E.row[j].size);
        iter += E.row[j].size;
        *iter = '\n';
        iter++;
    }
    return buf;
}

void editorOpen(char *filename) {
    FILE * fp = fopen(filename, "r");
    E.filename = strdup(filename);
    char * line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    // Read file by line
    while((linelen = getline(&line, &linecap, fp)) != -1) {
        // Strip return
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                    line[linelen - 1] == '\r'))
            linelen--;
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
}

void editorSave(void) {
    if (E.filename == NULL) return;
    int len;
    char *s = editorRowsToString(&len);
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    ftruncate(fd, len);
    write(fd, s, len);
    free(s);
    close(fd);
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
void editorScroll(void) {
    E.rx = 0;
    if (E.cy < E.numrows) {
        E.rx = editorCxToRx(&E.row[E.cy], E.cx);
    }

    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols) {
        E.coloff = E.rx - E.screencols + 1;
    }
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
}


void editorDrawRows(struct abuf *ab) {
    for (int y = 0; y < E.screenrows; y++) {
        // Clear this row
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) {
            // Display upper third welcome message
            if (E.numrows == 0 && y == E.screenrows / 3) {
                char welcome[80];
                int len = snprintf(welcome, sizeof(welcome),
                        "Kilo editor -- version %s", KILO_VERSION);
                // Truncate if necessary
                if (len > E.screenrows) len = E.screenrows;
                // Padding to center the message
                int padding = (E.screencols - len  + 1) / 2;
                abAppend(ab, "~", 1);
                for (; padding > 0; padding--) abAppend(ab, " ", 1);
                // Print it
                abAppend(ab, welcome, len);
            } else {
                abAppend(ab, "~", 1);
            }
        } else {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }

        abAppend(ab, "\x1b[K", 4);
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab) {
    abAppend(ab, "\x1b[7m", 4);
    // Invert colors
    char status[80];
    char fileloc[80];
    // Display filename if there is one
    int len = snprintf(status, sizeof(status), "%.20s - %d lines",
            E.filename ? E.filename : "[No Name]", E.numrows);
    int fileloclen = snprintf(fileloc, sizeof(fileloc), "%d,%d",
            E.cy + 1, E.cx + 1);
    if (len > E.screencols) len = E.screencols;
    // Display the status bar
    abAppend(ab, status, len);
    // Print row/col right aligned
    for (int i = len; i < E.screencols; i++) {
        if (E.screencols - i == fileloclen) {
            abAppend(ab, fileloc, fileloclen);
            break;
        } else {
            abAppend(ab, " ", 1);
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 4);
    int len = strlen(E.statusMessage);
    if (len > E.screencols) len = E.screencols;
    abAppend(ab, E.statusMessage, len);
}

void editorRefreshScreen(void) {
    editorScroll();
    // ANSI Escape Codes
    // https://vt100.net/docs/vt100-ug/chapter3.html
    struct abuf ab = ABUF_INIT;
    // Hide the cursor
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);
    // Redraw canvas
    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);
    // Position cursor
    char pos[30];
    int len = snprintf(pos, sizeof(pos), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
            (E.rx - E.coloff) + 1);
    abAppend(&ab, pos, len);
    // Reveal cursor
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(char *s) {
    strcpy(E.statusMessage, s);
}
/*** input ***/

void editorMoveCursor(int key) {
    // Get current row if it exists
    erow *row = NULL;
    if (E.cy < E.numrows) row = &E.row[E.cy];

    switch(key) {
        case ARROW_UP:
            if (E.cy > 0) E.cy--;
            break;
        case ARROW_LEFT:
            if (E.cx > 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size - 1;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < (E.numrows - 1)) {
                E.cy++;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size - 1) {
                E.cx++;
            } else if (row && E.cy < E.numrows - 1) {
                E.cy++;
                E.cx = 0;
            }
            break;
    }

    // Snap cursor to end of line
    row = NULL;
    if (E.cy < E.numrows) row = &E.row[E.cy];
    if (row && E.cx >= row->size) {
        E.cx = row->size - 1;
    }
    if (E.cx < 0) E.cx = 0;
}

void editorProcessKeypress(void) {
    int c = editorReadKey();
    switch(c) {
        // Quit on CTRL-q
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
            // Navigation mapping
        case '\r':
            break;
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DELETE_KEY:
            break;
        case ARROW_UP:
        case ARROW_LEFT:
        case ARROW_DOWN:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
        case PAGE_UP:
            {
                // Move cursor to top
                E.cy = E.rowoff;
                // Move up by a screen
                int times = E.screenrows - 2;
                for (; times > 0; times--) editorMoveCursor(ARROW_UP);
                editorScroll();
                // Move cursor to bottom
                E.cy = E.rowoff + E.screenrows - 1;
                break;
            }
        case PAGE_DOWN:
            {
                // Move cursor to bottom
                E.cy = E.rowoff + E.screenrows - 1;
                // Move up by a screen
                int times = E.screenrows - 2;
                for (; times > 0; times--) editorMoveCursor(ARROW_DOWN);
                editorScroll();
                // Move cursor to top
                E.cy = E.rowoff;
                break;
            }
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            {
                // Get current row if it exists
                if (E.cy < E.numrows)
                    E.cx = E.row[E.cy].size - 1;
                break;
            }
        case CTRL_KEY('s'):
            editorSave();
            break;
        case CTRL_KEY('l'):
        case '\x1b':
            break;
        default:
            editorInsertChar(c);
            break;
    }
}

/*** init ***/

void initEditor(void) {
    /* Initialize the E struct */
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusMessage[0] = '\0';
    int code = getWindowSize(&E.screenrows, &E.screencols);
    if (code == -1) die("getWindowSize");
    E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    initEditor();
    enableRawMode();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-Q = quit\0");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    } return 0;

}
