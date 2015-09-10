/* 
 * stoplight.c
 *
 * 31-1-2003 : GWA : Stub functions created for CS161 Asst1.
 *
 * NB: You can use any synchronization primitives available to solve
 * the stoplight problem in this file.
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

/*
 *
 * Constants
 *
 */

/*
 * Number of cars created.
 */

#define NCARS 20


/*
 *
 * Function Definitions
 *
 */

static const char *directions[] = { "N", "E", "S", "W" };

static const char *msgs[] = {
        "approaching:",
        "region1:    ",
        "region2:    ",
        "region3:    ",
        "leaving:    "
};

/* use these constants for the first parameter of message */
enum { APPROACHING, REGION1, REGION2, REGION3, LEAVING };

int numcarfinished;

static void
message(int msg_nr, int carnumber, int cardirection, int destdirection)
{
        kprintf("%s car = %2d, direction = %s, destination = %s\n",
                msgs[msg_nr], carnumber,
                directions[cardirection], directions[destdirection]);
}

/*
 * at any moment, there can only be one car entering the portion,
 * or, there can easily be a deadlock.
 */
struct lock *mutex;

/*
 * the lock only for displaying message
 */
struct lock *mesg;
/*
 * the lock only for adding new car to the approaching_queue
 */ 
struct lock *queue_mutex[4];

/*
 * the lock is called when that portion of 
 * the intersection is currently occupied.
 * 0: NE	1: ES
 * 2: SW	3: NW
 */
struct lock *portion[4];

struct lock *finish;

/*
 * the indicator to prevent deadlock.
 * a deadlock happens when all 4 portions are occupied, 
 * and none of 4 cars are leaving in the next move.
 * this var should never reach 4, and it is used when
 * a new car attempts to enter the intersection. 
 */
int deadlockchecker;


void init_stoplight() {
	portion[0] = lock_create("NE portion");
	portion[1] = lock_create("SE portion");
	portion[2] = lock_create("SW portion");
	portion[3] = lock_create("NW portion");
	
	mutex = lock_create("new car mutex");
	mesg = lock_create("synch mesg displaying");

	queue_mutex[0] = lock_create("synch adding new car to queue 0");
	queue_mutex[1] = lock_create("synch adding new car to queue 1");
	queue_mutex[2] = lock_create("synch adding new car to queue 2");
	queue_mutex[3] = lock_create("synch adding new car to queue 3");

	finish = lock_create("print the finish mesg");

	deadlockchecker = 0;
	numcarfinished = 0;
	
}

/*
 *
 */
//struct lock *transient;

/*
 * the lock is used to prevent cars from passing
 * each other heading the same way. 
 * 0: N		1: E
 * 2: S		3: S
 */
/*
struct lock *mutex[4];

void init_mutex_lock() {
	mutex[0] = lock_create("north mutex");
	mutex[1] = lock_create("east mutex");
	mutex[2] = lock_create("south mutex");
	mutex[3] = lock_create("west mutex");

}
*/

/*
 * keeps the list of incoming cars from 4 directions, 
 * the cars approaching later will be appended at the tail.
 * this list is to ensure that the order of entering the 
 * intersection is consistent with the order of approaching. 
 */
struct approaching_queue {
	unsigned long carnumber;
	unsigned long cardirection;
	struct approaching_queue* next;
} *queue_head[4], *queue_tail[4];

/*
 * the pt points to the car that's ready to enter the intersection.
 */
struct approaching_queue *next_entrance[4];

/*
 * initialize the 4 approaching_queue, add a dummy node
 * this is to prevent the lost of next_entrance pointer
 */
void init_approaching_queue () {
	int i;
	for (i = 0; i < 4; i ++ ) {
		queue_head[i] = (struct approaching_queue*)kmalloc(sizeof (struct approaching_queue));
		queue_head[i]->carnumber = -1;
		queue_tail[i] = queue_head[i];
		next_entrance[i] = queue_head[i];
	}
}

/*
 * function to handle first entry into the intersection;
 * this function doesn't care the direction or destination.
 * mode: 1: when the car wants to go straight or make a left turn
 * 	 0: when the car wants to make a right turn
 * turnoffset: the offset added to cardirection so that we can get 
 * 	       the correct destination number based on diff turns. 
 * note that a right turn will never cause a deadlock. 
 */
void attempt_to_enter (unsigned long cardirection, unsigned long carnumber, int mode, int turnoffset) {
	while (1) {
		lock_acquire(mutex);
		if (next_entrance[cardirection]->carnumber == carnumber) {
			lock_acquire(portion[(cardirection+3)%4]);
			//check if entering this portion will cause deadlock
			//if so, then discard, and wait for the next round
			//if not, then enter the portion. 
			if (mode == 1 && deadlockchecker == 3) {
				lock_release(portion[(cardirection+3)%4]);
				lock_release(mutex);
				continue;
			}
			next_entrance[cardirection] = next_entrance[cardirection]->next;
			if (mode == 1) {
				deadlockchecker ++ ;
			}
			//enter successfully, print message
			lock_acquire(mesg);
			message(1, carnumber, cardirection, (cardirection+turnoffset)%4);
			lock_release(mesg);
			lock_release(mutex);
			break;
		}
		lock_release(mutex);
	}

}


/*
 * gostraight()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement passing straight through the
 *      intersection from any direction.
 *      Write and comment this function.
 */
static
void
gostraight(unsigned long cardirection,
           unsigned long carnumber)
{	// ===========================================================
	// step 1: enter the first portion:
	// requirement: cars approaching later should not enter first
	//============================================================
	attempt_to_enter(cardirection, carnumber, 1, 2);
	// ===========================================================
	//step 2
	// ===========================================================
	lock_acquire(portion[(cardirection+2)%4]);
	deadlockchecker -- ;
	lock_acquire(mesg);
	message(2, carnumber, cardirection, (cardirection+2)%4);
	lock_release(mesg);
	lock_release(portion[(cardirection+3)%4]);
	// ===========================================================
	// step 3
	// ===========================================================
	lock_acquire(mesg);	
	message(4, carnumber, cardirection, (cardirection+2)%4);
	lock_release(mesg);
	lock_release(portion[(cardirection+2)%4]);

	lock_acquire(finish);
	numcarfinished ++ ;
	if (numcarfinished == NCARS)
		kprintf("ALL CARS FINISHED! \n");
	lock_release(finish);
}


/*
 * turnleft()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement making a left turn through the 
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
turnleft(unsigned long cardirection,
         unsigned long carnumber)
{
	// =========================================================
	// step 1
	// =========================================================
	attempt_to_enter(cardirection, carnumber, 1, 1);
	// =========================================================
	// step 2
	// =========================================================
	lock_acquire(portion[(cardirection+2)%4]);
	// deadlockchecker should not be decreased yet
	lock_acquire(mesg);
	message(2, carnumber, cardirection, (cardirection+1)%4);
	lock_release(mesg);
	lock_release(portion[(cardirection+3)%4]);
	// =========================================================
	// step 3
	// =========================================================
	lock_acquire(portion[(cardirection+1)%4]);
	deadlockchecker -- ;
	lock_acquire(mesg);
	message(3, carnumber, cardirection, (cardirection+1)%4);
	lock_release(mesg);
	lock_release(portion[(cardirection+2)%4]);
	// =========================================================
	// step 4
	// =========================================================
	lock_acquire(mesg);
	message(4, carnumber, cardirection, (cardirection+1)%4);
	lock_release(mesg);
	lock_release(portion[(cardirection+1)%4]);
	
	
	lock_acquire(finish);
	numcarfinished ++ ;
	if (numcarfinished == NCARS)
		kprintf("ALL CARS FINISHED! \n");
	lock_release(finish);

}


/*
 * turnright()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement making a right turn through the 
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
turnright(unsigned long cardirection,
          unsigned long carnumber)
{
	// =========================================================
	// step 1
	// =========================================================
	attempt_to_enter(cardirection, carnumber, 0, 3);
	// =========================================================
	// step 2
	// =========================================================
	lock_acquire(mesg);
	message(4, carnumber, cardirection, (cardirection+3)%4);
	lock_release(mesg);
	lock_release(portion[(cardirection+3)%4]);


	lock_acquire(finish);
	numcarfinished ++ ;
	if (numcarfinished == NCARS)
		kprintf("ALL CARS FINISHED! \n");
	lock_release(finish);

}


/*
 * approachintersection()
 *
 * Arguments: 
 *      void * unusedpointer: currently unused.
 *      unsigned long carnumber: holds car id number.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Change this function as necessary to implement your solution. These
 *      threads are created by createcars().  Each one must choose a direction
 *      randomly, approach the intersection, choose a turn randomly, and then
 *      complete that turn.  The code to choose a direction randomly is
 *      provided, the rest is left to you to implement.  Making a turn
 *      or going straight should be done by calling one of the functions
 *      above.
 */
 
static
void
approachintersection(void * unusedpointer,
                     unsigned long carnumber)
{
	(void) unusedpointer;
        
	// cardirection is set randomly.
        int cardirection = random() % 4;

	int turn = random() % 3;
	int dest;
	if (turn == 0) {
		dest = (cardirection+2)%4;
	} else if (turn == 1) {
		dest = (cardirection+1)%4;
	} else {
		dest = (cardirection+3)%4;
	}
	// we have to create a new dummy and update the info of new car
	// in the old dummy node. we cannot create a new node storing 
	// the info of new car, and insert it prior to the old dummy
	// the reason is that next_entrance may be pointing to old dummy 
	// before a new car is inserted to the queue. 
	// only the first method will prevend the lose next_entrance
	struct approaching_queue *newdummy = (struct approaching_queue*)kmalloc(sizeof (struct approaching_queue));
	newdummy->carnumber = -1;
	newdummy->cardirection = -1;
	newdummy->next = NULL;
	//adding new element to queue
	lock_acquire(queue_mutex[cardirection]);
	/*
	if (queue_head[cardirection] == NULL) {
		queue_head[cardirection] = newcar;
		queue_tail[cardirection] = newcar;
		next_entrance[cardirection] = newcar;
	} else {
		queue_tail[cardirection]->next = newcar;
		queue_tail[cardirection] = newcar;
		// the following if is intended to pick up the null 
		// next_entrance when a new car is approaching.
		// but it has a bug, if interleaving happens here:
		//i.e.: attempt_to_approach is called here, and then 
		// the car before the newcar and the newcar both finishes 
		// their crossing from beginning to end exactly here. 
		// then if the following if clause is executed, the newcar 
		// will have to run again. 
		// so the correct solution to prevent the lost of next_entrance
		// is to append a dummy node at the end.
		if (next_entrance[cardirection] == NULL) {
			next_entrance[cardirection]=newcar;
		}
	}
	*/
	queue_tail[cardirection]->next = newdummy;
	// NOTE: the order of assigning the new value to cardirection and carnumber matters!
	// in attempt_to_enter, the condition of while checks carnumber, so we have to 
	// assign cardirection prior to carnumber. 
	queue_tail[cardirection]->cardirection = cardirection;
	queue_tail[cardirection]->carnumber = carnumber;
	queue_tail[cardirection] = newdummy;
	lock_acquire(mesg);
	message(0, carnumber, cardirection, dest);
	lock_release(mesg);
	lock_release(queue_mutex[cardirection]);
	//making turn
	if (turn == 0) {
		gostraight(cardirection, carnumber);
	} else if (turn == 1) {
		turnleft(cardirection, carnumber);
	} else {
		turnright(cardirection, carnumber);
	}
}


/*
 * createcars()
 *
 * Arguments:
 *      int nargs: unused.
 *      char ** args: unused.
 *
 * Returns:
 *      0 on success.
 *
 * Notes:
 *      Driver code to start up the approachintersection() threads.  You are
 *      free to modiy this code as necessary for your solution.
 */

int
createcars(int nargs,
           char ** args)
{
        int index, error;

        /*
         * Avoid unused variable warnings.
         */

        (void) nargs;
        (void) args;

        /*
         * Start NCARS approachintersection() threads.
         */
	init_stoplight();
	init_approaching_queue();
        for (index = 0; index < NCARS; index++) {

                error = thread_fork("approachintersection thread",
                                    NULL,
                                    index,
                                    approachintersection,
                                    NULL
                                    );

                /*
                 * panic() on error.
                 */

                if (error) {
                        
                        panic("approachintersection: thread_fork failed: %s\n",
                              strerror(error)
                              );
                }
        }

        return 0;
}
