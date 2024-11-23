#include "userinput.h"
#include "draw.h"
#include "editor_ops.h"
#include "fileio.h"
#include "terminal.h"
#include "syntax.h"

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

