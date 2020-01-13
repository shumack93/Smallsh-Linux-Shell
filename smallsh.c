/**************************************************************************
** Author:	Cris Shumack
** Date: 	11/20/2019
** Description:	Implementation of a simple Linux shell. Supports the
**		built-in commands cd, status, and exit. It will also
**		allow for redirection of standard input and output,
**		and will support both forground and background processes
**		(controllable by the command line and signals).
***************************************************************************/

#include <sys/stat.h> 
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>

//Set the max command line length and max arguments as constants, 
//using lengths set in the program requirements.
#define MAX_COMMAND 2048
#define MAX_ARGS 512

//Global variable for catchSIGTSTP so it knows whether to enter
//foreground only mode or exit it.
int backgroundSwitch = 1;

//Declaring functions to avoid implicit declaration warning.
void exitStatus(int childExitMethod);
void catchSIGTSTP();

//Main function that contains the loop that keeps the shell running.
int main()
{
	int i;
	int continueLoop = 1;
	int status = 0;
	char userInput[MAX_COMMAND];
	char* arguments[MAX_ARGS];
	int processes[100];
	int numOfProcesses = 0;
	char* token;
	char inputFile[100] = "\0";
	char outputFile[100] = "\0";
	char tempString[100] = "\0";
	int fdIn = -1;
	int fdOut = -1;
	int dupRes;

	//Declaring a pid_t variable and setting it to a bogus value,
	//like how it was done in the lectures.
	pid_t spawnpid = -5;

	//Initialize every element in arguments array to be NULL.
	for (i = 0; i < MAX_ARGS; i++)
		arguments[i] = NULL;

	//Initializing the required sigaction structs.
	//Citation: Lecture 3.3 - Signals
	struct sigaction SIGINT_action = {0};
	SIGINT_action.sa_handler = SIG_IGN;
	SIGINT_action.sa_flags = 0;
	sigfillset(&SIGINT_action.sa_mask);

	struct sigaction SIGTSTP_action = {0};
	SIGTSTP_action.sa_handler = catchSIGTSTP;
	SIGTSTP_action.sa_flags = 0;
	sigfillset(&SIGTSTP_action.sa_mask);

	sigaction(SIGINT, &SIGINT_action, NULL);
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	while(continueLoop == 1)
	{
		//Declare (reset) argCount and background variables back to 0
		//each time the loop starts over.
		int argCount = 0;
		int background = 0;

		//Used with strlen to get the length of the argument in order to
		//expand $$ into process ID. See below.
		size_t argLength = 0;

		//Prints the command prompt and flushes output buffer (recommended in program).
		printf(": ");
		fflush(stdout);

		//Get user input from stdin and store in userInput variable.
		fgets(userInput, MAX_COMMAND, stdin);

		token = strtok(userInput, " \n");

		//Parse user input. Handle the special cases for <, >, and &.
		//Otherwise, add the input to the arguments array.
		while (token != NULL)
		{
			if (!strcmp(token, ">"))
			{
				token = strtok(NULL, " \n");
				
				if (token != NULL)
				{
					strcpy(outputFile, token);
					token = strtok(NULL, " \n");
				}
				else
					printf("No file specified. Not redirecting.\n");
			}
			else if(!strcmp(token, "<"))
			{
				token = strtok(NULL, " \n");

				if (token != NULL)
				{
					strcpy(inputFile, token);
					token = strtok(NULL, " \n");
				}
				else
					printf("No file specified. Not redirecting.\n");
			}
			else
			{
				arguments[argCount] = token;
				token = strtok(NULL, " \n");

				argCount++;
			}
		
			//If the last argument is a & symbol, the command should be run
			//int the background. Also reduces argCount by 1 and resets the
			//last element of the arry to NULL since the & itself does not
			//really count as an argument.
			if(!strcmp(arguments[argCount - 1], "&"))
			{
				background = 1;

				arguments[argCount - 1] = NULL;				

				argCount--;
			}
		}

		//Expands $$ into process ID using a temporary string, argLength from above
		//and the current element in the arguments array.
		for(i = 0; i < argCount; i++)
		{
			argLength = strlen(arguments[i]);
			
			if(arguments[i][argLength - 1] == '$' && arguments[i][argLength - 2] == '$')
			{
				arguments[i][argLength - 2] = 0;

				sprintf(tempString, "%s%d", arguments[i], getpid());

				arguments[i] = tempString;
			}
		}

		//If nothing is entered or userInput is a comment (starting with #), do nothing.
		if(arguments[0] == '\0' || strcmp(arguments[0], "#") == 0 || arguments[0][0] == '#')
		{	
			for(i = 0; arguments[i]; i++)
	       			arguments[i] = NULL;

			continue;
		}

		//Citation: https://stackoverflow.com/questions/9493234/chdir-to-home-directory
		else if(strcmp(arguments[0], "cd") == 0)
		{
			if(arguments[1] == NULL)
				chdir(getenv("HOME"));
			else
				chdir(arguments[1]);
		}
		//If user chooses to exit, run a for loop to kill all current background processes before exiting the shell.
		else if(strcmp(arguments[0], "exit") == 0)
		{
			if (numOfProcesses > 0)
			{
				for (i = 0; i < numOfProcesses; i++)
					kill(processes[i], SIGKILL);
			}
			
			continueLoop = 0;
		}
		else if(strcmp(arguments[0], "status") == 0)
			exitStatus(status);
		else
		{
			//Idea and structure for fork and switch case below taken
			//from Lecture 3.1 - Processes.
			spawnpid = fork();

			switch (spawnpid)
			{
				case -1:
					perror("Hull Breach!");
					exit(1);

					break;

				//Child
				case 0:
					//Per program requirements, terminate only foreground command if
					//one is running, not the shell. Therefore, changing the SIGINT
					//action back to default in child.		
					SIGINT_action.sa_handler = SIG_DFL;
					sigaction(SIGINT, &SIGINT_action, NULL);
				
					if(strcmp(inputFile, ""))
					{
						//Open input file in read only mode per program requirements.
						fdIn = open(inputFile, O_RDONLY);

						//Citation: Lecture 3.4 - More UNIX I/O
						if(fdIn == -1) 
						{
							printf("Hull breach - open() failed.\n"); 
							fflush(stdout);

							exit(1); 
						}
			
						dupRes = dup2(fdIn, 0);

						//Citation: Lecture 3.4 - More UNIX I/O
						if(dupRes == -1)
						{
							perror("Error - dup2() failed.\n"); 

							exit(2); 
						}

						close(fdIn);
					}
					
					if(strcmp(outputFile, ""))
					{
						//Open output file in write only mode per program requirements.
						fdOut = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

						//Citation: Lecture 3.4 - More UNIX I/O
						if(fdOut == -1) 
						{
							printf("Hull breach - open() failed.\n"); 
							fflush(stdout);

							exit(1); 
						}
			
						dupRes = dup2(fdOut, 1);

						//Citation: Lecture 3.4 - More UNIX I/O
						if(dupRes == -1)
						{
							perror("Error - dup2() failed.\n"); 

							exit(2); 
						}

						close(fdOut);
					}
				
					//If process is in the background (and allowed) and no inputFile or outputFile
					//are specified for redirection, redirect input/output to /dev/null/.
					if(background == 1 && backgroundSwitch == 1)
					{
						if(!strcmp(inputFile, ""))
						{
							strcpy(inputFile, "/dev/null");
							fdIn = open(inputFile, O_RDONLY);

							//Citation: Lecture 3.4 - More UNIX I/O
							if(fdIn == -1) 
							{
								printf("Hull breach - open() failed.\n"); 
								fflush(stdout);

								exit(1); 
							}
			
							dupRes = dup2(fdIn, 0);

							//Citation: Lecture 3.4 - More UNIX I/O
							if(dupRes == -1)
							{
								perror("Error - dup2() failed.\n"); 

								exit(2); 
							}

							close(fdIn);
						}
					
						if(!strcmp(outputFile, ""))
						{
							strcpy(outputFile, "/dev/null");
							fdOut = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

							//Citation: Lecture 3.4 - More UNIX I/O
							if(fdOut == -1) 
							{
								printf("Hull breach - open() failed.\n"); 
								fflush(stdout);

								exit(1); 
							}
			
							dupRes = dup2(fdOut, 1);

							//Citation: Lecture 3.4 - More UNIX I/O
							if(dupRes == -1)
							{
								perror("Error - dup2() failed.\n"); 

								exit(2); 
							}

							close(fdOut);
						}
					}					

					if(execvp(arguments[0], arguments))
					{
						printf("No command, file or directory called \"%s\"\n", arguments[0]);
						fflush(stdout);

						exit(1);
					}

					break;

				//Parent
				default:
					//If user specifies background process and background
					//processes are allowed, outputs the process ID of the
					//background process, adds it to the background process,
					//and increases numOfProcesses by 1.
					if(background == 1 && backgroundSwitch == 1)
					{
						printf("background pid is %d\n", spawnpid);
						fflush(stdout);

						processes[numOfProcesses] = spawnpid;
						numOfProcesses++;

						break;
					}
					else
						waitpid(spawnpid, &status, 0);
			}
		}

		//Reset the elements in the arguments array, inputFile, and outputFile.
		for(i = 0; arguments[i]; i++)
	       		arguments[i] = NULL;

		inputFile[0] = '\0';
		outputFile[0] = '\0';
		tempString[0] = '\0';
		
		//Checks for background child processes to complete.
		spawnpid = waitpid(-1, &status, WNOHANG);	

		while (spawnpid > 0) 
		{
			printf("child %d terminated\n", spawnpid);
			fflush(stdout);

			exitStatus(status);
	
			numOfProcesses--;

			spawnpid = waitpid(-1, &status, WNOHANG);	
		}
	}
}

//Simple function that prints the exit status or terminating
//signal of the last forground process. 
//Citation: Lecture 3.1 - Processes
void exitStatus(int childExitMethod)
{
	if (WIFEXITED(childExitMethod))
	{
		int status = WEXITSTATUS(childExitMethod);

		printf("Exit status was %d\n", status);
		fflush(stdout);
	}
	else
	{
		int termSignal = WTERMSIG(childExitMethod);
	
		printf("The process was terminated by signal %d\n", termSignal);
		fflush(stdout);
	}
}

//When the SIGTSTP signal is caught, send an informational message to the 
//console and update the backgroundSwitch variable.
void catchSIGTSTP()
{
	if (backgroundSwitch == 1)
	{
		//Turning the switch off.
		backgroundSwitch = 0;

		//Outputting informative message per program requirements.
		puts("\nNow entering foreground-only mode. The & character is ignored, and background processes cannot be run.");
		fflush(stdout);
	}
	else
	{
		//Turning the switch back on.
		backgroundSwitch = 1;

		//Outputting informative message per program requirements.
		puts("\nNow exiting foreground-only mode. The & character can once again be used, and background processes are allowed.");
		fflush(stdout);
	}
}
