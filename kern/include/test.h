#ifndef _TEST_H_
#define _TEST_H_

/*
 * Declarations for test code and other miscellaneous functions.
 */

/* These are only actually available if OPT_SYNCHPROBS is set. */
int catmousesem(int, char **);
int catmouselock(int, char **);
int createcars(int, char **);

/*
 * Test code.
 */

/* lib tests */
int arraytest(int, char **);
int bitmaptest(int, char **);
int queuetest(int, char **);

/* thread tests */
int threadtest(int, char **);
int threadtest2(int, char **);
int threadtest3(int, char **);
int semtest(int, char **);
int locktest(int, char **);
int cvtest(int, char **);

/* filesystem tests */
int fstest(int, char **);
int readstress(int, char **);
int writestress(int, char **);
int writestress2(int, char **);
int createstress(int, char **);
int printfile(int, char **);

/* other tests */
int malloctest(int, char **);
int mallocstress(int, char **);
int nettest(int, char **);

/* Kernel menu system */
void menu(char *argstr);

/*
 * struct to pass program name and args
 * to runprogram, to compatible with thread_fork
 */
struct runprogram_info {
	char *progname;
	int argc;
	char **argv;
};

/* Routine for running userlevel test code. */
int runprogram(struct runprogram_info *prog_info);

#endif /* _TEST_H_ */
