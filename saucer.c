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

/* ~~~~ CHANGABLE MACROS: ~~~~*/
/* the maximum number of rows with saucers on them */
#define NUMROW 2

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

/* delay for the saucers, higher number = slower saucers. 20000 recomended */
#define	SAUCERSPEED 20000

/* delay of the shots, higher number = slower shots. 60000 recomended */
#define SHOTSPEED 60000
/* ~~~~ END CHANGABLE MACROS ~~~~ */

/* the maximum number of shot threads */
#define MAXSHOTS 50

struct saucerprop{
	int row;	
	int delay;
		
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


/* lock the mutex protecting a critical region that involves printing output */
void lock_draw(){
	pthread_mutex_lock(&draw);
	move(LINES-1, COLS-1);
}

/* move cursor back and output changes on the screen */
/* unlock mutex protecting critical region */
void unlock_draw(){
	
	move(LINES-1, COLS-1);
	refresh();
	pthread_mutex_unlock(&draw);
}

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
	lock_draw();
	
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
	int shot_i = 0;
	void *retval;
	
	/* function prototypes */
	void stats();
	void *saucers();
	void *shots();
	void *replace_thread();
	int launch_site();
	void setup_saucer();
	int fire_shot();
	int rand_saucers();
	
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
			if(shot_update > 0){
				shot_i = fire_shot(shot_i, launch_position);
				
				/* fire_shot returns val<0 if no more shots */
				if(shot_i < 0){
					pthread_exit(retval);
				}
			}
		}
	}
	pthread_exit(retval);
}

/* 
 * rand_saucers adds a new saucer 
 * expects the current number of active saucers, returns the new number saucers
 */
int rand_saucers(int n){
	void *saucers();
	void setup_saucer();
	
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
 * fire shot creates a new shot
 * expects the current index of the shot_t array and the current launch position
 * if there are no shots left, returns -1, otherwise returns the new index
 */
int fire_shot(int index, int launch_position){
	
	void *retval;
	void *shots();
	
	/* loop to begining of the array */
	if(index == MAXSHOTS){
		index = 0;
	}
	
	/* set row & col for the shot (pos + 1 b/c of the space before | )*/
	shotinfo[index].col = launch_position + 1;
	
	/* create a thread for the shot */
	if(pthread_create(&shot_t[index], NULL, shots, &shotinfo[index])){
		fprintf(stderr,"error creating shot thread\n");
		endwin();
		exit(-1);
	}
	
	/* may be 1 or 0, depending on where shot thread is at */
	if(shot_update <= 1){
		
		/* no chance of hitting any other saucers */
		pthread_join(shot_t[index], &retval);
		
		/* if the last shot fired: signal to exit game */
		if(shot_update == 0){
			pthread_mutex_lock(&end_mutex);
			pthread_cond_signal(&end_condition); 
			pthread_mutex_unlock(&end_mutex);
			return -1;
		}
	}

	/* move to next shot */
	index ++;
	return index;
}

/* 
 * stats prints the # of rockets left and # of missed saucers on the screen
 * uses the draw mutex so draw must be unlocked before entering stats
 * expects no args & no return values
 */
void stats(){
		
	/* print message at bottom of the screen */
	lock_draw();
	mvprintw(LINES-1,0,
	    "score:%d, rockets remaining: %d, escaped saucers: %d/%d    ", /* over */
	    score_update, shot_update, escape_update, MAXESCAPE);
	unlock_draw();
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


void saucer_hit(int len, int col, struct saucerprop *info, char *shape){
	
	int i;
	int row = info->row;
	int index = info->index;

	lock_draw();
	
	/* draw over the saucer to remove it from the screen */
	mvaddnstr(row, col, shape, len);
	
	/* remove saucer position from the collision array */
	for(i = 0; i < len-1; i++){
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
 * expects a row, column, and index. returns nothing 
 */
void new_saucer_position(int row, int col, int index){
	
	/* add new position and remove old position */
	collision_position[row][col].saucer ++;
	
	/* provide a sign that this saucer is at that spot */
	collision_position[row][col].here[index] = 1;
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
		
		/* print the saucer on the screen at (row, col) */
		mvaddnstr(info->row, col, shape, len2);
		
		/* update collision array. len2-1 because of the extra space */
		for(i=0; i<len2-1; i++){
			
			/* sets saucer and there[i] in collision array*/
			new_saucer_position(info->row, col+1+i, info->index);
			
			/* remove old position if not first time through loop */
			if(col>0){
				collision_position[info->row][col+i].saucer --;
			}
		}
		
		/* only remove the one column that changed in collision array */
		collision_position[info->row][col].here[info->index]= 0;
		
		/*
		for(i = 0; i < COLS-1; i++){
			mvprintw(info->row+NUMROW, i, "%d", collision_position[info->row][i].saucer);
			mvprintw(info->row+2*NUMROW+1, i, "%d", collision_position[info->row][i].here[info->index]);
		}
		*/
		
		/* move cursor back and output changes on the screen */
		unlock_draw();

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
		lock_draw();
		mvaddstr(LINES-2, position, " | ");
		unlock_draw();
	}
	
	/* returns old pos if request was out of range, otherwise returns new */
	return position;
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
			
			/* draw over shot */
			mvaddch(row, col, ' ');
	
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

	/* reward a hit with more shots */
	shot_update = shot_update + hits; 
	stats();
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
	
	/* update the score now that a shot has been used */
	pthread_mutex_lock(&score_mutex);
	shot_update --;
	stats();
	pthread_mutex_unlock(&score_mutex);

	while(1){
		
		/* specify the delay in SHOTSPEED */
		usleep(SHOTSPEED);
		
		lock_draw();
		
		/* cover the old shot */
		mvaddch(info->row, info->col, ' ');
		
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
		
		/* draw the new shot at the new position one row up */
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
	int len = 10;
	struct message mes[len];
	
	/* sentences to print */
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