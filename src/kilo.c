/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

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

#include "fileio.h"
#include "terminal.h"
#include "draw.h"
#include "userinput.h"

/*** data ***/
struct editorConfig E;

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
    E.copy_buffer = NULL;
    E.prev_char = ' ';
    E.lineno_offset = 2;
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
