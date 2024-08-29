#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void disableRawMode(void) {
    /* Restore terminal flags */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode(void) {
    /* Put the terminal in raw mode.
    
    Remove all preprocessing of input text.
    */
    // Store original terminal settings 
    tcgetattr(STDIN_FILENO, &orig_termios);
    // Restore terminal at end of session
    atexit(disableRawMode);
    // We will store the raw settings here
    struct termios raw = orig_termios;
    // Get the current settings
    tcgetattr(STDIN_FILENO, &raw);

    ///////////
    // Flags //
    ///////////
    // Raw mode requires setting certain termios flags
    // https://man7.org/linux/man-pages/man3/termios.3.html
    raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                           | INLCR | IGNCR | ICRNL | IXON);
    raw.c_oflag &= ~OPOST;
    raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= ~(CSIZE | PARENB);
    raw.c_cflag |= CS8;
    //raw.c_iflag &= ~(IXON);  // CTRL-S & CTRL-Q
    //raw.c_lflag &= ~(ECHO | ICANON); // Read input byte-by-byte
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main(void) {
    enableRawMode();
    char c;
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
        if (iscntrl(c)) {
            printf("%d\n", c);
        } else {
            printf("%d (%c)\n", c, c);
        }
    }
	return 0;
}
