#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wait.h>
#include <stdbool.h>
#include <fcntl.h>

#define MAX_LINE 128 /* 80 chars per line, per command, should be enough. */
#define HISTORY_COUNT 10 /*10 history command will be displayed when user enter history command*/

#define CREATE_FLAGS (O_WRONLY | O_TRUNC | O_CREAT )
#define CREATE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define CREATE_APPENDFLAGS (O_WRONLY | O_APPEND | O_CREAT )

int command(char* args[]);
void execute(char* args[] ,int background, bool doFork, pid_t child_pid);
void clearBuffer(char buffer[]);
void execute_path(char* args[]);
int popBackgroundProcess();
void addBackgroundProcess(int data, char* args[]);
void printQueue();
void generateCommandLine(char *args[], char* command);
void generateHistoryLine(char *args[], char* histBuffer);
void controlZ();
void path(char *args[]);
void addPath(char* path_name);
void removePath(char* path_name);
void removeProcess(int data);
void fg(int pid);
void setupHist_Fg(char *histBuffer, char *histArgs[], int *background, int length,int controlVariableForFG);
void shiftHistArray();
void runningHistory(char * histBuffer, int length);
void copyBufferToHist(char buffer[128], int length);
int findCommandLength(char * command);
int exitt();
int ioRedirection(char *args[]);

typedef struct background_process_queue{
    pid_t p_id;
    char command[MAX_LINE];
    int id;
    struct background_process_queue *next;
} background_process;

background_process *head = NULL;
background_process *curr = NULL;
background_process *previous = NULL;

typedef struct pathname_list{
    char pathname[MAX_LINE];
    struct pathnamelist *next;
}pathname_list;


pathname_list *pathname_head = NULL;
pathname_list *current_path = NULL;

// Variables that hold info about background process
bool ANY_FOREGROUND_PROCESS = false;
int CURRENT_FOREGROUND_PROCESS;
int backgroundProcessNumber = 0;
int child_num;
int childs[80];
char histBuffer[MAX_LINE];
int histBufferLengths[MAX_LINE];
int maxValueOfHistLength=0;
int controlVariableForFG = 0;
int pathgeneratedControl = 0;
int numberofPath = 0;
int arg_count = 0;
char *hist[HISTORY_COUNT][MAX_LINE]; /* command history array */
int numberofHistory = 0;
bool controlOfHistoryNum = false;

int TRUNC_OUT = 0;  // For truncation
int APPEND_OUT = 0;   // For append
int ERR_OUT = 0;     // For printing err
int IN_MODE = 0;    //For input

/* The setup function below will not return any value, but it will just: read
in the next command line; separate it into distinct arguments (using blanks as
delimiters), and set the args array entries to point to the beginning of what
will become null-terminated, C-style strings. */

void setup(char inputBuffer[], char *args[],int *background)
{
    *background = 0;
    int length, /* # of characters in the command line */
            i,      /* loop index for accessing inputBuffer array */
            start,  /* index where beginning of next command parameter is */
            ct;     /* index of where to place the next parameter into args[] */

    ct = 0;

    /* read what the user enters on the command line */

    length = read(STDIN_FILENO,inputBuffer,MAX_LINE);

    /* 0 is the system predefined file descriptor for stdin (standard input),
       which is the user's screen in this case. inputBuffer by itself is the
       same as &inputBuffer[0], i.e. the starting address of where to store
       the command that is read, and length holds the number of characters
       read in. inputBuffer is not a null terminated C-string. */

    start = -1;
    if (length == 0)
        exit(0);            /* ^d was entered, end of user command stream */

/* the signal interrupted the read system call */
/* if the process is in the read() system call, read returns -1
  However, if this occurs, errno is set to EINTR. We can check this  value
  and disregard the -1 value */
    if ( (length < 0) && (errno != EINTR) ) {
        perror("error reading the command");
        exit(-1);           /* terminate with error code of -1 */
    }

//    printf(">>%s<<",inputBuffer);
    for (i=0;i<length;i++){ /* examine every character in the inputBuffer */
        switch (inputBuffer[i]){
            case ' ':
            case '\t' :               /* argument separators */
                if(start != -1){
                    args[ct] = &inputBuffer[start];    /* set up pointer */
                    ct++;
                }
                inputBuffer[i] = '\0'; /* add a null char; make a C string */
                start = -1;
                break;

            case '\n':                 /* should be the final char examined */
                if (start != -1){
                    args[ct] = &inputBuffer[start];
                    ct++;
                }
                inputBuffer[i] = '\0';
                args[ct] = NULL; /* no more arguments to this command */
                break;

            default :             /* some other character */
                if (start == -1)
                    start = i;
                if (inputBuffer[i] == '&'){
                    *background  = 1;
                    inputBuffer[i-1] = '\0';
                }
        } /* end of switch */
    }    /* end of for */
    args[ct] = NULL; /* just in case the input line was > 80 */

/*    for (i = 0; i < ct; i++)
        printf("%s ",args[i]);*/

//    printf("\n");
    /*for (i = 0; i < ct; i++)
        printf("args %d = %s\n",i,args[i]);*/
} /* end of setup routine */

int main(void)
{
    char inputBuffer[MAX_LINE]; /*buffer to hold command entered */
    int background; /* equals 1 if a command is followed by '&' */
    char *args[MAX_LINE/2 + 1]; /*command line arguments */

    int i = 0;

    for (i = 0; i < HISTORY_COUNT; i++)
        histBuffer[i] = '\0';

    /* Initialize signal */
    struct sigaction act;
    act.sa_handler = controlZ;
    act.sa_flags = SA_RESTART;

    // Set up signal handler for ^Z signal
    if ((sigemptyset(&act.sa_mask) == -1) || (sigaction(SIGTSTP, &act, NULL) == -1)) {
        fprintf(stderr, "Failed to set SIGTSTP handler\n");
        return 1;
    }

    while (1){
        printf("\nmyshell: ");
        fflush(stdout);

        setup(inputBuffer, args, &background);
        if(args[0] == NULL)
            continue;

        if(numberofHistory ==10)
            controlOfHistoryNum = true;
        if(numberofHistory < 10 && strcmp(args[0], "history")){
            numberofHistory++;
        }

        if(strcmp(args[0], "history") ) {

            if(controlOfHistoryNum ) //when hist num will be greater than 10, hist array will be shifted one by one
                shiftHistArray();

            generateHistoryLine(args, histBuffer);

            if(maxValueOfHistLength< histBufferLengths[numberofHistory-1])
                maxValueOfHistLength = histBufferLengths[numberofHistory-1];

            copyBufferToHist(histBuffer,histBufferLengths[numberofHistory-1]);

            for (i = 0; i < histBufferLengths[numberofHistory-1] ; i++)
                histBuffer[i] = '\0';

        }

        /*setup() calls exit() when Control-D is entered */
        int whichCommand= command(args);
        int index;
        switch (whichCommand){
            case 0:
                index= numberofHistory;
                while(true){  //printing history
                    printf("%4d  ", numberofHistory-index+1);

                    for (int i = 0; i < histBufferLengths[index-1] ; i++) {
                        if(hist[index-1][i] == NULL)
                            break;
                        printf("%c", hist[index-1][i]);
                    }

                    printf("\n");
                    index--;
                    if(index <= 0)
                        break;
                }
                break;
            case 1: controlZ();
                break;
            case 2: path(args);
                break;
            case 3: fg(atoi(args[1]));
                break;
            case 4: exitt();
                break;
            case 5: ioRedirection(args);
                break;
            case 6: execute(args , background,1,0); //Run execute if user give linux command
                break;
            case 7:
                index = atoi(args[2]);
                char histBuffer[maxValueOfHistLength];
                for (int k = 0; k < maxValueOfHistLength; ++k)
                    histBuffer[k]= '\0';

                for (int j = 0; j < maxValueOfHistLength ; ++j) {
                    histBuffer[j] = hist[numberofHistory-index][j];
                }

                runningHistory(histBuffer, histBufferLengths[numberofHistory-index]+1);
                break;
        }
    }
}

void controlZ() {
    int status;
    // If there is a foreground process
    if(ANY_FOREGROUND_PROCESS) {
        // Check if this process still running
        kill(CURRENT_FOREGROUND_PROCESS, 0);
        // If not running, errno will set to ESRCH
        if(errno == ESRCH) {
            // Inform user
            fprintf(stderr, "\nprocess %d not found\n", CURRENT_FOREGROUND_PROCESS);
            // Set any foreground process to false
            ANY_FOREGROUND_PROCESS = false;
            printf("myshell: ");
            fflush(stdout);
        }
            // If foreground process is still running
        else {
            // Send a kill signal to it
            kill(CURRENT_FOREGROUND_PROCESS, SIGKILL);
            // Then wait for its group to terminate with option WNOHANG
            waitpid(-CURRENT_FOREGROUND_PROCESS, &status, WNOHANG);
            printf("\n");
            // If code reaches here, there is no foreground process remaining
            ANY_FOREGROUND_PROCESS = false;
        }
    }
        // If there is no background process, ignore the signal
    else {
        printf("\nmyshell: ");
        fflush(stdout);
    }
}

void shiftHistArray() {

    for (int j = 0; j < HISTORY_COUNT-1; j++)  {
        for (int k = 0; k < MAX_LINE; k++) {
            hist[j][k]= hist[j+1][k];
        }
    }

    for (int l = 0; l <MAX_LINE ; l++) {
        hist[9][l]= '\0';
    }

    for (int m = 0; m < HISTORY_COUNT-1; ++m) {
        histBufferLengths[m]= histBufferLengths[m+1];
    }
}

void copyBufferToHist(char buffer[128], int length) {

    for (int k = numberofHistory-1; k<numberofHistory ; k++) { //histBuffer is copied to hist array
        for (int j = 0; j < length; j++) {
            if(buffer[j]== '\0' )
                break;
            hist[k][j]= buffer[j];
        }
    }
}

void runningHistory(char * histBuffer , int length) {

    int background = 0; /* equals 1 if a command is followed by '&' */
    char *histArgs[MAX_LINE/2 + 1]; /*command line arguments */

    numberofHistory++;
    if(controlOfHistoryNum )  //when hist num will be greater than 10, hist array will be shifted one by one
        shiftHistArray();

    copyBufferToHist(histBuffer, findCommandLength(histBuffer)+1);
    histBufferLengths[numberofHistory-1]= length;
    setupHist_Fg(histBuffer, histArgs, &background, length, controlVariableForFG);
    execute(histArgs ,background ,1,0);
}

void setupHist_Fg(char *histBuffer, char *histArgs[], int *background, int length,int controlVariableForFG) {

    int  i,      /* loop index forexecute accessing inputBuffer array */
            start,  /* index where beginning of next command parameter is */
            ct;     /* index of where to place the next parameter into args[] */

    ct = 0;

    start = -1;
    if (length == 0)
        exit(0);            /* ^d was entered, end of user command stream */

    if ( (length < 0) && (errno != EINTR) ) {
        fprintf(stderr, "Error reading the history command\n");
        exit(-1);           /* terminate with error code of -1 */
    }

    for (i=0;i<length;i++){ /* examine every character in the inputBuffer */
        switch (histBuffer[i]){
            case ' ':
            case '\t' :               /* argument separators */
                if(start != -1){
                    histArgs[ct] = &histBuffer[start];    /* set up pointer */
                    ct++;
                }
                histBuffer[i] = '\0'; /* add a null char; make a C string */
                start = -1;
                break;

            case '\n':                 /* should be the final char examined */
                if (start != -1){
                    histArgs[ct] = &histBuffer[start];
                    ct++;
                }
                histBuffer[i] = '\0';
                histArgs[ct] = NULL; /* no more arguments to this command */
                break;
            default :             /* some other character */
                if (start == -1)
                    start = i;
                if (histBuffer[i] == '&'){
                    *background  = 1;
                    histBuffer[i-1] = '\0';
                    histArgs[ct] = &histBuffer[start];
                    ct++;
                    histArgs[ct] = NULL;
                }
        } /* end of switch */
    }    /* end of for */
    histArgs[ct] = NULL; /* just in case the input line was > 80 */
}

int command(char* args[]){
    if (!strcmp(args[0], "history")){
        if(args[1] && !strcmp(args[1], "-i"))
            return 7;
        return 0;
    }
    else if (!strcmp(args[0], "^Z"))
        return 1;
    else if (!strcmp(args[0], "path"))
        return  2;
    else if (!strcmp(args[0], "fg"))
        return  3;
    else if (!strcmp(args[0], "exit"))
        return 4;
    else{
        int k = 0;
        arg_count = 0;
        while(args[k] != NULL) {
            arg_count++;
            k++;
        }
        if(arg_count > 1){
            if(!strcmp(args[1], ">") || !strcmp(args[1], ">>") || !strcmp(args[1], "<") || !strcmp(args[1], "2>"))
                return 5;
            if(arg_count > 2){
                if(!strcmp(args[2], ">") || !strcmp(args[2], ">>") || !strcmp(args[2], "<") || !strcmp(args[2], "2>"))
                    return 5;
            }
        }
    }
    return 6;
}

void execute(char* args[] ,int background, bool doFork, pid_t child_pid){

    if(doFork)
        child_pid = fork();

    // Fork error
    if(child_pid == -1) {
        fprintf(stderr, "Failed to fork\n");
        return;
    }

    else if(child_pid > 0){  // Parent process
        if(background == 0) {  //Foreground process
            //Update foreground process variables
            ANY_FOREGROUND_PROCESS = true;
            CURRENT_FOREGROUND_PROCESS = child_pid;
            wait(&child_pid);//wait for child process to end
            //No foreground process left
            ANY_FOREGROUND_PROCESS = false;
        }
        else {  //Background process
            printf("Process is a background process.\n");
            addBackgroundProcess(child_pid, args);
            printQueue(); //Print current background processes
        }
        background = 0;

    }
    else if (child_pid == 0){  //CHILD PROCESS
        //If the process created successfully
        if(background == 0)
            printf("Process is a foreground process.");

        execute_path(args);
    }

}

void execute_path(char* args[]){
    int path_exist = 1; //check variable for command exist or not
    int numberofPath = 1;
    //Get the ENV PATH's
    char *path_array = getenv("PATH");

    //get path number
    int i =0;
    while(path_array[i]!= '\0'){

        if( path_array[i] == ':')
            numberofPath++;
        i++;
    }

    //Separate each path from ":"
    char * path_token = strtok(path_array, ":");
    char buffer[MAX_LINE]; // array for add / and args[0] to the path token

    // loop through the string to extract all other tokens
    i = 0;
    while( path_token != NULL ) {
        strcat(buffer, path_token);
        strcat(buffer, "/");
        strcat(buffer, args[0]);

        if(execv(buffer, args) == -1){
            path_exist = 0 ;
        }

        clearBuffer(buffer);

        if(path_exist == 0 && numberofPath == i+1)
            fprintf(stderr, "\nInvalid command! Please try again.Press enter to continue. \n");

        i++;
        path_token = strtok(NULL, ":");
    }
}

void clearBuffer(char buffer[]){
    int i;
    for(i = 0; i < MAX_LINE; i++){
        buffer[i] = '\0';
    }
}

void printQueue() {
    background_process *temp = head;

    if(temp == NULL) {
        fprintf(stderr, "No background processes found\n");
        return;
    }
    else{

        printf("\n");
        printf("CURRENT BACKGROUND PROCESSES:\n");
        int i = 0;
        while(i < backgroundProcessNumber){
            printf("[%d]  %d\t\t%s\n", i, temp->p_id, temp->command);
            temp=temp->next;
            i++;
        }

    }



}

void addBackgroundProcess(int data, char* args[]){


    if(head == NULL) {
        head = (background_process *) malloc(sizeof(background_process));
        head->p_id = data;
        generateCommandLine(args, head->command);
        head->id = backgroundProcessNumber;
        head->next = (background_process *) malloc(sizeof(background_process));
        previous = curr;
        curr= head->next;
    }
    else{
        previous = curr;
        curr->p_id = data;
        generateCommandLine(args, curr->command);
        curr->id=backgroundProcessNumber;
        curr->next = (background_process *) malloc(sizeof(background_process));
        curr=curr->next;

    }
    backgroundProcessNumber++;

}

int popBackgroundProcess(){

    background_process* temp = head;
    int pid;

    while(1){

        if(temp->next->p_id == previous->p_id){
            temp->next = NULL;
            pid= previous->p_id;
            free(previous);
            break;
        }
        temp= temp->next;
    }
    backgroundProcessNumber--;

    return pid;
}

void generateCommandLine(char *args[], char* command){

    //generate command line from args array, generate one dimensional array
    for (int i = 0; ; ++i) {
        if(args[i] ){
            strcat(command, args[i]);

            if (!strcmp(args[i],"&")){
                break;
            }
            else{
                strcat(command, " ");
            }
        }
        else
            break;

    }
}

void generateHistoryLine(char *args[], char* histBuffer) {

    //generate history line from args array, generate one dimensional array
    for (int i = 0; ; ++i) {
        if(args[i] ){
            if (!strcmp(args[i],"&")){
                strcat(histBuffer, args[i]);
                break;
            }
            else{
                strcat(histBuffer, args[i]);
                strcat(histBuffer, " ");
            }
        }
        else
            break;
    }


    int length = findCommandLength(histBuffer);
    histBufferLengths[numberofHistory-1] = length;

}

void path(char* args[]){
    //Get the ENV PATH'sargs
    char *path_array = getenv("PATH");
    //Separate each path from ":"
    char * path_token = strtok(path_array, ":");

    if(pathgeneratedControl == 0){
        // add all paths to the pathname list
        while (path_token != NULL) {
            addPath(path_token);
            path_token = strtok(NULL, ":");
        }
        pathgeneratedControl = 1;
    }

    int argsLen = 0;

    while(args[argsLen])
        argsLen++;

    if(argsLen == 1) {   //if user give just "path" as an argument print it separately
        //if user give just "path" as an argument print it separately
    }
    else {
        if (!strcmp(args[1], "+")) {  //if user wants to add path
            addPath(args[2]);
        }else if (!strcmp(args[1], "-")) {  //if user wants to remove path
            removePath(args[2]);
        }
    }

    //print list
    pathname_list *temp = pathname_head;
    int i = 0;
    while(i < numberofPath ){
        printf("[%d]  \t%s\n", i, temp->pathname);
        temp=temp->next;
        i++;
    }
}

void addPath(char* path_name){

    pathname_list * temp;
    if(pathname_head == NULL){
        pathname_head = (pathname_list *) malloc(sizeof(pathname_list));
        strcat(pathname_head->pathname, path_name);
        current_path= (pathname_list *) malloc(sizeof(pathname_list));
        pathname_head -> next = current_path;
    }
    else{
        int length= findCommandLength(path_name);
        for (int i = 0; i < length+1; ++i) {
            current_path->pathname[i]= path_name[i];
        }
        current_path->next = (pathname_list *) malloc(sizeof(pathname_list));
        current_path = current_path->next;
    }
    numberofPath++;
}

void removePath(char* path_name){
    pathname_list *temp = pathname_head;
    pathname_list *temp2 = pathname_head;

    if(!strcmp(pathname_head->pathname, path_name))
    {
        temp = pathname_head;
        pathname_head= pathname_head->next;
        free(temp);
    }
    else
    {
        temp = pathname_head->next;
        while(strcmp(temp->pathname,path_name)){
            temp2= temp;
            temp = temp->next;
        }

        temp2->next = temp->next;
        free(temp);
    }
    numberofPath--;
}

void removeProcess(int data) {

    background_process* temp= head;

    if(head->p_id == data)
    {
        temp = head;
        head= head->next;
        free(temp);
    }
    else
    {
        temp = head;
        while(temp->next->p_id != data)
        {
            temp = temp->next;
        }
        background_process* temp1 = temp->next->next;
        background_process* temp2 = temp;
        free(temp->next);
        temp2->next = temp1;
    }
    backgroundProcessNumber--;

    if (backgroundProcessNumber == 0){

        head = NULL;
        previous = NULL;
    }
}

void fg(int id_num) { /*command line arguments */

    background_process* temp = head;

    char FgBuffer [MAX_LINE];

    int background = 0; /* equals 1 if a command is followed by '&' */
    char *FgArgs[MAX_LINE/2 + 1]; /*command line arguments */

    bool fgIdValid = false;
    int cnt = backgroundProcessNumber;
    while(cnt--)  //get command of process which has fg command pid
    {
        int id = id_num;
        if(temp->p_id == id){
            strcpy(FgBuffer, temp->command);
            fgIdValid = true;
            break;
        }
        temp = temp->next;
    }

    if(fgIdValid){

        int length = findCommandLength(FgBuffer) ;
        controlVariableForFG = 1;
        setupHist_Fg(FgBuffer, FgArgs, &background, length-1, controlVariableForFG);
        controlVariableForFG = 0;
        removeProcess(id_num);
        execute(FgArgs , background,1,0);
        printQueue();

    } else
        fprintf(stderr,"There is no such background process which has this id\n");

}

int ioRedirection(char *args[]) {

    int i = 0;
    while (args[i]) {
        if (!strcmp(args[i], ">")) {
            TRUNC_OUT = 1;
        } else if (!strcmp(args[i], ">>")) {
            APPEND_OUT = 1;
            break;
        } else if (!strcmp(args[i], "<")) {
            IN_MODE = 1;
        } else if (!strcmp(args[i], "2>")) {
            ERR_OUT = 1;
            break;
        }
        i++;
    }

    if (IN_MODE == 1 && TRUNC_OUT == 1) {   // This condition is for opening output file of the command "x < y > z"
        pid_t pid = fork();
        if (pid == 0) {
            int inputFile;
            int outputFile;
            char *path = "/bin/";
            //input file open
            inputFile = open(args[arg_count - 3], O_RDWR, CREATE_MODE);
            //new file
            outputFile = open(args[arg_count - 1], CREATE_FLAGS, CREATE_MODE);

            if (inputFile == -1) {
                perror("Failed to open file");
                return 1;
            }

            if (dup2(inputFile, STDIN_FILENO) == -1) {  //dup2 redirects standard input to the input file
                perror("Failed to redirect standart output");
                return 1;
            }

            if (close(inputFile) == -1) {
                perror("Failed to close the file");
                return 1;
            }

            if (outputFile == -1) {
                perror("Failed to open file");
                return 1;
            }

            if (dup2(outputFile, STDOUT_FILENO) == -1) {    //dup2 redirects standard output to the output file
                perror("Failed to redirect standart output");
                return 1;
            }

            if (close(outputFile) == -1) {
                perror("Failed to close the file");
                return 1;
            }


            char kp[50];
            //creating path
            strcpy(kp, path);
            strcat(kp, args[0]);
            args[arg_count - 4] = NULL;

            if (execv(kp, args) == -1) {
                //fprintf(stderr, "Child does not work \n");
            }
        } else if (pid > 0) {
            wait(&pid);
        }
        IN_MODE = 0;
        TRUNC_OUT = 0;
    }

    else if (TRUNC_OUT == 1) { // Write the standard output of program to the file
        pid_t pid;
        char *path = "/bin/"; //Start Path
        pid = fork();

        // Create file if needed
        if (pid == 0) {
            int openFile;
            openFile = open(args[arg_count - 1], CREATE_FLAGS, CREATE_MODE);
            // Check if file opened
            if (openFile == -1) {
                fprintf(stderr, "Failed to open file\n");
                return 1;
            }
            //Check if redirection is finished
            if (dup2(openFile, STDOUT_FILENO) == -1) {
                fprintf(stderr, "Failed to redirect standard output\n");
                return 1;
            }
            //Check if the file is gonna close
            if (close(openFile) == -1) {
                fprintf(stderr, "Failed to close the file\n");
                return 1;
            }
            char pathForExec[50];
            //create a path for the redirection
            //copy path
            strcpy(pathForExec, path);
            //add args for different commands
            strcat(pathForExec, args[0]);
            args[arg_count - 1] = NULL;
            args[arg_count - 2] = NULL; //The last two arrays will be NULL
            //Print the errors in stderr like requested
            if (execv(pathForExec, args) == -1) {
                fprintf(stderr, "Child process couldn't run \n");
            }
        } else if (pid > 0) {
            wait(&pid);
        }
        TRUNC_OUT = 0;
    }

    else if (APPEND_OUT == 1) {
        char* path="/bin/"; //Start Path
        pid_t pid= fork();

        if(pid==0){
            int openFile;
            // create new folder
            openFile = open(args[arg_count-1],CREATE_APPENDFLAGS,CREATE_MODE);
            if(openFile == -1){
                fprintf(stderr, "Failed to open file\n");
                return 1;
            }
            if(dup2(openFile,STDOUT_FILENO) == -1){
                fprintf(stderr, "Failed to redirect standard output\n");
                return 1;
            }

            if(close(openFile) == -1){
                fprintf(stderr, "Failed to close the file\n");
                return 1;
            }

            char pathForExec[50];

            strcpy(pathForExec, path);
            strcat(pathForExec, args[0]);

            args[arg_count-1] = NULL;
            args[arg_count-2] = NULL;

            if (execv(pathForExec,args) == -1){
                fprintf(stderr, "Child does not work \n");
            }

        }
        else if(pid>0){
            wait(&pid);
        }
        APPEND_OUT = 0;
    }

    else if (IN_MODE == 1) {
        pid_t pid = fork();
        if (pid == 0) {
            int fd_in;
            // open in read only mode, if opening fails...
            if ((fd_in = open(args[arg_count - 1], O_RDONLY)) < 0) {
                fprintf(stderr, "Couldn't open input file\n");
                exit(-1);
            }
            // duplicate standard input file
            dup2(fd_in, STDIN_FILENO);

        } else if (pid > 0) {
            wait(pid);
        }
        IN_MODE = 0;
    }

    else if (ERR_OUT == 1) {
        pid_t pid = fork();
        if (pid == 0) {
            int fd_err;
            // open file with write permission, create if doesn't exists, truncate if exists...
            // with desired read, write, execute permissions (0644 in our program)
            if ((fd_err = open(args[arg_count - 1], O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
                fprintf(stderr, "Couldn't open output file\n");
                exit(-1);
            }
            // redirect standard error file to the given file
            dup2(fd_err, STDERR_FILENO);
            close(fd_err);                   // close file
        } else if (pid > 0) {
            wait(&pid);
        }
        ERR_OUT = 0;
    }

}

int findCommandLength(char * command){
    int num= 0;
    int spacenum=0;
    for (int j = 0; j < MAX_LINE; j++) {  //define length of history buffer
        if(command[j] == '\0'){
            break;
        }
        if(command[j] == ' ')
            spacenum++;

        num++;
    }
    if(spacenum == 0)
        num--;
    return num;
}

int exitt( ){     // If user entered exit
    // Check background processes
    if(head != NULL) {
        // inform user
        fprintf(stderr, "\nThere are some processes that are running background\n");
        printQueue();
        fflush(stdout);

        fprintf(stderr, "Do you want to terminate all of them? [y/n] \n");
        char answer = getchar();
        if (answer == 'y') {

            head = NULL;
            previous = NULL;
            printf("Session ends\nBye!\n");
            exit(0);
        } else
            printf("Program will not terminated. There is still background processes.\n");
    }
        // If no background processes running currently, exit program
    else {
        printf("Session ends\nBye!\n");
        exit(0);
    }

}
