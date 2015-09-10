#include <syscall.h>
#include <types.h>
#include <lib.h>
#include <kern/unistd.h>
#include <uio.h>
#include <curthread.h>
#include <thread.h>
#include <vnode.h>
#include <vfs.h>
#include <test.h>

// write 6
int sys_write(int filehandle, const void *buf, size_t size) {
	(void) filehandle;
	(void) size;
	// for now we only support std out,
	// so int filehandle is not needed 
	// (it should be STDOUT_FILENO when sys_write is called by user)
	// check the function backtrace for printf
	// [write(STDOUT_FILENO, &c, 1)]
	char out = *(char *)(buf);
	// kprintf corresponds to filehandle = STDOUT_FILENO
	return kprintf("%c", out);
}

// read 5
int sys_read(int filehandle, void *buf, size_t size, int *retval) {
	// in menu.c, when you read from console,
	// you call kget(), and kget is implemented
	// based on getch() (get one char at a time ?)
	(void)filehandle;
	char buff[size+1];
	kgets_syscall(buff, size, retval);
	buff[size] = '\0';
	//kprintf("%c", buf[0]);
	copyout(&buff, buf, size+1);
	return 0;
}


