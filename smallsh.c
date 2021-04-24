/*****
 * Camille Marcojos
 * CS 344 Summer 2020 - Assigment 2: smallsh (Portfolio Assignment)
 * Description: In this assignment you will write your own shell in C, similar to bash. No other languages, including C++, are allowed, though you may use C99. 
 * The shell will run command line instructions and return the results similar to other shells you have used, but without many of their fancier features.
 * The shell supports three built in commands: exit, cd, and status. It also supports comments, which are lines beginning with the # character.
 *****/
#include <stdio.h>  //for printf and perror
#include <stdlib.h> //for exit
#include <string.h>
#include <stdbool.h>
#include <sys/wait.h> //for waitpid
#include <sys/types.h>
#include <unistd.h> //for execv, getpid, fork, dup2
#include <fcntl.h>
#include <signal.h>
#define MAX 2048

int argsCount = 0;
bool input_redirection = false;
bool output_redirection = false;
bool run_background = false;
bool background_allowed = true;
int childStatus = 0;


char** parseInput(char userInput[], char[], char[]);
void execute_command(char** args, char input_file[], char output_file[], struct sigaction* sig_action);
void convertpid(char* input, char** args);
bool check_background(char** args);
void edit_args(char** args);
void redirect(char input_file[], char output_file[], char** args);
void exit_status();
void handle_SIGTSTP(int signo);

int main()
{
    char input_file[256]= "";
    char output_file[256]="";
    char userInput[256]= "";
    char** args;
    bool run=true;

    //implementing signal management for SIGTSTP
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP,&SIGTSTP_action,NULL);

    //implementing signal management for SIGINT
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT,&SIGINT_action,NULL);

    do{
        // Get user input
        printf(": ");
        fflush(stdout);
        fgets(userInput,MAX,stdin);

        // Check if comment or empty
        if(userInput[0] =='\n'){
            //do nothing, repeat prompt
            continue;
        }else if(userInput[0] =='#'){
            //do nothing, repeat prompt
            continue;
        }else{
            //read user input for file redirections, commands, and other arguments
            args = parseInput(userInput, input_file, output_file);
            
            /***** TESTING 
            printf("\n...printing arguments...\n");
            for(int i=0; i < argsCount; i++){
                printf("%s\n", args[i]);
            }

            printf("\n...printing file names...\n");
            printf("input file: %s\n", input_file);
            printf("output file: %s\n", output_file);
            ******/

            if(strcmp(args[0], "exit") == 0){
                exit(0);
            }else if(strcmp(args[0], "status") == 0){
                exit_status();
            }else if(strcmp(args[0], "cd") == 0){
                if(args[1] == NULL){
                    chdir(getenv("HOME"));
                }else{
                    chdir(args[1]);
                }
            }else{
                execute_command(args, input_file, output_file,&SIGINT_action);
            }

        }

        //free memory
        for(int i = 0; i < argsCount; i++){
            free(args[i]);
        }
        free(args);

        //reset all variables for next command
        argsCount = 0;
        if(input_redirection){
            memset(input_file,0,256);
            input_redirection = false;
        }
        if(output_redirection){
            memset(output_file,0,256);
            output_redirection = false;
        }
        
    }while(run);

    return 0;

}

/***
 * parseInput
 * parses user input and adds the commands and arguments to an arg list
 * **/
char** parseInput(char userInput[], char input_file[], char output_file[]){
    //holds args for execution
    char** args = (char**) malloc(513 * sizeof(char*));
    //if any of the arguments contain && for conversion
    bool getpid = false;
    char* token;
    token = strtok(userInput, " \n");

    while(token != NULL){
        if(strcmp(token,"<") == 0){
            args[argsCount] = malloc(strlen(token) * sizeof(char));
            strcpy(args[argsCount], token);
            //testing...printf("argument %d: %s\n",argsCount,args[argsCount]);
            argsCount++;
            //testing...printf("read < token: %s\n",token);
            //advance to next string which contains the file name
            token = strtok(NULL," \n");
            strncpy(input_file, token, strlen(token));
            //testing...printf("input file: %s\n",input_file);
            input_redirection = true;
            //advance to next string
            token = strtok(NULL," \n");
        }else if(strcmp(token,">") == 0){
            //testing...printf("read > token: %s\n",token);
            args[argsCount] = malloc(strlen(token) * sizeof(char));
            strcpy(args[argsCount], token);
            //testing...printf("argument %d: %s\n",argsCount,args[argsCount]);
            argsCount++;
            //advance to next string which contains the file name
            token = strtok(NULL," ");
            strncpy(output_file, token, strlen(token)-1);
            //testing...printf("output file: %s\n",output_file);
            output_redirection = true;
            //advance to next string
            token = strtok(NULL," \n");
        }else{
            //testing...printf("read argument: %s\n",token);
            //if the token contains $$, expand it into the processID of the shell
            for(int i=0; i < strlen(token); i++){
                if((token[i] == '$') && (i + 1 <strlen(token)) && (token[i+1] == '$')){
                    getpid = true;
                    break;
                }
            }

            if(getpid){
                convertpid(token,args);
                getpid=false;
                //advance to next string
                token = strtok(NULL," \n");
            }else{
                args[argsCount] = malloc(strlen(token) * sizeof(char));
                strcpy(args[argsCount], token);
                //printf("argument %d: %s\n",argsCount,args[argsCount]);
                argsCount++;
                //advance to next string
                token = strtok(NULL," \n");
            }
        }
    }
    //setting the last argument to NULL for execvp
    args[argsCount] = NULL;

    return args;
}

/*****
 * execute_command
 * executes the user inputted commands from the command line (non built-in) i
 *****/
void execute_command(char** args, char input_file[], char output_file[], struct sigaction* SIGINT_action){

    //check if the process needs to be run in the background
    run_background=check_background(args);

    pid_t childPid;
    pid_t spawnPid = fork();

    switch(spawnPid){
        case -1:
            perror("fork() failed\n");
            exit(1);
            break;
        case 0: 
            //child process terminates when SIGINT signal is received
            SIGINT_action->sa_handler = SIG_DFL;
            sigaction(SIGINT,SIGINT_action,NULL);

            //setting up redirection files if redirection is needed
            redirect(input_file, output_file, args);
            //testing...printf("Child(%d) running command: %s\n", getpid(), args[0]);
            execvp(args[0],args);
            //exec only returns if there is an error
            perror("execve");
            exit(1);
            break;
        default:
            //run process in bg if there is an '&' arg and SIGTSTP is off
            if(run_background && background_allowed){
                    childPid=waitpid(spawnPid, &childStatus, WNOHANG);
                    printf("background pid is %d\n", spawnPid);
                    fflush(stdout);
            }else{
                childPid=waitpid(spawnPid, &childStatus, 0);
                if(WTERMSIG(childStatus) != 0){
                    exit_status();
                }
            }
    }
    
    //check background processes
    while((childPid=waitpid(-1, &childStatus, WNOHANG)) > 0){
            printf("background pid %d is done: ", childPid);
            fflush(stdout);
            exit_status();
    };
    
}

/*****
 * convertpid
 * takes the '$$' in userinput and expands it to the processID, adds the string to the arg list
 *****/
void convertpid(char* input, char** args){
    //taken some code from stack overflow: https://stackoverflow.com/questions/53311499/c-replace-in-string-with-id-from-getpid
    int len = strlen(input) -1;
    int newlen = len + 12;
    char* pidString = malloc(newlen);
    char* buffer=strdup(input);

    for(int i=0; i < strlen(buffer); i++){
        if((buffer[i] == '$') && (i + 1 <strlen(buffer)) && (buffer[i+1] == '$')){
            buffer[i]='%';
            buffer[i+1]='d';  
        }
    }
    snprintf(pidString, newlen-1,buffer,getpid());
    args[argsCount] = malloc(strlen(pidString) * sizeof(char));
    strcpy(args[argsCount], pidString);
    //testing...printf("argument %d: %s\n",argsCount,args[argsCount]);      
    argsCount++;
    free(pidString);
    free(buffer);
}

/*****
 * check_background
 * checks if '&' is in the arg list so that the process can be ran in the bg
 *****/
bool check_background(char** args){
    if(strcmp(args[argsCount - 1],"&")==0){
        //testing...printf("Background process.\n");
        args[argsCount - 1] = NULL;
        return true;
    }
    return false;
}

/*****
 * edit_args
 * if there is </> in the arg list, this fxn edits it out of the arg list so that it can be execvp
 *****/
void edit_args(char** args){
    for(int i = 1; i < argsCount; i++){
        args[i] = NULL;
    }
}

/*****
 * redirect
 * sets up I/O streams if there is redirection needed
 *****/
void redirect(char input_file[], char output_file[], char** args){
    int sourceFD, result, targetFD;

    //redirecting I/O for processes running in the bg
    if(run_background){
        sourceFD = open("/dev/null", O_RDONLY);
        if(sourceFD == -1){
            perror("source open()");
            exit(1);
        };

        result = dup2(sourceFD,0);
        if(result == -1){
            perror("source dup2()");
            childStatus = 1;
        };

        targetFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if(targetFD == -1){
            perror("target open()");
            exit(1);
        };            

        result = dup2(targetFD, 1);
        if(result == -1){
            perror("target dup2()");
            exit(1);
        };
    }
    
    if(input_redirection){
        sourceFD = open(input_file, O_RDONLY);
        if(sourceFD == -1){
            perror("source open()");
            exit(1);
        };

        result = dup2(sourceFD,0);
        if(result == -1){
            perror("source dup2()");
            exit(1);
        };
        //need to remove </> from the argument list for execvp
        edit_args(args);
    };

    if(output_redirection){
        targetFD = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if(targetFD == -1){
            perror("target open()");
            exit(1);
        };
        result = dup2(targetFD, 1);
        if(result == -1){
            perror("target dup2()");
            exit(1);
        }
        //need to remove </> from the argument list for execvp
        edit_args(args);
    };
}

/*****
 * exit_status
 * displays exit status of process
 *****/
void exit_status(){
    //taken from Exploration: Process API - Monitoring Child Processes
    if(WIFEXITED(childStatus)){
            printf("exit value %d\n", WEXITSTATUS(childStatus));
            fflush(stdout);
        } else{
            printf("terminated by signal %d\n", WTERMSIG(childStatus));
            fflush(stdout);
    };
}

/*****
 * handle_SIGTSTP
 * custom signal handler for SIGTSTP
 *****/
void handle_SIGTSTP(int signo){
    if(background_allowed){
        char* message1 = "Entering foreground-only mode (& is now ignored)\n";
        char* message2 = ": ";
        write(1, message1, strlen(message1));
        write(1, message2, strlen(message2));
        background_allowed = false;
    }else{
        char* message1 = "Exiting foreground-only mode\n";
        char* message2 = ": ";
        write(1, message1, strlen(message1));
        write(1, message2, strlen(message2));
        background_allowed = true;
    }
}