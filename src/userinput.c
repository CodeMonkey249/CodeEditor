#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "editor_ops.h"
#include "terminal.h"
#include "draw.h"
#include "fileio.h"

int editorReadKey(void) {
    int code = 0;
    char c;
    while (code != 1) {
        code = read(STDIN_FILENO, &c, 1);
        if (code == -1) die("read");
    }
    // Catch tab
    if (c == '\t') {
        return TAB_KEY;
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

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';
    while(1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();
        if (c == '\x1b') {
            if (callback) callback(buf, c);
            editorSetStatusMessage("");
            free(buf);
            return NULL;
        } else if (c == BACKSPACE || c == CTRL_KEY('h') || c == DELETE_KEY) {
            if (buflen > 0) {
                buflen--;
                buf[buflen] = '\0';
                editorRefreshScreen();
            }
        } else if (c == '\r') {
            if (buflen != 0) {
                editorSetStatusMessage("");
                if (callback) callback(buf, c);
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen >= bufsize) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
        if (callback) callback(buf, c);
    }
}

void editorProcessKeypress(void) {
    static int quit_confirm = 1;
    int c = editorReadKey();
    switch(c) {
        // Quit on CTRL-q
        case CTRL_KEY('q'):
            if (E.dirty && quit_confirm == 1) {
                editorSetStatusMessage("File has unsaved changes. "
                        "Press CTRL-Q again to quit, or press CTRL-S to save.");
                quit_confirm--;
                return;
            }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
            // Navigation mapping
        case CTRL_KEY('c'):
            // Copy
            break;
        case CTRL_KEY('v'):
            // Paste
            break;
        case CTRL_KEY('f'):
            editorFind();
            break;
        case '\r':
            editorInsertNewLine();
            break;
        case BACKSPACE:
        case CTRL_KEY('h'):
            if (E.cx > 0 && E.row[E.cy].chars[E.cx-1] == ' ') {
                // Cursor is on a tab stop
                if (E.cx % KILO_TAB_STOP == 0 && E.cx != 0) {
                    for (int i = 0; i < KILO_TAB_STOP; i++) {
                        if (E.cx != 0 && E.row[E.cy].chars[E.cx-1] == ' ') {
                            editorDelChar();
                        } else {
                            break;
                        }
                    }
                }
                // Cursor is not on a tab stop
                else {
                    int idx = E.cx;
                    while (idx % KILO_TAB_STOP != 0) {
                        if (E.cx != 0 && E.row[E.cy].chars[E.cx-1] == ' ') {
                            editorDelChar();
                            idx--;
                        } else {
                            break;
                        }
                    }
                }
            } else {
                editorDelChar();
            }
            break;
        case DELETE_KEY:
            editorMoveCursor(ARROW_RIGHT);
            editorDelChar();
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
                    E.cx = E.row[E.cy].size;
                break;
            }
        case TAB_KEY:
            {
                // On a tab stop or at the front of a line
                if (E.cx % KILO_TAB_STOP == 0 || E.cx == 0) {
                    for (int i = 0; i < KILO_TAB_STOP; i++) {
                        editorInsertChar(' ');
                    }
                }
                else {
                    int idx = E.cx;
                    while (idx % KILO_TAB_STOP != 0) {
                        editorInsertChar(' ');
                        idx++;
                    }
                }
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
    E.prev_char = c;
    quit_confirm = 1;
}

