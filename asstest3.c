/*
 * tanimate.c: animate several strings using threads, curses, usleep()
 *
 *	bigidea one thread for each animated string
 *		one thread for keyboard control
 *		shared variables for communication
 *	compile	cc ass3.c -lcurses -lpthread -o ass3
 *	to do   needs locks for shared variables
 *	        nice to put screen handling in its own thread
 */

#include <stdio.h>
#include <curses.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/time.h>
#include <sys/resource.h>

/* limit of number of rows with saucers */
#define NUMROW 3

/* number of initial saucers to display at the start of the program */
#define	NUMSAUCERS 3

/* the maximum number of saucers at one time */
#define MAXSAUCERS 20

/* the maximum number of shot threads */
#define MAXSHOTS 100

/* higher number = less likely to have random saucers appear */
#define RANDSAUCERS 20

/* limit of number of shots allowed */
#define NUMSHOTS 5

/* timeunits in microseconds */
#define	TUNIT 20000

struct saucerprop{
	char *str;	/* the saucer string */
	int row;	/* the row position on screen */
	int delay;  	/* delay in time units */
	int index;	/* element # in thrd array */

};
/*
struct shotprop{
	int col;	
	int row;
	int num;	
};	*/
		
int escape_update = 0;
int rockets_update = NUMSHOTS;
int score_update = 0;

/* holds the element number of the thread that can be replaced */
int replace_index = 7;
pthread_cond_t replace_condition = PTHREAD_COND_INITIALIZER;

/* static mutex with default attributes */
pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t score_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t replace_mutex = PTHREAD_MUTEX_INITIALIZER;

/* stores the threads */
pthread_t thrds[MAXSAUCERS];

/* for storing the properties of saucers and shots */
struct saucerprop saucerinfo[MAXSAUCERS];

int main(int ac, char *av[]){
	
	int i, c;
	
	/* id for the thread that handles assigning replacements */
	pthread_t replace_t;

	//struct shotprop shotinfo[NUMSHOTS];
	struct rlimit rlim;

	/* arrays for saucers and shots */
	//char *saucerarray[NUMSAUCERS];
	//char *shotarray[NUMSHOTS];
	
	/* function prototypes */
	void stats();
	void *saucers();
	void *shots();
	void *replace_thread();
	void setup_saucer();

        /* number of saucers */
	int nsaucers = NUMSAUCERS;

	/* no arguments should be included for this program */
	if (ac != 1){
		printf("usage: saucer\n");
		exit(1);
	}
	
	/* make sure the system allows enough processes to play the game */
	getrlimit(RLIMIT_NPROC, &rlim);
	if (rlim.rlim_cur < MAXSAUCERS + MAXSHOTS){
		fprintf(stderr,
		"Your system does not allow enough processes for this game\n");
		exit(1);
	}
	
	/* create a new thread to handle the other threads */
	if (pthread_create(&replace_t, NULL, replace_thread, NULL)){
		/* if thread is not created exit */
		fprintf(stderr,"error creating replacement control thread");
		exit(-1);
	} 
	
	/* set up curses */
	initscr();
	crmode();
	noecho();
	clear();
	
	/* print message with info about the game @ the bottom of the page */
	stats();

//MUTEX??
	/* create a thread for each initial saucer */
	for(i=0; i<NUMSAUCERS; i++){
		
		/* populate saucerinfo for the initial saucers */
		setup_saucer(i);
		
		/* each thread is created - runs/exits in saucers function */
		if (pthread_create(&thrds[i], NULL, saucers, &saucerinfo[i])){
			fprintf(stderr,"error creating saucer thread");
			endwin();
			exit(-1);
		}
	}
	
	/* process user input */
	while(1){
		
		/* Add more saucers at random */
		/* The more shots taken, the more saucers added */
		if(rand()%RANDSAUCERS == 0 && nsaucers < MAXSAUCERS){
			setup_saucer(nsaucers);
			nsaucers ++;
		}
		
		/* read character from input and store in variable c */
		c = getch();

		/* quit program */
		if(c == 'Q'){
			break;
		}
		
//change		/* change direction of all saucers */
		if(c == ' '){

		}

//change		/* change direction of specific saucer if it exists */
		if(c >= '0' && c <= '9'){
			i = c - '0';
		}
	}

	/* cancel all the threads */
/*mx?*/	pthread_mutex_lock(&mx);
	for(i=0; i<nsaucers; i++){
		pthread_cancel(thrds[i]);
	}
	pthread_cancel(replace_t);
	endwin();
	return 0;
}

/* 
 * stats prints the number of rockets left and number of missed saucers to the screen
*/
void stats( ){
		
	/* print message at bottom of the screen */
	mvprintw(LINES-1,0,"score:%d, rockets remaining: %d, escaped saucers: %d", score_update, rockets_update, escape_update);
	refresh();
}


/* 
 * setup_saucer populates one element (specified by i) in saucerinfo
 */ 
void setup_saucer(int i){
	
	srand(getpid()+i);
	saucerinfo[i].str = "<--->";	
	saucerinfo[i].row = (i+3)%NUMROW;
	saucerinfo[i].delay = 1+(rand()%15);
	saucerinfo[i].index = i;
}

/* 
 * saucers is the function used by all the saucer threads
 * is type void* and recieves argument void* because of the requirements of pthread_create
 * in this program, recieves the address of the corresponding saucer properties
 * in this program, returns nothing
 */
void *saucers(void *properties){	
	
	struct saucerprop *info = properties;	/* point to properties info for the saucer */
	int len = strlen(info->str) + 2;	/* size of the saucer +2 (for padding) */
/*for testing only*/	int col = 0;	
//int col = -1*rand()%(COLS-len-3);	/* random column to start at */
	void *retval;
	int len2 = len;
	//saucerinfo[info->index].id = pthread_self();
	
	while(1){
		move(LINES-1, COLS-1);
		
		/* thread sleeps for (its delay time * defined timeunits) */
		usleep(info->delay*TUNIT);

		/* lock the mutex mx CRITICAL REGION BELOW */
/*?mx*/		pthread_mutex_lock(&mx);
		
		/* move cursor to position (row, col) */
		move(info->row, col);

		/* place ' '"<--->"' ' at the new position (row, col) */
		addch(' ');
		addnstr(info->str, len2);
		addch(' ');

		/* move cursor back and output changes on the screen */
		move(LINES-1, COLS-1);
		refresh();

		/* unlock mutex protecting critical region */
		pthread_mutex_unlock(&mx);

		/* move to next column */
		col ++;
		
		/* when we reach the end of the screen start to stop writing */
		if (col+len >= COLS){
			
			/* @ end - write progressively less of the string */
			len2 = len2-1;
			
			/* now the string is off the page, exit the thread */
			if(len2 < 0){

				/* update the score now that a saucer escaped */
				pthread_mutex_lock(&score_mutex);
				escape_update ++;
				stats();
				pthread_mutex_unlock(&score_mutex);
				
				/* signal that the thread can be replaced */
				pthread_mutex_lock(&replace_mutex);
				
				/* set global to index that can be replaced */
				replace_index = info->index;
				pthread_cond_signal(&replace_condition);
				pthread_mutex_unlock(&replace_mutex);
				
				/* now we are finished with the thread */
				pthread_exit(retval);
			}
		}	
	}
}


void *replace_thread(){
	
	void **retval;
	void *retval2;
	
	
	while(1){

	/* wait until given the signal to replace a thread */
	pthread_mutex_lock(&replace_mutex);
	pthread_cond_wait(&replace_condition, &replace_mutex);
	
	mvprintw(8, 0, "first checkpoint, ");
	refresh();
	
	//pthread_join(thrds[replace_index], retval);
	
	mvprintw(9, 0, "second checkpoint");
	refresh();
	//if(pthread_cancel(saucerinfo->id)!=0){
	/* leave some time between last to exit screen and new */
	//sleep(5);
	
/*testing purposes*/
	mvprintw(10, 0, "index: %d", replace_index);
	refresh();
	//}
//else mvprintw(11, 0, "cancel didn't work");
	
	pthread_mutex_unlock(&replace_mutex);
	}
}

void *shots(void *properties){
	return "blah";
}

