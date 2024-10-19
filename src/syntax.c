#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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
#include "syntax.h"
#include "userinput.h"

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
        case HL_NUMBER: return 31; // red
        case HL_STRING: return 32; // green
        case HL_MATCH: return 34; // blue
        case HL_KEYWORD: return 35; // purple
        case HL_COMMENT: return 36; // cyan
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

