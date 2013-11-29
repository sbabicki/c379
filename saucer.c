/*
 * Comput 379 Assignment 3
 * Sasha Babicki
 * 1264274
 *
 * saucer.c is an animated terminal based game using curses and threads
 *
 * Threads:
 *	one thread for keyboard control
 *	one thread for each saucer
 *	one thread for each shot
 *	one thread for replacing saucers once they are finished
 * 	one thread for each time monitoring a possible last shot
 *
 * Mutex/Condition Variables:
 * 	drawing on the screen
 *	replacing a thread
 *	updating the score
 * 	updating the number of shots
 *	calling for the program to exit
 *
 * Compile:
 *	gcc saucer.c -lcurses -lpthread -o saucer
 *	
 */

#include <stdio.h>
#include <curses.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>

/* the maximum number of rows with saucers on them */
/* RESTRICTION: cannot be > LINES - 3 		   */
#define NUMROW 3

/* number of initial saucers to display at the start of the program */
#define	NUMSAUCERS 3

/* the maximum number of saucers on screen at one time 	*/
/* RESTRICTION: must be >= NUMSAUCERS 			*/
#define MAXSAUCERS 6

/* the maximum number of escaped saucers */
#define MAXESCAPE 20

/* higher number = less likely to have random saucers appear */
#define RANDSAUCERS 50

/* number of initial shots limit */
#define NUMSHOTS 15

/* delay for the saucers, higher number = slower saucers. 20000 recomended */
#define	SAUCERSPEED 20000

/* delay of the shots, higher number = slower shots. 60000 recomended 	  */
/* RESTRICTION: be sure to adjust MAXSHOTS to a higher number if changing */
/* SHOTSPEED to be very fast 						  */
#define SHOTSPEED 60000

/* the maximum number of shot threads. 50 recomended for average window size */
/* RESTRICTION: if the window size is very large be sure to increase this #! */
#define MAXSHOTS 100

struct saucerprop{
	int row;	
	int delay;
	int colour;
		
	/* element # in thread array */
	int index;
	
	/* 0 default, set to 1 for kill */	
	int kill;
};

struct shotprop{
	int col;	
	int row;
};

struct screen{
	int shot;
	int saucer;
	int here[MAXSAUCERS];
};

/* for storing the properties of saucers and shots */
struct saucerprop saucerinfo[MAXSAUCERS];
struct shotprop shotinfo[MAXSHOTS];

/* collision detection array */
struct screen **collision_position;

/* score update variables */		
int escape_update = 0;
int shot_update = NUMSHOTS;
int score_update = 0;

/* holds the element number of the thread that can be replaced */
int replace_index;

/* saves the most up to date shot index */
int save;

/* screen colour variables */
int use_colour;
int next_colour;

/* condition variables */
pthread_cond_t replace_condition = PTHREAD_COND_INITIALIZER;
pthread_cond_t end_condition = PTHREAD_COND_INITIALIZER;

/* mutexes */
pthread_mutex_t draw = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t score_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t shot_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t replace_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t end_mutex = PTHREAD_MUTEX_INITIALIZER;

/* arrays to store the threads */
pthread_t saucer_t[MAXSAUCERS];
pthread_t shot_t[MAXSHOTS];
pthread_t end_t;

/* function prototypes */
void lock_draw();
void unlock_draw();
void setup_saucer();
void stats();
int launch_site();
void saucer_hit();
void new_saucer_position();
void *saucers();
int rand_saucers();
void *replace_thread();
void find_hit();
void *shots();
void *find_end();
int fire_shot();
void *process_input();
int welcome(); 


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

	/* no arguments should be included for this program */
	if (ac != 1){
		printf("usage: saucer\n");
		exit(1);
	}
	
	/* make sure the system allows enough processes to play the game */
	getrlimit(RLIMIT_NPROC, &rlim);
	if (rlim.rlim_cur < MAXSAUCERS + MAXSHOTS + 4){
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
	
	/* if we can't use colour set use_colour global variable to false */
	if(has_colors() == FALSE){	
		use_colour = 0;
	}
	
	/* if we can use colour set use_colour to true and start colour */
	else{
		use_colour = 1;
		start_color();
		
		/* initialize colour codes */
		init_pair(1, COLOR_RED, COLOR_BLACK);
		init_pair(2, COLOR_GREEN, COLOR_BLACK);
		init_pair(3, COLOR_BLUE, COLOR_BLACK);
		init_pair(4, COLOR_CYAN, COLOR_BLACK);
		init_pair(5, COLOR_MAGENTA, COLOR_BLACK);
		init_pair(6, COLOR_YELLOW, COLOR_BLACK);
	}
	
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
		fprintf(stderr,"error creating input processing thread\n");
		endwin();
		exit(-1);
	}
	
	/* wait for 'Q', too many escaped saucers, or run out of rockets */
	/* score mutex is locked by the calling thread to ensure no updates */
	pthread_mutex_lock(&end_mutex);
	pthread_cond_wait(&end_condition, &end_mutex);
	
	/* cancel all threads so they no longer update */
	for(i=0; i<MAXSAUCERS; i++){
		pthread_cancel(saucer_t[i]);
	}
	for(i=0; i<MAXSHOTS; i++){
		pthread_cancel(shot_t[i]);
	}
	pthread_cancel(replace_t);
	pthread_cancel(input_t);
	pthread_cancel(end_t);
	
	/* erase everything on the screen in prep for closing message */
	erase();
	
	/* padding for the sides of the messages */
	r_padding = LINES/2 - LINES/4;
	c_padding = COLS/2 - COLS/3;
	
	/* if the game ends by too many saucers escaping */
	if(escape_update == MAXESCAPE){
		
		/* print too many escaped saucers closing message */
		mvprintw(r_padding, c_padding, "TOO MANY SAUCERS ESCAPED :(");
	}
	
	/* if the game ends by running out of shots */
	else if(shot_update == 0){
		
		/* print ran out of rockets closing message */
		mvprintw(r_padding, c_padding, "YOU RAN OUT OF ROCKETS :(");
	}
	
	/* closing message */
	mvprintw(r_padding +1, c_padding, "Escaped saucers: %d", escape_update);
	mvprintw(r_padding +2, c_padding, "Rockets left: %d", shot_update);
	mvprintw(r_padding +3, c_padding, "Final score: %d", score_update);
	mvprintw(r_padding +4, c_padding, "Thanks for playing!");
	mvprintw(r_padding +5, c_padding, "(Press 'Q' to exit)");
	refresh();
	
	/* free allocated memory */
	free(array[0]);
	free(array);
	
	/* allow the user to exit the program */
	while(1){
		c = getch();
		if(c == 'Q'){
			erase();
			refresh();
			break;
		}
	}

	/* close curses */
	endwin();
	return 0;
}


/* 
 * lock_draw locks the draw mutex protecting a critical region that involves 
 * printing output and moves curser to corner of the screen
 */
void lock_draw(){
	pthread_mutex_lock(&draw);
	move(LINES-1, COLS-1);
}


/* 
 * unlock_draw moves cursor back and output changes on the screen
 * and unlocks draw mutex protecting critical region 
 */
void unlock_draw(){
	
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
	saucerinfo[i].colour = next_colour;
	
	/* loop colours */
	if(next_colour == 6){
		next_colour = 0;
	}
	else{
		next_colour ++;
	}	
}


/* 
 * stats prints the # of rockets left and # of missed saucers on the screen
 * uses the draw mutex so draw must be unlocked before entering stats
 * score_mutex should be locked before entering
 * expects no args & no return values
 */
void stats(){

	/* print message at bottom of the screen */
	lock_draw();
	mvprintw(LINES-1, 0, 
	    " score: %d, rockets remaining: %d, escaped saucers: %d/%d        ", 
	    score_update, shot_update, escape_update, MAXESCAPE);

	unlock_draw();
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
		lock_draw();
		mvaddstr(LINES-2, position, " | ");
		unlock_draw();
	}
	
	/* returns old pos if request was out of range, otherwise returns new */
	return position;
}


/*
 * saucer hit updates the collision array after a saucer has been hit
 * and replaces the saucer thread with a new one
 * expects the length to remove, column, saucerinfo, and a string to draw 
 * no return value
 */
void saucer_hit(int len, int col, struct saucerprop *info, char *shape){
	
	int i;
	int row = info->row;
	int index = info->index;

	lock_draw();
	
	/* draw over the saucer to remove it from the screen */
	mvaddnstr(row, col, shape, len);
	
	/* remove saucer position from the collision array */
	for(i = 0; i < len; i++){
		collision_position[row][col+i].saucer --;
		collision_position[row][col+i].here[index] = 0;
		info->kill = 0;
	}
	
	/* signal to replace the thread at that index */
	pthread_mutex_lock(&replace_mutex);
	
	/* set global to index that can be replaced */
	replace_index = index;
	pthread_cond_signal(&replace_condition);
	pthread_mutex_unlock(&replace_mutex);
	
	unlock_draw();
}


/* 
 * new_saucer_position updates adds information in the collision array
 * draw mutex should be locked before entering this function
 * expects a row, column, and index. returns nothing 
 */
void new_saucer_position(int row, int col, int index){
	
	/* add new position position */
	collision_position[row][col].saucer ++;
	
	/* provide a sign that this saucer is at that spot */
	collision_position[row][col].here[index] = 1;
}


/* 
 * saucers is the function used by all the saucer threads
 * is type void* and recieves argument void* b/c requirements of pthread_create
 * expects the start the address of the corresponding saucer properties
 * no return value
 */
void *saucers(void *properties){	
	
	int i;
	void *retval;
	char *shape = " <--->";
	char *shape2= "      ";
	char *shape3 = "<--->";
	int len = strlen(shape);
	int len2 = len;
	int col = 0;
	
	
	/* points to properties info for a specific saucer */
	struct saucerprop *info = properties;
	
	/* update the saucers */
	while(1){
		
		/* remove the saucer if kill is set for that saucer */
		if(info->kill == 1){
		
			/* remove saucer info */
			saucer_hit(len2, col, info, shape2);
		
			/* finish with the thread */
			pthread_exit(retval);
		}
		
		/* thread sleeps for (its delay time * defined timeunits) */
		usleep(info->delay*SAUCERSPEED);
		
		/* lock the draw mutex CRITICAL REGION BELOW */
		lock_draw();
		
		/* set colour only if the global use_colour is set to 1 */
		if(use_colour){
			
			/* change colour of terminal */
			attron(COLOR_PAIR(info->colour));
		}
		
		/* if not overlapping */
		if(collision_position[info->row][col].saucer <= 1){
			
			/* print the saucer on the screen at (row, col) */
			mvaddnstr(info->row, col, shape, len2);
		}
		
		/* if overlapping with another saucer */
		else{
			/* print saucer without the padding at the end */
			mvaddnstr(info->row, col+1, shape3, len2-1);
		}
		
		/* unset colour only if the global use_colour is set to 1 */
		if(use_colour){
			
			/* change colour back to default */
			attroff(COLOR_PAIR(info->colour));
		}
		
		/* update collision array. col+1 because of the extra space */
		for(i=0; i<len2; i++){
			
			/* sets saucer and there[i] in collision array*/
			new_saucer_position(info->row, col+1+i, info->index);
			
			/* remove old position if not first time through loop */
			if(col>0){
				collision_position[info->row][col+i].saucer --;
			}
		}
		
		/* only remove the one column that changed in collision array */
		collision_position[info->row][col].here[info->index]= 0;
		
		/* for testing only
		for(i = 0; i < COLS-1; i++){
			mvprintw(info->row+NUMROW, i, "%d", 
		collision_position[info->row][i].saucer);
			//mvprintw(info->row+2*NUMROW+1, i, "%d", 
		//collision_position[info->row][i].here[info->index]);
		}
		*/
		
		/* move cursor back and output changes on the screen */
		unlock_draw();

		/* move to next column */
		col ++;
		
		/* when we reach the end of the screen start to stop writing */
		if (col+len >= COLS){
			
			/* @ end - write progressively less of the string */
			len2 --;
			
			/* now the string is off the page, exit the thread */
			if(len2 == 0){

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
 * rand_saucers adds a new saucer 
 * expects the current number of active saucers, returns the new number saucers
 */
int rand_saucers(int n){
	
	/* populate saucerinfo */
	setup_saucer(n);
	
	/* create a saucer thread */
	if (pthread_create(&saucer_t[n], NULL, saucers, &saucerinfo[n])){
		fprintf(stderr,"error creating saucer thread\n");
		endwin();
		exit(-1);
	}
		
	/* new number of saucers */
	n ++;
	return n;
}


/*
 * replace_thread is run constantly by one thread
 * it replaces a saucer thread that has finished running with a new saucer 
 * allows many threads to be created but only a fixed number of active threads
 * and a set amount of threads to be stored in an array
 * expects no args & no return values
 */
void *replace_thread(){
	
	void *retval;
	int i;
	
	while(1){

		/* wait until given the signal to replace a thread */
		pthread_mutex_lock(&replace_mutex);
		pthread_cond_wait(&replace_condition, &replace_mutex);

		/* wait until thread terminates for sure before replacing it */
		pthread_join(saucer_t[replace_index], &retval);
		
		i = replace_index;
	
		/* optional delay */
	 	/* sleep(2); */
	
		/* populate new saucer + create new thread reusing old index */
		setup_saucer(replace_index);
		if(pthread_create(&saucer_t[i], NULL, saucers, &saucerinfo[i])){
			fprintf(stderr,"error replacing saucer thread\n");
			endwin();
			exit(-1);
		}
		pthread_mutex_unlock(&replace_mutex);
	}
}


/*
 * find hit locates hit saucers, draws over a shot at a given position,
 * and adds 1 point to the score+shots
 * NOTE: must have draw mutex locked before entering function 
 * expects row and col as args, returns nothing
 */
void find_hit(int row, int col){
	int i; 
	int hits = 0;
	
	for(i = 0; i<MAXSAUCERS; i++){
	
		/* check if any saucer thread is a hit */
		if(collision_position[row][col].here[i] != 0){
	
			/* set kill for hit saucers */
			saucerinfo[i].kill = 1;
			
			/* keep track of how many saucers were hit */
			hits++;	
		}
	}
	unlock_draw();
	
	/* update the score */
	pthread_mutex_lock(&score_mutex);

	/* add one point to the score */
	score_update = score_update + hits; 
	pthread_mutex_lock(&shot_mutex);
	/* reward a hit with more shots */
	shot_update = shot_update + hits; 
	stats();
	pthread_mutex_unlock(&shot_mutex);
	pthread_mutex_unlock(&score_mutex);
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
	
	
	while(1){
		
		/* specify the delay in SHOTSPEED */
		usleep(SHOTSPEED);
		
		lock_draw();
		
		/* cover the old shot if no saucer has moved there */
		if(collision_position[info->row][info->col].saucer == 0){
			mvaddch(info->row, info->col, ' ');
		}
		
		/* remove the old position from the collision array */
		if( info->row >= 0 && info->row < LINES-1){
			collision_position[info->row][info->col].shot --;	
		}
		
		/* the new position one row up */
		info->row --;
		
		/* update the new position in the collision array */
		if( info->row >= 0 && info->row < LINES-1){
			collision_position[info->row][info->col].shot ++;
			
			/* hit = # of saucers at that position */
			hit = collision_position[info->row][info->col].saucer;
			if(hit > 0){
				
				/* find hits and update score, release draw */
				find_hit(info->row, info->col);
				
				/* now we are done with this shot */
				pthread_exit(retval);
			}
		}
		
		/* if no hit draw the new shot at the new position one row up */
		mvaddch(info->row, info->col, '^');
		
		/* move cursor back and output changes on the screen */
		unlock_draw();
		
		/* if reach the top of the screen without hitting anything */
		if(info->row < 0){
			
			/* now we are finished with the thread */
			pthread_exit(retval);
		}
	}
}


/*
 * find_end waits for a shot thread to end and checks if the program can exit
 * if the shot finishes without hitting anything the program can exit
 * otherwise more shots are available and the game can continue
 * expects the address of the shot of interest thread id, no return values
 */
void *find_end(){//void *index){
	
	void *retval;
	int track;
	
	/* keep track of the index we are currently looking at */
	track = save;
	
	/* when there is no chance of hitting any other saucers */
	pthread_join(shot_t[track], &retval);
	
	/* must have 0 shots left and must be last shot ever (no updates!) */
	pthread_mutex_lock(&shot_mutex);
	if(shot_update == 0 && track == save){
		
		/* if the last shot fired and misses: signal to exit game */
		pthread_mutex_lock(&end_mutex);
		pthread_cond_signal(&end_condition); 
		pthread_mutex_unlock(&end_mutex);
		pthread_exit(&retval);
	}
	
	/* if the last shot does hit something go back to allowing new shots */
	pthread_mutex_unlock(&shot_mutex);
	pthread_exit(&retval);
}


/* 
 * fire shot creates a new shot
 * expects the current index of the shot_t array and the current launch position
 * if there are no shots left, returns -1, otherwise returns the new index
 */
int fire_shot(int i, int launch_position){
	
	void *retval;
	int end_i;
	
	/* store index of the current last shot fired in end_i */
	if(i != 0){
		end_i = i - 1;
	}
	
	/* if the current index is zero that means the last index looped */
	else{
		end_i = MAXSHOTS - 1;
	}
	
	/* if we still have shots left create fire a new shot */
	pthread_mutex_lock(&shot_mutex);
	if(shot_update > 0){
		pthread_mutex_unlock(&shot_mutex);
	
		/* loop to begining of the array to reuse indices */
		if(i == MAXSHOTS){
			i = 0;
		}
	
		/* set row & col for the shot (pos+1 b/c of the space before|)*/
		shotinfo[i].col = launch_position + 1;
	
		/* create a thread for the shot */
		if(pthread_create(&shot_t[i], NULL, shots, &shotinfo[i])){
			fprintf(stderr,"error creating shot thread\n");
			endwin();
			exit(-1);
		}
		
		/* update and print the score now that a shot has been used */
		pthread_mutex_lock(&score_mutex);
		pthread_mutex_lock(&shot_mutex);
		shot_update --;
		stats();
		pthread_mutex_unlock(&shot_mutex);
		pthread_mutex_unlock(&score_mutex);
		
		/* update the current next shot now that there is a new shot */
		end_i = i;
	}
	
	/* update save with the current index */
	save = end_i;
	pthread_mutex_unlock(&shot_mutex);
	
	/* if the shot thread is at zero, see if it remains at zero */
	pthread_mutex_lock(&shot_mutex);
	if(shot_update == 0){
		pthread_mutex_unlock(&shot_mutex);
		
		/* create a thread to wait for the last shot to finish */
		if(pthread_create(&end_t, NULL, find_end, NULL)){
			fprintf(stderr,"error creating shot thread\n");
			endwin();
			exit(-1);
		}
	}
	pthread_mutex_unlock(&shot_mutex);

	/* move to next shot for next call */
	end_i ++;
	
	/* return the next available index for the next shot */
	return end_i;
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
	int shot_i = 0;
	void *retval;
	
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
			nsaucers = rand_saucers(nsaucers);
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
		
		/* pausing the game: an intentional use of deadlock!! */
		else if(c == 'p'){
			
			/* stop anything from being drawn on the screen */
			lock_draw();
			
			/* print message letting user know game is paused */
			mvprintw(10, 10, "PAUSED");
			mvprintw(11, 10, "(press 'p' to resume)");
			refresh();
			
			/* wait for user to press 'p' to resume game */
			while(1){
				c = getch();
				if (c == 'p'){
					
					/* cover pause message and return */
					mvprintw(10,10,"      ");
					mvprintw(11,10,"                     ");
					refresh();
					break;
				}
			}
			
			/* stop deadlock */
			unlock_draw();
		}
		/* toggle turning colour on or off */
		else if(c == 'c'){
			
			/* set use_colour to the opposite of what it is now */
			lock_draw();
			if (use_colour){
				use_colour = 0;
			}
			else{
				use_colour = 1;
			}
			unlock_draw();
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
			
			/* if we are not out of shots yet fire the next one */
			pthread_mutex_lock(&shot_mutex);
			if(shot_update > 0){
				pthread_mutex_unlock(&shot_mutex);
				
				/* fire_shot returns next shot index */
				shot_i = fire_shot(shot_i, launch_position);
			}
			pthread_mutex_unlock(&shot_mutex);
		}
	}
	pthread_exit(retval);
}


/* 
 * print the introduction message
 * expects no arguments, returns zero when complete
 */
int welcome(){
	
	struct message{
		char *word;
		int length;
	};
	
	int c, i, j;
	int row = LINES/2 - LINES/4;
	int col = COLS/2 - COLS/3;
	
	/* number of words in words array */
	int len = 12;
	struct message mes[len];
	
	/* sentences to print */
	char *words[12] = {
	"Aliens are trying to invade your homeland!!!! :O",
	"In order to stop them you must shoot down their saucers from the sky.",
	"You only have a set number of rockets so use them wisely.",
	"The game will end if you run out of rockets.",
	"The game will also end if you let too many saucers escape.",
	"Each saucer you shoot down will give you one new rocket.",
	"Information about your score is printed at the bottom of the page.",
	"Press space to shoot a rocket.",
	"Press ',' to move your launchpad right, and '.' to move it left.",
	"Press 'c' to toggle colours on or off.",
	"Press 'p' to pause or resume the game.",
	"If you want to quit the game at any time press 'Q'."
	};
	
	/* populate the message array */
	for(i = 0; i < len; i++){
		mes[i].word = words[i];
		mes[i].length = strlen(words[i])+1;
	}
	
	/* print intro on screen */
	mvprintw(row, col, "Welcome to SAUCER!");
	mvprintw(row+2, col, "Press '.' to view the rest of the instructions");
	mvprintw(row+3, col,"Press space to skip instructions & start playing");
	refresh();
	
	/* print instructions */
	while(1){
		c = getch();
		if(c == '.'){
			erase();
			mvaddstr(LINES-2, 0, "Press '.' to continue reading");
			mvaddstr(LINES-1,0,"Press any other key to begin game");
			
			/* print each line character by character */
			for(i=0;i<len;i++){
				for(j=1; j<mes[i].length; j++){
					mvaddnstr(i+1, 0, mes[i].word, j);
					refresh();
					
					/* pause to get the effect of typing */
					usleep(10000);
				}
				
				/* exit the welcome function by user request */
				c = getch();
				if(c != '.'){
					erase();
					move(LINES-1,COLS-1);
					return 0;
				}
			}
			
			/* last message */
			erase();
			mvprintw(row+3,col,"(Press any key to start the game)");
			
			/* read in any input to signal exit function */
			c = getch();
			erase();
			move(LINES-1,COLS-1);
			return 0;
			
		}
		
		/* if user chooses to skip instructions */
		else if (c == ' '){
			erase();
			move(LINES-1,COLS-1);
			return 0;
		}
	}
}