#include "syntax.h"
#include "draw.h"
#include "editor_ops.h"
#include "userinput.h"

/*** append buffer ***/
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
            // Print line numbers
            abAppend(ab, "\x1b[33m", 5); // yellow
            char lineno[36];
            int linenoLen = snprintf(lineno, sizeof(lineno), "%d",
                    filerow + 1);
            abAppend(ab, lineno, linenoLen);
            int curr = floor (log10 (abs (filerow+1))) + 1;
            int padding = E.lineno_offset - curr;
            for (; padding > 0; padding--) abAppend(ab, " ", 1);
            abAppend(ab, "\x1b[39m", 5); // normal color
            // Syntax highlighting
            char *c = &E.row[filerow].render[E.coloff];
            unsigned char *hl = &E.row[filerow].hl[E.coloff];
            for (int i = 0; i < len; i++) {
                if (((filerow >= E.select_start_y && filerow <= E.select_end_y)
                        || (filerow <= E.select_start_y && filerow >= E.select_end_y))
                    && ((i >= E.select_start_x && i < E.select_end_x)
                        || (i <= E.select_start_x && i > E.select_end_x))
                    && editorIsSelecting()) {
                    // Invert colors
                    // TODO: does this invert the entire line?
                    abAppend(ab, "\x1b[7m", 4);
                }

                if (hl[i] == HL_NORMAL) {
                    abAppend(ab, &c[i], 1);
                } else {
                    int color = editorSyntaxToColor(hl[i]);
                    char buf[36];
                    int colorLen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                    abAppend(ab, buf, colorLen);
                    abAppend(ab, &c[i], 1);
                    abAppend(ab, "\x1b[39m", 5);
                }
                abAppend(ab, "\x1b[m", 3);
            }
            abAppend(ab, "\x1b[39m", 5);
        }
        abAppend(ab, "\x1b[K", 4);
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab) {
    // Invert colors
    abAppend(ab, "\x1b[7m", 4);
    char status[80];
    char fileloc[80];
    // Display filename if there is one
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
            E.filename ? E.filename : "[No Name]", E.numrows,
            E.dirty ? "(modified)" : "");
    int fileloclen = snprintf(fileloc, sizeof(fileloc), "%s | %d,%d",
            E.syntax ? E.syntax->filetype : "no filetype", E.cy + 1, E.cx + 1);
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

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusMessage, sizeof(E.statusMessage), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

