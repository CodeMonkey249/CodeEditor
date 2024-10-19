#ifndef USERINPUT_H_
#define USERINPUT_H_

int editorReadKey(void);

char *editorPrompt(char *prompt, void (*callback)(char *, int));

void editorProcessKeypress(void);

#endif
