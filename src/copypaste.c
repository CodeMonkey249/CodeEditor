#include <stdlib.h>

#include "terminal.h"
#include "draw.h"

void copy(void) {

    // Is end_y < start_y
    //  move = -1
    // Is end_y > start_y
    //  move = 1
    // Is end_y == start_y
    //  Is end_x < start_x
    //   move = -1
    //  Is end_x > start_x
    //   move = 1
    //  Is end_x == start_x
    //   move = 0

    int move;
    if (E.select_end_y < E.select_start_y) {
        move = -1;
    } else if (E.select_end_y > E.select_start_y) {
        move = 1;
    } else if (E.select_end_y == E.select_start_y) {
        if (E.select_end_x < E.select_start_x) {
            move = -1;
        } else if (E.select_end_x > E.select_start_x) {
            move = 1;
        } else if (E.select_end_x == E.select_start_x) {
            move = 0;
        }
    }

    int x = E.select_start_x;
    int y = E.select_start_y;
    int x_end = E.select_end_x;
    int y_end = E.select_end_y;

    if (move == -1) {
        x = E.select_end_x + 1;
        y = E.select_end_y;
        x_end = E.select_start_x + 1;
        y_end = E.select_start_y;
        move = 1;
    }

    E.copy_buffer = malloc(2);
    E.copy_buffer_len = 1;

    int idx = 0;
    int potato = 0;

    while(move && !(x == x_end && y == y_end)) {
        if (idx >= E.copy_buffer_len) {
            E.copy_buffer_len *= 2;
            E.copy_buffer = realloc(E.copy_buffer, E.copy_buffer_len + 1);
        }
        if (x >= E.row[y].size && y != y_end) {
            E.copy_buffer[idx] = '\n';
            idx++;
            y++;
            x = -1;
        } else if (E.row[y].chars) {
            E.copy_buffer[idx] = E.row[y].chars[x];
            idx++;
        }
        x += move;
    }
    E.copy_buffer_len = idx;
    E.copy_buffer[E.copy_buffer_len] = '\0';

    editorSetStatusMessage("%d characters copied", E.copy_buffer_len);
}
