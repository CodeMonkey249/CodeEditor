#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "editor_ops.h"
#include "terminal.h"
#include "draw.h"
#include "fileio.h"
#include "copypaste.h"

int editorReadKey(void) {
    int code = 0;
    char c;
    while (code != 1) {
        code = read(STDIN_FILENO, &c, 1);
        if (code == -1) die("read");
    }
    // Tab
    if (c == '\t') {
        return TAB_KEY;
    }

    // Escape Sequences
    if (c == '\x1b') {
        char seq[5];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if(seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                editorSetStatusMessage("%d,%d,%d", seq[0], seq[1], seq[2]);
                // Text Selection (SHIFT+ARROWS)
                //   <esc>[1;2<ABCD>
                if (seq[1] == '1' && seq[2] == ';') {
                    if (read(STDIN_FILENO, &seq[3], 1) != 1) return '\x1b';
                    if (seq[3] == '2') {
                        if (read(STDIN_FILENO, &seq[4], 1) != 1) return '\x1b';
                        switch(seq[4]) {
                            case 'A': return SELECT_UP;
                            case 'B': return SELECT_DOWN;
                            case 'C': return SELECT_RIGHT;
                            case 'D': return SELECT_LEFT;
                        }
                    }
                }
                // PAGE/HOME/END
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
                // Arrow keys
                switch(seq[1]) {
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

void editorStartSelecting(void) {
    E.select_start_x = E.cx;
    E.select_start_y = E.cy;
    // TODO: highlighting!
}

int editorIsSelecting(void) {
    if (E.select_start_x == E.select_end_x && E.select_start_y == E.select_end_y) {
        return 0;
    }
    else {
        return 1;
    }
}

int isInSelection(int x, int y) {
    if (!editorIsSelecting()) return 0;

    // Up
    if (E.select_start_y > E.select_end_y) {
        // top line
        if (y == E.select_end_y && x > E.select_end_x) {
            return 1;
        // bottom line
        } else if (y == E.select_start_y && x <= E.select_start_x) {
            return 1;
        // between
        } else if (y < E.select_start_y && y > E.select_end_y) {
            return 1;
        }
    // Down
    } else if (E.select_start_y < E.select_end_y) {
        // top line
        if (y == E.select_start_y && x >= E.select_start_x) {
            return 1;
        // bottom line
        } else if (y == E.select_end_y && x < E.select_end_x) {
            return 1;
        // between
        } else if (y > E.select_start_y && y < E.select_end_y) {
            return 1;
        }
    // Same Line
    } else if (E.select_start_y == E.select_end_y) {
        if (y == E.select_start_y) {
            // before
            if (x >= E.select_start_x && x < E.select_end_x) {
                return 1;
            // after
            } else if (x <= E.select_start_x && x > E.select_end_x) {
                return 1;
            }
        }
    }

    return 0;
}

void editorProcessKeypress(void) {
    static int quit_confirm = 1;
    int c = editorReadKey();
    //editorSetStatusMessage("%d", E.prev_char);
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
            copy();
            break;
        case CTRL_KEY('v'):
            paste();
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
            E.cursor_pos = E.cx;
            break;
        case END_KEY:
            {
                // Get current row if it exists
                if (E.cy < E.numrows)
                    E.cx = E.row[E.cy].size;
                E.cursor_pos = E.cx;
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
        case SELECT_UP:
        case SELECT_DOWN:
        case SELECT_RIGHT:
        case SELECT_LEFT:
            if (!editorIsSelecting()) {
                editorStartSelecting();
            } else if (E.cx != E.select_end_x || E.cy != E.select_end_y) {
                editorStartSelecting();
            }
            if (c == SELECT_UP) editorMoveCursor(ARROW_UP);
            if (c == SELECT_DOWN) editorMoveCursor(ARROW_DOWN);
            if (c == SELECT_RIGHT) editorMoveCursor(ARROW_RIGHT);
            if (c == SELECT_LEFT) editorMoveCursor(ARROW_LEFT);
            E.select_end_x = E.cx;
            E.select_end_y = E.cy;
            break;
        case CTRL_KEY('s'):
            editorSave();
            break;
        case CTRL_KEY('l'):
        case '\x1b':
            E.select_start_x = E.select_end_x;
            E.select_start_y = E.select_end_y;
            //editorSetStatusMessage("ESC char");
            break;
        default:
            editorInsertChar(c);
            break;
    }
    E.prev_char = c;
    quit_confirm = 1;
}

