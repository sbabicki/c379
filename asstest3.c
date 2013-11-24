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
#define NUMROW 5

/* number of initial saucers to display at the start of the program */
#define	NUMSAUCERS 5

/* number of initial shots limit */
#define NUMSHOTS 5

/* the maximum number of saucers at one time */
#define MAXSAUCERS 10

/* the maximum number of shot threads */
#define MAXSHOTS 50

/* higher number = generally less likely to have random saucers appear */
#define RANDSAUCERS 50

/* timeunits in microseconds */
#define	TUNIT 20000
#define SHOTSPEED 60000

struct saucerprop{
	int row;	/* the row position on screen */
	int delay;  	/* delay in time units */
	int index;	/* element # in thrd array */
	int col1;
	int col2;

};

struct shotprop{
	int col;	
	int row;
	int index;	
};

struct screen{
	int shot;
	int saucer;
};

/* collision detection array */
struct screen **collision_position;
		
int escape_update = 0;
int rockets_update = NUMSHOTS;
int score_update = 0;

/* holds the element number of the thread that can be replaced */
int replace_index;
pthread_cond_t replace_condition = PTHREAD_COND_INITIALIZER;

/* static mutex with default attributes */
pthread_mutex_t draw = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t score_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t replace_mutex = PTHREAD_MUTEX_INITIALIZER;

/* stores the threads */
pthread_t thrds[MAXSAUCERS];
pthread_t shot_t[MAXSHOTS];

/* for storing the properties of saucers and shots */
struct saucerprop saucerinfo[MAXSAUCERS];
struct shotprop shotinfo[MAXSHOTS];


int main(int ac, char *av[]){
	
	int i, c;
	int launch_position;
	int shot_index = 0;
	
	//void *collision_array;
	
	/* collision detection */
	//struct screen collision[LINES-1][COLS-1];
	
	/* id for the thread that handles assigning replacements */
	pthread_t replace_t;

	//struct shotprop shotinfo[NUMSHOTS];
	struct rlimit rlim;
	
	/* for creating the 2D array used for collision detection */
	struct screen **array;
	struct screen *data;

	/* arrays for saucers and shots */
	//char *saucerarray[NUMSAUCERS];
	//char *shotarray[NUMSHOTS];
	
	/* function prototypes */
	void stats();
	void *saucers();
	void *shots();
	void *replace_thread();
	int launch_site();
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
	
	/* found this section of code from http://bit.ly/19Px1R4 */
	/* creates a 2D array */
/*NUMROW*/	array = calloc(LINES-1, sizeof(*array));
/*NUMROW*/	data = calloc((LINES-1) * (COLS), sizeof(*data));
	
	/* error checking: calloc returns NULL if it failed */
	if(array == NULL || data == NULL) {
		fprintf(stderr, "calloc failed, maybe we ran out of memory :(");
		exit(-1);
	}
	
	/* connect the rows and cols, now we can use array[i][j] */
/*NUMROW*/	for(i = 0; i < (LINES-1); i++){
		array[i] = &data[i * (COLS-1)];
	}
	/* end section from stackoverflow */
	
	/* collision_position is a global so any function can access array */
	collision_position = array;
	
	/* print message with info about the game @ the bottom of the page */
	stats();
	
	/* draw original launch site in the middle of the screen */
	launch_position = (COLS-1)/2;
	launch_site(0, launch_position);
	
	/* set a seed so rand() results will be different each game */
	srand(getpid());
	
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
			if (pthread_create(&thrds[nsaucers], NULL, saucers, &saucerinfo[nsaucers])){
				fprintf(stderr,"error creating saucer thread");
				endwin();
				exit(-1);
			}
			nsaucers ++;
		}
		
		/* read character from input and store in variable c */
		c = getch();

		/* quit program */
		if(c == 'Q'){
			break;
		}
		
		
		/* move launch site to the left */
		if(c == ','){
			launch_position = launch_site(-1, launch_position);
		}
		
		/* move launch site to the right */
		else if(c == '.'){
			launch_position = launch_site(1, launch_position);
		}
		
		
		/* fire one shot */
		else if(c == ' '){
			
			/* loop to begining of the array */
			if(shot_index == MAXSHOTS){
				shot_index = 0;
			}
			/* set cols/index. pos + 1 b/c of the space before | */
			shotinfo[shot_index].col = launch_position + 1;
			shotinfo[shot_index].index = shot_index;
			pthread_create(&shot_t[shot_index], NULL, shots, &shotinfo[shot_index]);
			shot_index ++;
		}
		
/*
		if(c >= '0' && c <= '9'){
			i = c - '0';
		}
*/	
	}

	/* cancel all the threads */
/*mx?*/	pthread_mutex_lock(&draw);
	for(i=0; i<nsaucers; i++){
		pthread_cancel(thrds[i]);
	}
	pthread_cancel(replace_t);
	
	/* free allocated memory */
	free(array[0]);
	free(array);
	
	/* close curses */
	endwin();
	return 0;
}

/* 
 * stats prints the # of rockets left and # of missed saucers to the screen
 */
void stats( ){
		
	/* print message at bottom of the screen */
	pthread_mutex_lock(&draw);
	move(LINES-1, COLS-1);
	mvprintw(LINES-1,0,"score:%d, rockets remaining: %d, escaped saucers: %d", score_update, rockets_update, escape_update);
	move(LINES-1, COLS-1);
	refresh();
	pthread_mutex_unlock(&draw);
}


/* 
 * setup_saucer populates one element (specified by i) in saucerinfo
 */ 
void setup_saucer(int i){

	saucerinfo[i].row = (rand())%NUMROW;
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
	
	int i;
	struct saucerprop *info = properties;	/* point to properties info for the saucer */
	char *shape = " <--->";
	int len = strlen(shape);
	int len2 = len;
	int col = 0;	
	void *retval;
	
	while(1){
		
		/* thread sleeps for (its delay time * defined timeunits) */
		usleep(info->delay*TUNIT);

		/* lock the mutex draw CRITICAL REGION BELOW */
		pthread_mutex_lock(&draw);
		move(LINES-1, COLS-1);
		/* print the saucer on the screen at (row, col) */
		mvaddnstr(info->row, col, shape, len2);
		
		/* move cursor back and output changes on the screen */
		move(LINES-1, COLS-1);
		refresh();
		
		/* update collision array. len-1 because of the extra space */
		for(i=0; i<len-1; i++){
			
			/* add new position and remove old position */
			collision_position[info->row][col+1+i].saucer ++;
			if(col>0){
				collision_position[info->row][col+i].saucer --;
			}
		}
		
		/* for testing */
		/* mvprintw(3, col, "%d", 
		collision_position[info->row][col].saucer); 
		mvprintw(2, col, "%d", 
		collision_position[info->row][col+1].saucer); */
		
		/* for testing */
		/*
		for(i=0;i<COLS-1; i++){
			mvprintw((info->row)+NUMROW+1, i, "%d", 
			collision_position[info->row][i].saucer);
		} 
		refresh(); 
	*/
		/* unlock mutex protecting critical region */
		pthread_mutex_unlock(&draw);

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

/*
 * replace_thread is run constantly by one thread
 * it replaces a saucer thread that has finished running with a new saucer 
 * allows many threads to be created but only a fixed number of active threads
 * and a set amount of threads to be stored in an array
 */
void *replace_thread(){
	
	void *retval;
	
	while(1){

		/* wait until given the signal to replace a thread */
		pthread_mutex_lock(&replace_mutex);
		pthread_cond_wait(&replace_condition, &replace_mutex);
	
/* testing purposes */	
/* mvprintw(10, 0, "index: %d", replace_index);
refresh(); */
	
		/* wait until a thread terminates for sure before replacing it */
		pthread_join(thrds[replace_index], &retval);
	
		/* optional delay */
	 	//sleep(2); 
	
		/* populate a new saucer and create new thread reusing an old index */
		setup_saucer(replace_index);
		if (pthread_create(&thrds[replace_index], NULL, saucers, &saucerinfo[replace_index])){
			fprintf(stderr,"error creating saucer thread");
			endwin();
			exit(-1);
		}
	
/*testing purposes*/
/* mvprintw(10, 0, "index: %d", replace_index); 
refresh(); */
		pthread_mutex_unlock(&replace_mutex);
	}
}

int launch_site(int direction, int position){
	
	/* if we are within the range of the screen move to new position */
	if(position+direction >= 5 && position+direction < COLS-2){
		
		position = direction + position;
		
		pthread_mutex_lock(&draw);
		move(LINES-1, COLS-1);
		mvaddstr(LINES-2, position, " | ");
		
/* for testing */		
/* mvprintw(LINES - 4, 0, "launcher %d", position); 
*/
		
		/* move cursor back and output changes on the screen */
		move(LINES-1, COLS-1);
		refresh();

		/* unlock mutex protecting critical region */
		pthread_mutex_unlock(&draw);
	}
	return position;
}

/* 
 * shots is the function used by all the shot threads
 * prints shots on the screen
 * in this program, recieves the address of the corresponding shot properties
 * in this program, returns nothing
 */
void *shots(void *properties){
	
	int hit = 0;
	struct shotprop *info = properties;
	void *retval;
	
	/* initial row at bottom of screen */
	info->row = LINES - 3;
	
	/* update the score now that a shot has been used */
	pthread_mutex_lock(&score_mutex);
	rockets_update --;
	stats();
	pthread_mutex_unlock(&score_mutex);
	
/* for testing */ 
/* mvprintw(LINES - 3, 0, "column #%d, shot #%d", info->col, info->index);
*/

	while(1){
		
		/* specify the delay in SHOTSPEED */
		usleep(SHOTSPEED);
		
		pthread_mutex_lock(&draw);
		move(LINES-1, COLS-1);
		
		/* cover the old shot */
		mvaddch(info->row, info->col, ' ');
		
		/* remove the old position from the collision array */
		if( info->row >= 0 && info->row < LINES-1){
			collision_position[info->row][info->col].shot --;	
		}
		
		/* draw the new shot at the new position one row up */
		info->row --;
		mvaddch(info->row, info->col, '^');
		
		/* update the position in the collision array */
		if( info->row >= 0 && info->row < LINES-1){
			collision_position[info->row][info->col].shot ++;
			
			/* 1 shot + >= 0 saucers, depending on the saucers */
			hit = collision_position[info->row][info->col].shot + 
			collision_position[info->row][info->col].saucer;
		}
		
		/* move cursor back and output changes on the screen */
		move(LINES-1, COLS-1);
		refresh();
		pthread_mutex_unlock(&draw);
		
		/* update the score if there is a collision */
		if(hit >1){
			pthread_mutex_lock(&score_mutex);
			
			/* hit minus 1 because 1 is from the shot */
			score_update = score_update + hit-1;
			stats();
			
			/* reset hit so the shot can hit other saucers */
			hit = 0;
			pthread_mutex_unlock(&score_mutex);
		}
		
		/* if reach the top of the screen without hitting anything */
		if(info->row < 0){
			
			/* now we are finished with the thread */
			pthread_exit(retval);
		}
	}
}