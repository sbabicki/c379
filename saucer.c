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

/* CHANGABLE MACROS: */

/* the maximum number of rows with saucers on them */
#define NUMROW 5

/* number of initial saucers to display at the start of the program */
#define	NUMSAUCERS 5

/* the maximum number of saucers on screen at one time */
#define MAXSAUCERS 10

/* the maximum number of escaped saucers */
#define MAXESCAPE 50

/* higher number = less likely to have random saucers appear */
#define RANDSAUCERS 50

/* number of initial shots limit */
#define NUMSHOTS 20

/* delay for the saucers, higher number = slower saucers */
#define	SAUCERSPEED 20000

/* delay of the shots, higher number = slower shots */
#define SHOTSPEED 60000

/* END CHANGABLE MACROS */


/* the maximum number of shot threads */
#define MAXSHOTS 50

struct saucerprop{
	int row;	/* the row position on screen */
	int delay;  	/* delay in time units */
	int index;	/* element # in thrd array */
	int kill; 	/* 0 for survive 1 for kill */

};

struct shotprop{
	int col;	
	int row;
	int index;
	int kill;
};

struct screen{
	int shot;
	int saucer;
	int there[MAXSAUCERS];
};

/* collision detection array */
struct screen **collision_position;

/* score update variables */		
int escape_update = 0;
int shot_update = NUMSHOTS;
int score_update = 0;

/* holds the element number of the thread that can be replaced */
int replace_index;

/* condition variables */
pthread_cond_t replace_condition = PTHREAD_COND_INITIALIZER;
pthread_cond_t end_condition = PTHREAD_COND_INITIALIZER;

/* mutexes */
pthread_mutex_t draw = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t score_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t replace_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t end_mutex = PTHREAD_MUTEX_INITIALIZER;

/* arrays to store the threads */
pthread_t saucer_t[MAXSAUCERS];
pthread_t shot_t[MAXSHOTS];

/* for storing the properties of saucers and shots */
struct saucerprop saucerinfo[MAXSAUCERS];
struct shotprop shotinfo[MAXSHOTS];

/*
 * main does some simple error checking and setup as well as some closing tasks
 * when it gets a signal from other threads
 * expects no arguments
 */
int main(int ac, char *av[]){
	
	int i, c, r_padding, c_padding;
	void *retval;
	
	/* id for the thread that handles assigning replacements */
	pthread_t replace_t;
	
	/* id for the thread that processes user input */
	pthread_t input_t;
	
	/* for finding the maximum processes allowed at once on the computer */
	struct rlimit rlim;
	
	/* for creating the 2D array used for collision detection */
	struct screen **array;
	struct screen *data;
	
	/* function prototypes */
	void stats();
	void *saucers();
	void *shots();
	void *replace_thread();
	int launch_site();
	void setup_saucer();
	int welcome();
	void *process_input();

	/* no arguments should be included for this program */
	if (ac != 1){
		printf("usage: saucer\n");
		exit(1);
	}
	
	/* make sure the system allows enough processes to play the game */
	getrlimit(RLIMIT_NPROC, &rlim);
	if (rlim.rlim_cur < MAXSAUCERS + MAXSHOTS + 3){
		fprintf(stderr,
		"Your system does not allow enough processes for this game\n");
		exit(-1);
	}

	/* create a new thread to handle the other threads */
	if (pthread_create(&replace_t, NULL, replace_thread, NULL)){
		/* if thread is not created exit */
		fprintf(stderr,"error creating replacement control thread\n");
		exit(-1);
	}
	
	/* set up curses */
	initscr();
	crmode();
	noecho();
	clear();
	
	/* print opening message with instructions */
	welcome();
	
	/* found this section of code from http://bit.ly/19Px1R4 */
	/* creates a 2D array */
	array = calloc(LINES-1, sizeof(*array));
	data = calloc((LINES-1) * (COLS-1), sizeof(*data));
	
	/* error checking: calloc returns NULL if it failed */
	if(array == NULL || data == NULL) {
		fprintf(stderr, "calloc failed, maybe we ran out of memory \n");
		endwin();
		exit(-1);
	}
	
	/* connect the rows and cols, now we can use array[i][j] */
	for(i = 0; i < (LINES-1); i++){
		array[i] = &data[i * (COLS-1)];
	}
	/* end section from stackoverflow */
	
	/* collision_position is a global so any function can access array */
	collision_position = array;
	
	/* create a thread for handling user input */
	if (pthread_create(&input_t, NULL, process_input, NULL)){
		fprintf(stderr,"error creating saucer thread\n");
		endwin();
		exit(-1);
	}
	
	/* wait for 'Q', too many escaped saucers, or run out of rockets */
	pthread_mutex_lock(&end_mutex);
	pthread_cond_wait(&end_condition, &end_mutex);
	
	/* make sure the other threads won't keep updating the screen */
	pthread_mutex_lock(&draw);
	
	/* erase everything on the screen in prep for closing message */
	erase();
	
	/* padding for the sides of the messages */
	r_padding = LINES/2 - LINES/4;
	c_padding = COLS/2 - COLS/3;
	
	/* if the game ends by too many saucers escaping */
	if(escape_update == MAXESCAPE){
		
		/* print too many escaped saucers closing message */
		mvprintw(r_padding, c_padding, "TOO MANY SAUCERS ESCAPED :(");
		mvprintw(r_padding+1, c_padding, "Number saucers escaped: %d",  /* over */
		    escape_update);
	}
	
	/* if the game ends by running out of shots */
	else if(shot_update == 0){
		
		/* print ran out of rockets closing message */
		mvprintw(r_padding, c_padding, "YOU RAN OUT OF ROCKETS :(");
	}
	
	/* closing message */
	mvprintw(r_padding +2, c_padding, "Final score: %d", score_update);
	mvprintw(r_padding +3, c_padding, "Thanks for playing!");
	mvprintw(r_padding +5, c_padding, "(Press 'Q' to exit)");
	refresh();
	
	/* cancel all threads */
	pthread_cancel(replace_t);
	pthread_cancel(input_t);
	for(i=0; i<NUMSAUCERS; i++){
		pthread_cancel(saucer_t[i]);
	}
	for(i=0; i<MAXSHOTS; i++){
		pthread_cancel(shot_t[i]);
	}
	
	/* free allocated memory */
	free(array[0]);
	free(array);
	
	/* allow the user to exit the program */
	while(1){
		c = getch();
		if(c == 'Q'){
			break;
		}
	}

	/* close curses */
	endwin();
	return 0;
}

/*
 * process_input deals with the user input from the terminal
 * in this program process_input is run as a single thread
 * expects no arguments, no return value
 */
void *process_input(){
	
	int i, c;
	int launch_position = (COLS-1)/2;
	int nsaucers = NUMSAUCERS;
	int shot_index = 0;
	void *rval;
	
	/* function prototypes */
	void stats();
	void *saucers();
	void *shots();
	void *replace_thread();
	int launch_site();
	void setup_saucer();
	
	/* print message with info about the game @ the bottom of the page */
	stats();
	
	/* draw original launch site in the middle of the screen */
	launch_site(0, launch_position);
	
	/* set a seed so rand() results will be different each game */
	srand(getpid());
	
	/* create a thread for each initial saucer */
	for(i=0; i<NUMSAUCERS; i++){
		
		/* populate saucerinfo for the initial saucers */
		setup_saucer(i);
		
		/* each thread is created - runs/exits in saucers function */
		if(pthread_create(&saucer_t[i], NULL, saucers, &saucerinfo[i])){
			fprintf(stderr,"error creating saucer thread\n");
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
			if (pthread_create(&saucer_t[nsaucers], NULL, saucers, /* over */
			    &saucerinfo[nsaucers])){
				fprintf(stderr,"error creating saucer thrd\n");
				endwin();
				exit(-1);
			}
			
			/* new number of saucers */
			nsaucers ++;
		}
		
		/* read character from input and store in variable c */
		c = getch();

		/* quit program */
		if(c == 'Q'){
			
			/* exit thread and return to main function */
			pthread_mutex_lock(&end_mutex);
			pthread_cond_signal(&end_condition);
			pthread_mutex_unlock(&end_mutex);
			break;
		}
		
		/* move launch site to the left */
		else if(c == ','){
			launch_position = launch_site(-1, launch_position);
		}
		
		/* move launch site to the right */
		else if(c == '.'){
			launch_position = launch_site(1, launch_position);
		}
		
		/* fire one shot */
		else if(c == ' '){
			
			/* if we are not out of shots fire the next one */
			if(shot_update > 0){
				
				/* loop to begining of the array */
				if(shot_index == MAXSHOTS){
					shot_index = 0;
				}
				
				/* set row/col for the shot */
				/* pos + 1 b/c of the space before | */
				shotinfo[shot_index].col = launch_position + 1;
				shotinfo[shot_index].index = shot_index;
				
				/* create a thread for the shot */
				if(pthread_create(&shot_t[shot_index], NULL, /* over */
				    shots, &shotinfo[shot_index])){
					fprintf(stderr,"error creating shot t");
					endwin();
					exit(-1);
				}
				
				/* 1 or 0, depending on where shot thread is */
				if(shot_update <= 1){
					
					/* no chance of hitting other saucer */
					pthread_join(shot_t[shot_index], &rval);
					
					/* if the last shot fired: exit game */
					if(shot_update == 0){
						pthread_mutex_lock(&end_mutex);
						pthread_cond_signal(&end_condition); /* over */
						pthread_mutex_unlock(&end_mutex);
						break;
					}
				}
			
				/* move to next shot */
				shot_index ++;
			}	
		}
	}
	pthread_exit(rval);
}

/* 
 * stats prints the # of rockets left and # of missed saucers on the screen
 */
void stats( ){
		
	/* print message at bottom of the screen */
	pthread_mutex_lock(&draw);
	move(LINES-1, COLS-1);
	mvprintw(LINES-1,0,
	    "score:%d, rockets remaining: %d, escaped saucers: %d/%d    ", /* over */
	    score_update, shot_update, escape_update, MAXESCAPE);
	move(LINES-1, COLS-1);
	refresh();
	pthread_mutex_unlock(&draw);
}

/* 
 * setup_saucer populates one element (indexed at i) in saucerinfo
 * expects integer corresponding to the index, no return value
 */ 
void setup_saucer(int i){

	saucerinfo[i].row = (rand())%NUMROW;
	saucerinfo[i].delay = 1+(rand()%15);
	saucerinfo[i].index = i;
}

/* 
 * saucers is the function used by all the saucer threads
 * is type void* and recieves argument void* b/c requirements of pthread_create
 * in this program, recieves the address of the corresponding saucer properties
 * in this program, returns nothing
 */
void *saucers(void *properties){	
	
	int i;	
	void *retval;
	char *shape = " <--->";
	char *shape2= "      ";
	int len = strlen(shape);
	int len2 = len;
	int col = 0;
	
	/* points to properties info for a specific saucer */
	struct saucerprop *info = properties;
	
	/* update the saucers */
	while(1){
		
		/* thread sleeps for (its delay time * defined timeunits) */
		usleep(info->delay*SAUCERSPEED);

		/* lock the draw mutex CRITICAL REGION BELOW */
		pthread_mutex_lock(&draw);
		move(LINES-1, COLS-1);
		
		/* remove the saucer if kill is set for that saucer */
		if(info->kill == 1){
			move(LINES-1, COLS-1);
			
			/* draw over the saucer to remove it from the screen */
			mvaddnstr(info->row, col, shape2, len2);
			
			/* remove saucer position from the collision array */
			for(i = 0; i < len-1; i++){
				collision_position[info->row][col+i].saucer --;
				collision_position[info->row][col+i].there[info->index] = 0; /* over */
				info->kill = 0;
			}
			
			/* signal to replace the thread at that index */
			pthread_mutex_lock(&replace_mutex);
			
			/* set global to index that can be replaced */
			replace_index = info->index;
			pthread_cond_signal(&replace_condition);
			pthread_mutex_unlock(&replace_mutex);
			
			move(LINES-1, COLS-1);
			pthread_mutex_unlock(&draw);
			
			/* finish with the thread */
			pthread_exit(retval);
		}
		
		
		/* print the saucer on the screen at (row, col) */
		mvaddnstr(info->row, col, shape, len2);
		
		/* update collision array. len-1 because of the extra space */
		for(i=0; i<len2-1; i++){
			
			/* add new position and remove old position */
			collision_position[info->row][col+1+i].saucer ++;
			
			/* provide a sign that this saucer is at that spot */
			collision_position[info->row][col+1+i].there[info->index] = 1; /* over */
			
			/* remove old position if not first time through loop */
			if(col>0){
				collision_position[info->row][col+i].saucer --;
			}
		}
		
		/* only remove the one column that changed */
		collision_position[info->row][col].there[info->index] = 0;
		
		for(i = 0; i < COLS-1; i++){
			mvprintw(info->row+NUMROW, i, "%d", collision_position[info->row][i].saucer);
			//mvprintw(info->row+2*NUMROW+1, i, "%d", collision_position[info->row][i].there[info->index]);
		}
		
		/* move cursor back and output changes on the screen */
		move(LINES-1, COLS-1);
		refresh();

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
				
				/* if we have reached the max escaped saucers */
				if(escape_update == MAXESCAPE){
					
					/* send signal to the main function */
					pthread_mutex_lock(&end_mutex);
					pthread_cond_signal(&end_condition);
					pthread_mutex_unlock(&end_mutex);
					
					/* we are done with the thread now */
					pthread_exit(retval);
				}
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

		/* wait until thread terminates for sure before replacing it */
		pthread_join(saucer_t[replace_index], &retval);
	
		/* optional delay */
	 	/* sleep(2); */
	
		/* populate new saucer + create new thread reusing old index */
		setup_saucer(replace_index);
		if (pthread_create(&saucer_t[replace_index], NULL, saucers, /* over */
		    &saucerinfo[replace_index])){
			fprintf(stderr,"error creating saucer thread");
			endwin();
			exit(-1);
		}
		pthread_mutex_unlock(&replace_mutex);
	}
}

/* 
 * launch_site responds to user input that moves the launch site
 * expects new direction and old position of the shot, returns the new position
 */
int launch_site(int direction, int position){
	
	/* if we are within the range of the screen move to new position */
	if(position+direction >= 0 && position+direction < COLS-3){
		
		/* new position */
		position = direction + position;
		
		/* draw new position on screen */
		pthread_mutex_lock(&draw);
		move(LINES-1, COLS-1);
		mvaddstr(LINES-2, position, " | ");
	
		/* move cursor back and output changes on the screen */
		move(LINES-1, COLS-1);
		refresh();

		/* unlock mutex protecting critical region */
		pthread_mutex_unlock(&draw);
	}
	
	/* returns old pos if request was out of range, otherwise returns new */
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
	int i;
	struct shotprop *info = properties;
	void *retval;
	
	/* initial row at bottom of screen */
	info->row = LINES - 3;
	
	/* update the score now that a shot has been used */
	pthread_mutex_lock(&score_mutex);
	shot_update --;
	stats();
	pthread_mutex_unlock(&score_mutex);

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
		
		/* update the new position in the collision array */
		if( info->row >= 0 && info->row < LINES-1){
			collision_position[info->row][info->col].shot ++;
			
			/* 1 shot + >= 0 saucers, depending on the saucers */
			hit = collision_position[info->row][info->col].shot + 
				collision_position[info->row][info->col].saucer;/* over */
			for(i = 0; i<MAXSAUCERS; i++){
				
				/* check if any saucer thread is a hit */
				if(collision_position[info->row][info->col].there[i] != 0){
					
					/* set kill signal for hit saucers */
					saucerinfo[i].kill = 1;
				}
			}
			
			/* update the score if there is a collision */
			if(hit >1){
				
				/* draw over shot */
				mvaddch(info->row, info->col, ' ');
				
				/* remove the collision position */
				collision_position[info->row][info->col].shot --;
				
				/* print changes to the screen */
				move(LINES-1, COLS-1);
				refresh();
				pthread_mutex_unlock(&draw);
		
				/* update the score */
				pthread_mutex_lock(&score_mutex);
			
				/* hit minus 1 because 1 is from the shot */
				score_update = score_update + hit-1;
			
				/* reward a hit with more shots */
				shot_update = shot_update + hit-1;
				stats();
				pthread_mutex_unlock(&score_mutex);
				
				/* we are done with this shot */
				pthread_exit(retval);
			}
		}
		
		/* move cursor back and output changes on the screen */
		move(LINES-1, COLS-1);
		refresh();
		pthread_mutex_unlock(&draw);
		
		/* if reach the top of the screen without hitting anything */
		if(info->row < 0){
			
			/* now we are finished with the thread */
			pthread_exit(retval);
		}
	}
}

int welcome(){
	
	struct message{
		char *word;
		int length;
	};
	
	int c, i, j;
	int number = 10;
	struct message mes[number];
	
	char *words[10] = {
	"Aliens are trying to invade your homeland!!!! :O",
	"In order to stop them you must shoot down their saucers from the sky.",
	"You only have a set number of rockets so use them wisely.",
	"The game will end if you run out of rockets.",
	"The game will also end if you let too many saucers escape.",
	"Each saucer you shoot down will give you one new rocket.",
	"Information about your score is printed at the bottom of the page.",
	"Press space to shoot a rocket.",
	"Press ',' to move your launchpad right, and '.' to move it left.",
	"If you want to quit the game at any time press 'Q'."
	};
	
	for(i = 0; i < number; i++){
		mes[i].word = words[i];
		mes[i].length = strlen(words[i])+1;
	}
	
	
	mvprintw(LINES/2 - LINES/4, COLS/2 - COLS/3, "Welcome to SAUCER!");
	
	mvprintw(LINES/2 - LINES/4 +2, COLS/2 - COLS/3,
	 "Press '.' to view the rest of the instructions");
	mvprintw(LINES/2 - LINES/4 +3, COLS/2 - COLS/3,
	 "Or press space to skip the instructions and start playing");
	refresh();
	
	while(1){
		c = getch();
		if(c == '.'){
			erase();
			mvaddstr(LINES-2, 0,
			 "Press '.' to continue instructions");
			mvaddstr(LINES-1, 0,
			 "Press any other key to begin the game");
			for(i=0;i<number;i++){
				
				for(j=1; j<mes[i].length; j++){
					mvaddnstr(i+1, 0, mes[i].word, j);
					refresh();
					usleep(10000);
				}
				c = getch();
				if(c != '.'){
					erase();
					return 0;
				}
			}
			erase();
			mvprintw(LINES/2 - LINES/4 +3, COLS/2 - COLS/3,
			"(Press any key to start the game)");
			c = getch();
			erase();
			move(LINES-1,COLS-1);
			return 0;
			
		}
		else if (c == ' '){
			erase();
			move(LINES-1,COLS-1);
			return 0;
		}
	}
	
}