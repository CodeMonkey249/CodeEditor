#ifndef DRAW_H_
#define DRAW_H_

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len);

void abFree(struct abuf *ab);

void editorScroll(void);

void editorDrawRows(struct abuf *ab);

void editorDrawStatusBar(struct abuf *ab);

void editorDrawMessageBar(struct abuf *ab);

void editorRefreshScreen(void);

void editorSetStatusMessage(const char *fmt, ...);

#endif
