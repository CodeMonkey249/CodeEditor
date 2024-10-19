#ifndef TERMINAL_H_
#define TERMINAL_H_

#include <termios.h>
#include <unistd.h>

typedef struct erow {
    int idx;
    int size;
    int rsize; 
    char *chars;
    char *render;
    unsigned char *hl;
    int hl_open_comment;
    int indent;
} erow;


enum keyboardOperations {
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
    TAB_KEY,
    SELECT_UP,
    SELECT_DOWN,
    SELECT_RIGHT,
    SELECT_LEFT,
};

enum editorHighlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEYWORD,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH,
    HL_SELECT,
};

struct editorConfig {
    int cx;
    int cy;
    int rx;
    erow *row;
    int dirty;
    char *filename;
    char *copy_buffer;
    char statusMessage[80];
    int prev_char;
    int rowoff;
    int coloff;
    int lineno_offset;
    int numrows;
    int screenrows;
    int screencols;
    time_t statusmsg_time;
    int select_start_x;
    int select_start_y;
    int select_end_x;
    int select_end_y;
    struct editorSyntax *syntax;
    struct termios orig_termios;
};

extern struct editorConfig E;

void die(const char *s);

void disableRawMode(void);

void enableRawMode(void);

int editorReadKey(void);

int getCursorPosition(int *rows, int *cols);

int getWindowSize(int *rows, int *cols);

#endif
