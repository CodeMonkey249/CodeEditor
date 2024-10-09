/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
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

enum editorHighlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEYWORD,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH,
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHTLIGHT_STRINGS (1<<1)

/*** data ***/
struct editorSyntax {
    char *filetype;
    char **filematch;
    char **keywords;
    char *singleline_comment_start;
    char *multiline_comment_start;
    char *multiline_comment_end;
    int flags;
};

typedef struct erow {
    int idx;
    int size;
    int rsize; 
    char *chars;
    char *render;
    unsigned char *hl;
    int hl_open_comment;
} erow;

struct editorConfig {
    int cx;
    int cy;
    int rx;
    erow *row;
    int dirty;
    char *filename;
    char statusMessage[80];
    int rowoff;
    int coloff;
    int lineno_offset;
    int numrows;
    int screenrows;
    int screencols;
    time_t statusmsg_time;
    struct editorSyntax *syntax;
    struct termios orig_termios;
};

struct editorConfig E;

/*** filetypes ***/
char *C_HL_EXTENSIONS[] = {".c", ".cpp", ".h", NULL};
char *C_HL_KEYWORDS[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if", 
    "int", "long", "register", "return", "short", "signed", "sizeof", "static",
    "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", NULL
};


struct editorSyntax HLDB[] = {
    {
        "c",
        C_HL_EXTENSIONS,
        C_HL_KEYWORDS,
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHTLIGHT_STRINGS,
    },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** prototypes ***/
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen(void);
char *editorPrompt(char *prompt, void (*callback)(char *, int));

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

/*** syntax highlighting ***/
int is_separator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(erow *row) {
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);
    int i = 0;
    unsigned char prev_hl = HL_NORMAL;
    char prev_char = '\0';

    int in_string = 0;
    int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

    char *scs = E.syntax->singleline_comment_start;
    int scs_len = scs ? strlen(scs) : 0;

    char *mcs = E.syntax->multiline_comment_start;
    char *mce = E.syntax->multiline_comment_end;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;


    char **keywords = E.syntax->keywords;

    while(i < row->rsize) {
        if (i != 0) {
            prev_hl = row->hl[i-1];
        }
        char c = row->render[i];

        /** singleline comments **/
        if (!strncmp(&row->render[i], scs, scs_len) && scs_len && !in_string 
                && !in_comment) {
            memset(&row->hl[i], HL_COMMENT, row->rsize - i);
            break;
        }

        /** multiline comments **/
        if (mcs_len && mce_len && !in_string) {
            if (in_comment) {
                row->hl[i] = HL_MLCOMMENT;
                if (!strncmp(&row->render[i], mce, mce_len)) {
                    memset(&row->hl[i], HL_MLCOMMENT, mce_len);
                    i += mce_len;
                    prev_char = row->render[i-1];
                    in_comment = 0;
                    continue;
                } else {
                    prev_char = row->render[i];
                    i++;
                    continue;
                }
            } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
                in_comment = 1;
                memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
                i += mcs_len;
                prev_char = row->render[i-1];
                continue;
            }
        }

        /** Strings **/
        if (E.syntax->flags & HL_HIGHTLIGHT_STRINGS) {
            if (in_string) {
                row->hl[i] = HL_STRING;
                if (c == '\\' && i + 1 < row->size) {
                    row->hl[i+1] = HL_STRING;
                    prev_char = row->hl[i+1];
                    i += 2;
                    continue;
                }
                if (c == in_string) in_string = 0;
                prev_char = c;
                i++;
                continue;
            } else {
                if (c == '"' || c == '\'') {
                    in_string = c;
                    row->hl[i] = HL_STRING;
                    prev_char = c;
                    i++;
                    continue;
                }
            }
        }

        /** Digits **/
        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((isdigit(c) && (is_separator(prev_char) || prev_hl == HL_NUMBER)) ||
                    (c == '.' && prev_hl == HL_NUMBER)) {
                row->hl[i] = HL_NUMBER;
                prev_char = c;
                i++;
                continue;
            }
        }

        /** keywords **/
        if (is_separator(prev_char)) {
            int j;
            for (j = 0; keywords[j]; j++) {
                char *keyword = keywords[j];
                int keyword_len = strlen(keywords[j]);
                if(!strncmp(&row->render[i], keyword, keyword_len) &&
                        is_separator(row->render[i + keyword_len])) {
                    memset(&row->hl[i], HL_KEYWORD, keyword_len);
                    prev_char = row->render[i+keyword_len-1];
                    i += keyword_len;
                    break;
                }
            }
        }
        prev_char = c;
        i++;
    }  
    int changed = (row->hl_open_comment != in_comment);
    row->hl_open_comment = in_comment;
    if (changed && row->idx + 1 < E.numrows)
        editorUpdateSyntax(&E.row[row->idx + 1]);
}

int editorSyntaxToColor(int hl) {
    switch(hl) {
        case HL_MLCOMMENT:
        case HL_COMMENT: return 36;
        case HL_KEYWORD: return 33;
        case HL_NUMBER: return 31;
        case HL_STRING: return 32;
        case HL_MATCH: return 34;
        default: return 37;
    }
}

void editorSelectSyntaxHilighting(void) {
    E.syntax = NULL;
    if (E.filename == NULL) return;

    char *extension = strstr(E.filename, ".");

    for (unsigned int i = 0; i < HLDB_ENTRIES; i++) {
        struct editorSyntax *syntax = &HLDB[i];
        int j = 0;
        while(syntax->filematch[j]) {
            int is_extension = syntax->filematch[j][0] == '.';
            if ((is_extension && extension && strcmp(syntax->filematch[j], extension)) || 
                    (!is_extension && strstr(E.filename, syntax->filematch[j]))) {
                E.syntax = syntax;
                int filerow;
                for (filerow = 0; filerow < E.numrows; filerow++) {
                    editorUpdateSyntax(&E.row[filerow]);
                }
                return;
            }
            j++;
        }
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
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void editorInsertNewLine(void) {
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else {
       erow *row = &E.row[E.cy];
       editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
       row = &E.row[E.cy];
       row->size = E.cx;
       row->chars[row->size] = '\0';
       editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}

void editorDelChar(void) {
    if (E.cy >= E.numrows) return;
    if (E.cx == 0 && E.cy > 0) {
        E.cx = E.row[E.cy-1].size;
        editorRowAppendString(&E.row[E.cy-1], E.row[E.cy].chars, E.row[E.cy].size);
        editorDelRow(E.cy);
        E.cy--;
    }
    else {
        editorRowDelChar(&E.row[E.cy], E.cx - 1);
        E.cx--;
    }
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
    editorSelectSyntaxHilighting();
    char * line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    // Read file by line
    while((linelen = getline(&line, &linecap, fp)) != -1) {
        // Strip return
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                    line[linelen - 1] == '\r'))
            linelen--;
        editorInsertRow(E.numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

void editorSave(void) {
    if (E.filename == NULL) {
        E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
        if (!E.filename) {
            editorSetStatusMessage("Save aborted");
            return;
        }
        editorSelectSyntaxHilighting();
    }
    

    if (E.filename == NULL) return;
    int len;
    char *s = editorRowsToString(&len);
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, s, len) == len) {
                close(fd);
                free(s);
                E.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }
    free(s);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
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
            char lineno[36];
            int linenoLen = snprintf(lineno, sizeof(lineno), "%d",
                    filerow + 1);
            abAppend(ab, lineno, linenoLen);
            int curr = floor (log10 (abs (filerow))) + 1;
            int padding = E.lineno_offset - curr;
            for (; padding > 0; padding--) abAppend(ab, " ", 1);
            // Syntax highlighting
            char *c = &E.row[filerow].render[E.coloff];
            unsigned char *hl = &E.row[filerow].hl[E.coloff];
            for (int i = 0; i < len; i++) {
                if (hl[i] == HL_NORMAL) {
                    abAppend(ab, "\x1b[39m", 5);
                    abAppend(ab, &c[i], 1);
                } else {
                    int color = editorSyntaxToColor(hl[i]);
                    char buf[36];
                    int colorLen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                    abAppend(ab, buf, colorLen);
                    abAppend(ab, &c[i], 1);
                    abAppend(ab, "\x1b[39m", 5);
                }
            }
            abAppend(ab, "\x1b[39m", 5);
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

/*** input ***/
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
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < (E.numrows - 1)) {
                E.cy++;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) {
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
        E.cx = row->size;
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
        case CTRL_KEY('f'):
            editorFind();
            break;
        case '\r':
            editorInsertNewLine();
            break;
        case BACKSPACE:
        case CTRL_KEY('h'):
            editorDelChar();
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
    quit_confirm = 1;
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
    E.dirty = 0;
    E.filename = NULL;
    E.lineno_offset = 0;
    E.statusMessage[0] = '\0';
    E.statusmsg_time = 0;
    E.syntax = NULL;
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

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-F = find | Ctrl-Q = quit\0");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    } return 0;

}
