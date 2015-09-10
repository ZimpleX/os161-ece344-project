/*
 * catsem.c
 *
 * 30-1-2003 : GWA : Stub functions created for CS161 Asst1.
 *
 * NB: Please use SEMAPHORES to solve the cat syncronization problem in 
 * this file.
 */


/*
 * 
 * Includes
 *
 */

#include <types.h>
#include <lib.h>
#include <test.h>
#include <thread.h>
#include <synch.h>
#include <scheduler.h>

/*
 * 
 * Constants
 *
 */

/*
 * Number of food bowls.
 */

#define NFOODBOWLS 2

/*
 * Number of cats.
 */

#define NCATS 6

/*
 * Number of mice.
 */

#define NMICE 2

/*
 * Number of times a mouse or cat has to eat
 */

#define NEAT 4
//name for the semaphore
const char *sem_name = "cat_mice_mutex_sem";
const char *p_sem_name = "print_mutex";
//1 for cat, 0 for mouse, -1 for nobody. 
int species; 
//store the number of cats or mice at the bowl. can be 0, 1 or 2
int count;
int bowlstatus[NFOODBOWLS];
struct semaphore *mutex;
struct semaphore *p_mutex;

int catfinishcount;
int mousefinishcount;

/*
 * initialize all values
 * initialize all bowls to be empty.
 * status = 0 -> empty
 * status = 1 -> full
 */

void init_catsem() {
	int i;
	for (i = 0; i < NFOODBOWLS; i++){
		bowlstatus[i] = 0;
	}
	species = -1;
	count = 0;
	catfinishcount = 0;
	mousefinishcount = 0;
	mutex = sem_create(sem_name, 1);
	p_mutex = sem_create(p_sem_name, 1);		
}

/*
 * 
 * Function Definitions
 * 
 */

/* who should be "cat" or "mouse" */
static void
sem_eat(const char *who, int num, int bowl, int iteration)
{
	P(p_mutex);
        kprintf("%s: %d starts eating: bowl %d, iteration %d\n", who, num, 
                bowl, iteration);
	V(p_mutex);

        clocksleep(1);
	
	P(p_mutex);
        kprintf("%s: %d ends eating: bowl %d, iteration %d\n", who, num, 
                bowl, iteration);
	V(p_mutex);
}

/*
 * generate a bowl number according to the current empty bowls, and set the status of that bowl
 * return the newly occupied bowl number
 */
int get_empty_bowl () {
	int i;
	for (i = 0; i < NFOODBOWLS; i++) {
		if (bowlstatus[i] == 0) {
			bowlstatus[i]=1;
			return (i+1);
		}
	}
	return -1;
}

/*
 * free the bowl after the species finishes eating
 */
void free_bowl (int bowl_num) {
	bowlstatus[bowl_num-1] = 0;
}

/*
 * catsem()
 *
 * Arguments:
 *      void * unusedpointer: currently unused.
 *      unsigned long catnumber: holds the cat identifier from 0 to NCATS - 1.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Write and comment this function using semaphores.
 *
 */

static
void
catsem(void * unusedpointer, 
       unsigned long catnumber)
{
	(void)unusedpointer;
       	int i = 0;
	int bowl;
	while (i < NEAT) {
		P(mutex);
		if ((species == 1 || species == -1) && (count < NFOODBOWLS)) {
			species = 1;
			count ++ ;
			bowl = get_empty_bowl();
		} else {
			V(mutex);
			continue;
		}
		V(mutex);
		// ===================================================
		//const char *tcat[6] = {"c0", "c1", "c2", "c3", "c4", "c5"};
		//print_all_thread();
		// ===================================================
	
		sem_eat ("cat", (int)catnumber, bowl, i);
	
		P(mutex);
		count -- ;
		if (count == 0) {
			species = -1;
		}
		free_bowl (bowl);
		V(mutex);
		i++;
	}
	P(mutex);
	catfinishcount ++ ;
	if (catfinishcount == 6) {
		kprintf("CAT FINISH!\n");
	}
	V(mutex);
	// thread_exit() is not needed:
	// when the thread exits, it will return in mi_threadstart();
	// and thread_exit() is called automatically in mi_threadstart().
}
        

/*
 * mousesem()
 *
 * Arguments:
 *      void * unusedpointer: currently unused.
 *      unsigned long mousenumber: holds the mouse identifier from 0 to 
 *              NMICE - 1.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Write and comment this function using semaphores.
 *
 */

static
void
mousesem(void * unusedpointer, 
         unsigned long mousenumber)
{
	(void)unusedpointer;
        int i = 0;
	int bowl;	//the bowl number which the mouse is consuming
	while (i < NEAT) {
		P(mutex);
		//check bowls left, check species
		if ((species == 0 || species == -1) && count < NFOODBOWLS) {
			species = 0;
			count ++ ;
			bowl = get_empty_bowl();
		} else {
			V(mutex);
			continue;	//continue without i++
		}
		V(mutex);
		// ================================================	
		//const char *tmouse[2] = {"m0", "m1"};
		//print_all_thread();
		// ================================================
	
		sem_eat ("mouse", (int)mousenumber, bowl, i);

		P(mutex);
		count -- ;
		if (count == 0) {
			species = -1;
		}
		free_bowl (bowl);
		V(mutex);
		i ++ ;	
	}
	//thread_exit();
	P(mutex);
	mousefinishcount ++ ;
	if (mousefinishcount == 2) {
		kprintf("MOUSE FINISH!\n");
	}
	V(mutex);
}


/*
 * catmousesem()
 *
 * Arguments:
 *      int nargs: unused.
 *      char ** args: unused.
 *
 * Returns:
 *      0 on success.
 *
 * Notes:
 *      Driver code to start up catsem() and mousesem() threads.  Change this 
 *      code as necessary for your solution.
 */

int
catmousesem(int nargs,
            char ** args)
{
        int index, error;
   
        /*
         * Avoid unused variable warnings.
         */

        (void) nargs;
        (void) args;
   	const char *tcat[6] = {"c0", "c1", "c2", "c3", "c4", "c5"};
	const char *tmouse[2] = {"m0", "m1"};
	init_catsem();
	P(mutex);	
        /*
         * Start NCATS catsem() threads.
         */
        for (index = 0; index < NCATS; index++) {
           
                error = thread_fork(tcat[index], 
                                    NULL, 
                                    index, 
                                    catsem, 
                                    NULL
                                    );
                
                /*
                 * panic() on error.
                 */

                if (error) {
                 
                        panic("catsem: thread_fork failed: %s\n", 
                              strerror(error)
                              );
                }
        }
        /*
         * Start NMICE mousesem() threads.
         */

        for (index = 0; index < NMICE; index++) {
   
                error = thread_fork(tmouse[index], 
                                    NULL, 
                                    index, 
                                    mousesem, 
                                    NULL
                                    );
                
                /*
                 * panic() on error.
                 */

                if (error) {
         
                        panic("mousesem: thread_fork failed: %s\n", 
                              strerror(error)
                              );
                }
        }
	// ===============================================
	//print_run_queue();
	// ===============================================

	V(mutex);
	return 0;
}


/*
 * End of catsem.c
 */
