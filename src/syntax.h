#ifndef SYNTAX_H_
#define SYNTAX_H_

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

#include "terminal.h"

struct editorSyntax {
    char *filetype;
    char **filematch;
    char **keywords;
    char *singleline_comment_start;
    char *multiline_comment_start;
    char *multiline_comment_end;
    int flags;
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHTLIGHT_STRINGS (1<<1)

extern char *C_HL_EXTENSIONS[];
extern char *C_HL_KEYWORDS[];

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))


int is_separator(int c);

void editorUpdateSyntax(erow *row);

int editorSyntaxToColor(int hl);

void editorSelectSyntaxHilighting(void);

#endif
