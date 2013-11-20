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

/* limit of number of rows with saucers */
#define NUMROW 3

/* number of initial saucers to display at the start of the program */
#define	NUMSAUCERS 1

/* limit of number of shots allowed */
#define NUMSHOTS 5

/* timeunits in microseconds */
#define	TUNIT 20000

struct saucerprop{
	char *str;	/* the saucer string */
	int row;	/* the row position on screen */
	int delay;  	/* delay in time units */
	int end;	/* +1 or -1 */
	int hit;	/* -1 if not hit, +1 if hit */
	int thrdnum;	/* element # from thrd array for canceling thread */
};

struct shotprop{
	int col;	/* where the shot is */
	int row;
	int num;	/* what number shot */
};
		

/* static mutex with default attributes */
pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t escape = PTHREAD_MUTEX_INITIALIZER;

int main(int ac, char *av[])
{
	int i, c;
	
	/* stores the threads */
	pthread_t thrds[NUMSAUCERS+10];

	/* for storing the properties of saucers and shots */
	struct saucerprop saucerinfo[NUMSAUCERS+10];
	struct shotprop shotinfo[NUMSHOTS];

	/* arrays for saucers and shots */
	char *saucerarray[NUMSAUCERS];
	char *shotarray[NUMSHOTS];
	
	/* function prototypes */
	void stats();
	int more_saucers();
	void *saucers();
	void *shots();
	int setup();

        /* number of saucers */
	int numsaucers;

	/* no arguments should be included for this program */
	if (ac != 1){
		printf("usage: saucer\n");
		exit(1);
	}
	
	/* set up curses */
	initscr();
	crmode();
	noecho();
	clear();
	
	/* print message with info about the game @ the bottom of the page */
	stats(0, NUMSHOTS, 0);

	/* fill the saucer array with <---> strings */
	for(i=0; i<NUMSAUCERS; i++){
		saucerarray[i] = "<--->";
	}
	//mvprintw(i+5,0,"%s", saucerarray[i]);

	/* populate the array of saucerprop structs */
	numsaucers = setup(NUMSAUCERS, saucerarray, saucerinfo);

	/* create a thread for each saucer */
	for(i=0; i<numsaucers; i++){
		/* once each thread is created it calls and stays in the saucers function */
		if (pthread_create(&thrds[i], NULL, saucers, &saucerinfo[i])){

			/* if thread is not created exit */
			fprintf(stderr,"error creating thread");
			endwin();
			exit(0);
		}
	}
	
	/* process user input */
	while(1){
		
		/* Add more saucers at random */
		/* The more shots taken, the more saucers added */
		if(rand()%20 == 0 && numsaucers < NUMSAUCERS+9){
			numsaucers = more_saucers(numsaucers, thrds, saucerinfo);
		}
		
		/* read character from input and store in variable c */
		c = getch();

		/* quit program */
		if(c == 'Q'){
			break;
		}
		
		/* change direction of all saucers */
		if(c == ' '){
			for(i=0; i<numsaucers; i++){
				saucerinfo[i].end = -saucerinfo[i].end;
			}
		}

		/* change direction of specific saucer if it exists */
		if(c >= '0' && c <= '9'){
			i = c - '0';
			if (i < numsaucers){
				saucerinfo[i].end = -saucerinfo[i].end;
			}
		}

	}

	/* cancel all the threads */
	pthread_mutex_lock(&mx);
	for(i=0; i<numsaucers; i++){
		pthread_cancel(thrds[i]);
	}
	endwin();
	return 0;
}

/* 
 * setup populates a saucerprop struct 
 * requires the number of strings to include, an array of the strings, and the array of structs to populate
 * returns the number of strings in the struct
 */
int setup(int nstrings, char *strings[], struct saucerprop saucerinfo[])
{
	int numsaucers = ( nstrings > NUMSAUCERS ? NUMSAUCERS : nstrings );
	int i;

	/* assign rows and velocities to each saucer */
	srand(getpid());
	for(i=0 ; i<numsaucers; i++){
		saucerinfo[i].str = strings[i];	/* <---> */
		saucerinfo[i].row = i%NUMROW;	/* the row */
		saucerinfo[i].delay = 1+(rand()%15);	/* a speed */
		saucerinfo[i].end = 1;	/* moving right */
		saucerinfo[i].thrdnum = i;
	}

	/* set up curses 
	initscr();
	crmode();
	noecho();
	clear();
	mvprintw(LINES-1,0,"'Q' to quit, '0'..'%d' to bounce",numsaucers-1);
	*/
	return numsaucers;
}

/* 
 * saucers is the function used by all the saucer threads
 * is type void* and recieves argument void* because of the requirements of pthread_create
 * in this program, recieves the address of the corresponding saucer properties
 * in this program, returns nothing
 */
void *saucers(void *properties)
{
	struct saucerprop *info = properties;	/* point to properties info for the saucer */
	int len = strlen(info->str) + 2;	/* size of the saucer +2 (for padding) */
	int col = 0;	/* random column to start at */
	int index;
	//int col = -1*rand()%(COLS-len-3);	/* random column to start at */
	void *retval;
	
	int len2 = len;
	
	while(1){
		move(LINES-1, COLS-1);
		
		/* thread sleeps for (its delay time * defined timeunits) */
		usleep(info->delay*TUNIT);

		/* lock the mutex mx CRITICAL REGION BELOW */
		pthread_mutex_lock(&mx);
		
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
			
			/* write progressively less of the string */
			len2 = len2-1;
			
			/* now the string is off the page, exit the thread */
			if(len2 < 0){
				index = info->thrdnum;
				pthread_exit(retval);
			}
		}	
	}
}

/* 
 * more_saucers creates one new saucer and creates a thread for it
 * expects the number of rows, the threads array, and the saucer array
 * returns the updated number of saucers
 */
int more_saucers(int num, pthread_t *thrds, struct saucerprop *saucerinfo){

	/* srand(getpid()); */
	saucerinfo[num].str = "<-.->";	/* <---> */
	saucerinfo[num].row = num%NUMROW;	/* the row */
	saucerinfo[num].delay = 1+(rand()%15);	/* a speed */
	saucerinfo[num].end = 1;	/* moving right */
	saucerinfo[num].thrdnum = num;

	/* once each thread is created it calls and stays in the saucers function */
	if (pthread_create(&thrds[num], NULL, saucers, &saucerinfo[num])){
			
		/* if thread is not created exit */
		fprintf(stderr,"error creating thread");
		endwin();
		exit(0);
	}
	
	/* return the new number of saucers */
	num ++;	
	return num;
	
}

void *shots(void *properties){
	return "blah";
}

/* 
 * stats prints the number of rockets left and number of missed saucers to the screen
*/
void stats(int score, int rockets, int saucers){
		
	/* print message at bottom of the screen */
	mvprintw(LINES-1,0,"score:%d, rockets remaining: %d, escaped saucers: %d", score, rockets, saucers);
}
