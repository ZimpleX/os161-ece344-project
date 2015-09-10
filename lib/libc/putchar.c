#include <stdio.h>
// unistd is included so that you can 
// translate user level syscall write() to kernel level sys_write()
#include <unistd.h>

/*
 * C standard function - print a single character.
 *
 * Properly, stdio is supposed to be buffered, but for present purposes
 * writing that code is not really worthwhile.
 */

int
putchar(int ch)
{
	char c = ch;
	int len;
	len = write(STDOUT_FILENO, &c, 1);
	if (len<=0) {
		return EOF;
	}
	return ch;
}
