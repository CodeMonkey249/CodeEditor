#ifndef EDITOR_OPS_H_
#define EDITOR_OPS_H_

#include "terminal.h"

/*** defines ***/ 
#define KILO_TAB_STOP 4
#define KILO_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)


int editorCxToRx(erow *row, int cx);

int editorRxToCx(erow *row, int rx);

void editorUpdateRow(erow *row);

void editorInsertRow(int at, char *s, size_t len);

void editorFreeRow(erow *row);

void editorDelRow(int at);

void editorRowInsertChar(erow *row, int at, int c);

void editorRowAppendString(erow *row, char *s, size_t len);

void editorRowDelChar(erow *row, int at);

void editorInsertChar(int c);

void editorInsertNewLine(void);

void editorDelChar(void);

void editorFindCallback(char *query, int key);

void editorFind(void);

void editorMoveCursor(int key);

#endif
