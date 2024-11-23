#ifndef USERINPUT_H_
#define USERINPUT_H_

int editorReadKey(void);

char *editorPrompt(char *prompt, void (*callback)(char *, int));

void editorProcessKeypress(void);

void editorStartSelecting(void);

int editorIsSelecting(void);

int isInSelection(int x, int y);

#endif
