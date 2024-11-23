#include "editor_ops.h"
#include "syntax.h"
#include "userinput.h"

/*** row operations ***/
int editorCxToRx(erow *row, int cx) {
    int rx = 0;
    for(int i = 0; i < cx; i++) {
        if (row->chars[i] == '\t') {
            rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        }
        rx++;
    }
    rx += E.lineno_offset;
    return rx;
}

int editorRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
    cur_rx++;
    if (cur_rx > rx) return cx;
  }
  return cx;
}

void editorUpdateRow(erow *row) {
    row->render = malloc(row->size + 1);
    int i, leading_spaces = 0;
    // Get leading spaces
    for (i = 0; i < row->size; i++) {
        if (row->chars[i] == ' ') {
            leading_spaces++;
        } else {
            break;
        }
    }
    row->indent = leading_spaces;
    free(row->render);
    row->render = malloc(row->size + 1);

    // Fill render buffer
    int idx = 0;
    for(i = 0; i < row->size; i++) {
        row->render[idx++] = row->chars[i];
    }
    row->render[idx] = '\0';
    row->rsize = idx;

    editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > E.numrows) return;
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    memmove(&E.row[at+1], &E.row[at], sizeof(erow) * (E.numrows - at));
    for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;

    E.row[at].idx = at;

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].render = NULL;
    E.row[at].rsize = 0;
    E.row[at].hl_open_comment = 0;
    E.row[at].hl = NULL;
    editorUpdateRow(&E.row[at]);

    E.numrows++;
    E.lineno_offset = floor (log10 (abs (E.numrows))) + 2;
    E.dirty++;
}

void editorFreeRow(erow *row) {
    free(row->chars);
    free(row->render);
    free(row->hl);
}

void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows) return;
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at+1], sizeof(erow) * (E.numrows - at - 1));
    for (int j = at; j <= E.numrows - 1; j++) E.row[j].idx--;
    E.numrows--;
    E.lineno_offset = floor (log10 (abs (E.numrows))) + 2;
    E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at+1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

/*** editor operations ***/
void editorInsertChar(int c) {
    if (E.cy == E.numrows) {
        editorInsertRow(E.cy, "", 0);
    }
    if (c == 125 && E.cx >= KILO_TAB_STOP && E.prev_char == 13) {
        for (int i = 0; i < KILO_TAB_STOP; i++) {
            editorDelChar();
        }
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
    E.cursor_pos = E.cx;
}

void editorInsertNewLine(void) {
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else {
        erow *row = &E.row[E.cy];
        // Auto indenting
        int len = E.row[E.cy].size - E.cx;
        char *s = malloc(E.row[E.cy].size + KILO_TAB_STOP);
        int padding = row->indent;
        int idx = 0;
        for (; padding > 0; padding--) {
            s[idx] = ' ';
            idx++;
            len++;
        }
        if (row->chars[row->size-1] == '{') {
            for (int i = 0; i < KILO_TAB_STOP; i++) {
                s[idx] = ' ';
                idx++;
                len++;
            }
        }
        memcpy(&s[idx], &row->chars[E.cx], row->size - E.cx);
        editorInsertRow(E.cy + 1, s, len);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
        free(s);
    }
    E.cy++;
    E.cx = E.row[E.cy].indent;
}

void editorDelChar(void) {
    if (E.cy >= E.numrows) return;
    if (E.cx == 0 && E.cy > 0) {
        E.cx = E.row[E.cy-1].size;
        editorRowAppendString(&E.row[E.cy-1], E.row[E.cy].chars, E.row[E.cy].size);
        editorDelRow(E.cy);
        E.cy--;
    } else if (E.cx != 0 && E.cy >= 0) {
        editorRowDelChar(&E.row[E.cy], E.cx - 1);
        E.cx--;
    }
}

/*** find ***/
static int searchOffset = 0;
void editorFindCallback(char *query, int key) {
    if (key == '\r' || key == '\x1b') {
        searchOffset = 0;
        return;
    }
    if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        searchOffset++;
    }
    if ((key == ARROW_LEFT || key == ARROW_UP) && searchOffset > 0) {
        searchOffset--;
    }

    int currSearchOffset = searchOffset;
    int matchFound = 0;
    for (int i = 0; i < E.numrows; i++) {
        struct erow *row = &E.row[i];
        char *match = strstr(row->render, query);
        if (match) {
            if (currSearchOffset <= 0) {
                matchFound = 1;
                E.cy = i;
                E.cx = editorRxToCx(row, match - row->render);
                E.rowoff = E.numrows;
                memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
                break;
            } else {
                currSearchOffset--;
            }
        }
    }
    if (!matchFound && searchOffset > 0) {
        searchOffset--;
    }
}

void editorFind(void) {
    int orig_cx = E.cx;
    int orig_cy = E.cy;
    int orig_rowoff = E.rowoff;
    int orig_coloff = E.coloff;
    unsigned char *orig_hl = E.row->hl;

    char *query = editorPrompt("Search: %s (Arrows to navigate | ESC to cancel)", editorFindCallback);
    if (query) {
        free(query);
    } else {
        E.cx = orig_cx;
        E.cy = orig_cy;
        E.rowoff = orig_rowoff;
        E.coloff = orig_coloff;
        E.row->hl = orig_hl;
    }
    for (int i = 0; i < E.numrows; i++) {
        editorUpdateSyntax(&E.row[i]);
    }
}

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
            }
            E.cursor_pos = E.cx;
            break;
        case ARROW_DOWN:
            if (E.cy < (E.numrows - 1)) {
                E.cy++;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) {
                E.cx++;
            }
            E.cursor_pos = E.cx;
            break;
    }

    // Snap cursor to end of line
    row = NULL;
    if (E.cy < E.numrows) row = &E.row[E.cy];
    if (row && (E.cx >= row->size || E.cursor_pos >= row->size)) {
        E.cx = row->size;
    } else {
        E.cx = E.cursor_pos;
    }
}

