#ifndef _SYSCALL_H_
#define _SYSCALL_H_
#include <types.h>
#include <machine/trapframe.h>
/*
 * Prototypes for IN-KERNEL entry points for system call implementations.
 */

int sys_reboot(int code);
// -----------------------------
int sys_write(int filehandle, const void *buf, size_t size);
int sys_read(int filehandle, void *buf, size_t size, int *retval);
int sys_fork(struct trapframe *tf, int32_t *retval);
int sys_getpid(int32_t *retval);
int sys_waitpid(pid_t child_pid, struct trapframe *tf, int32_t *retval);
int sys__exit(struct trapframe *tf, int32_t *retval, int code);
int sys_execv(char *prog, char *const *args, int32_t *retval);
int sys_sbrk(int size, int32_t *retval);

#endif /* _SYSCALL_H_ */
