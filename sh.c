#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include "jobs.c"

#define ARG_SIZE 1<<10
#define BUF_SIZE 1<<10

ssize_t __real_write(int fd, const void *buf, size_t count);

ssize_t __wrap_write(int fd, const void *buf, size_t count) {
	if (count == 0) return 0;
	ssize_t return_val;
	if 	((return_val =__real_write(fd, buf, count)) == -1) {
		perror("write error");
		return -1;
	}
	return return_val;

}
int check_redirection(char *buffer, char *input, char *output) {
	int return_val = 0;
	char *inposition = NULL;
	char *outposition = NULL;

	input[0] = '\0';
	output[0] = '\0';

	if ((inposition = strchr(buffer, '<')) != NULL) {
		*inposition = ' ';
		inposition++;
		while(isspace(*inposition)) inposition++; // ignores white spaces between < and first argument
		int count = 0;
		while( *inposition != '\0' && !(isspace(*inposition)) && *inposition != '>') {
			if ( *inposition == '<')  {
				
				char *msg = "Illegal argument: cannot pass multiple redirection symbols\n";
				write(STDERR_FILENO, msg, strlen(msg));	
				return 6;
			}
			input[count] = *inposition;
			*inposition = ' ';
			count++;
			inposition++;
		}
		if (count == 0)  {
			
			char *msg = "Error: No redirection file specified\n";
			write(STDERR_FILENO, msg, strlen(msg));	
			return 6;
		}
		if (strchr(inposition, '<') != NULL) {
			
			char *msg = "Illegal argument: cannot pass multiple redirection symbols\n";
			write(STDERR_FILENO, msg, strlen(msg));	
			return 6;
		}
		input[count] = '\0';
		return_val = 1;
	}

	if ((outposition = strchr(buffer, '>')) != NULL) {
		*outposition = ' ';
		outposition++;
		if (*outposition == '>')  {
			*outposition = ' ';
			outposition++;
			return_val += 4;
		}
		else return_val += 2;

		while(isspace(*outposition)) outposition++; // removes white spaces betw < or << and first argument
		int count = 0;
		while (*outposition != '\0' && !(isspace(*outposition)) && *outposition != '>' && *outposition != '<') {
			output[count] = *outposition;
			*outposition = ' ';
			count++;
			outposition++;
		}
		if (count == 0) {
			
			char *msg = "Error: No redirection file specified\n";
			write(STDERR_FILENO, msg, strlen(msg));	
			return 6;
		}
		if (strchr(outposition, '>') != NULL) {
			
			char *msg = "Illegal argument: cannot pass multiple redirction symbols\n";
			write(STDERR_FILENO, msg, strlen(msg));	
			return 6;
		}
		output[count] = '\0';
	}

	return return_val;
	
	
}
/*
0 - no redirection
1 - < only
2 - > only
3 - < and > (order doesn't matter)
4 - >> only
5 - < and >> (order doesn't matter)
6 - error

 */

char *next_token(char *s) {
	char *c = s;
	while (*c != '\0'){
		if (isspace(*c)) {
			*c = '\0';
			c++;
			while (isspace(*c)) {
				c++;
			}
			return c;
		}
		c++;
	}
	return c;	
}

int parse_string(char *buffer, char**argv, char *input, char*output, int *red_stat) {
	char *c = buffer;
	char *delim = " \r\n\t\f\v";	
	// removes whitespaces before first argument
	
	if ((*red_stat = check_redirection(buffer, input, output)) == 6) return -1;
	

	while (*c != '\0') {
		if (!(isspace(*c))) {
			c++;
			break;
		}
		else {
			buffer++;
			c++;
		}
	}

	c = next_token(buffer);
	argv[0] = buffer;
	int arg_num = 1;
	while (*c != '\0') {
		char *s = next_token(c);
		argv[arg_num] = c;
		c = s;
		arg_num++;	
	}
	return --arg_num;
}

void reap_background(job_list_t *joblist) {
	pid_t reap;
	int state;
	while ((reap = waitpid(-1, &state, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
		//get job id and branch into states
		int job_id = get_job_jid(joblist, reap);
		if (WIFEXITED(state)) {
			// exited normally
			if(remove_job_pid(joblist, reap) == 0) {
				int exitstatus = WEXITSTATUS(state);
				char msg1[128];
				sprintf(msg1, "[%d] (%ld) terminated with exit status %d\n",job_id, (long int)reap, exitstatus);
				write(STDOUT_FILENO, msg1, strlen(msg1));
			} else {
				char *msg2 = "No such job: exited normally\n";
				write(STDERR_FILENO, msg2, strlen(msg2));
			}
		}
		if (WIFSIGNALED(state)) {
			// terminated by a signal
			if(remove_job_pid(joblist, reap) == 0) {
				int sig = WTERMSIG(state);
				char msg1[128];
				sprintf(msg1, "[%d] (%ld) terminated by signal %d\n",job_id, (long int)reap, sig);
				write(STDOUT_FILENO, msg1, strlen(msg1));
			} else {
				char *msg2 = "No such job: terminated by signal\n";
				write(STDERR_FILENO, msg2, strlen(msg2));
			}	
		}
		if (WIFSTOPPED(state)) {
			//stopped
			if(update_job_pid(joblist, reap, _STATE_STOPPED) == 0) {
				int sig = WSTOPSIG(state);
				char msg1[128];
				sprintf(msg1, "[%d] (%ld) suspended by signal %d\n",job_id, (long int)reap, sig);
				write(STDOUT_FILENO, msg1, strlen(msg1));

			} else {
				char *msg2 = "No such job: suspended by signal\n";
				write(STDERR_FILENO, msg2, strlen(msg2));
			}
		}
		if (WIFCONTINUED(state)) {
			// Continued
			if(update_job_pid(joblist, reap, _STATE_RUNNING) == 0) {
				char msg[128];
				sprintf(msg, "[%d] (%ld) resumed\n", job_id, (long int)reap);
				write(STDOUT_FILENO, msg, strlen(msg));
			}
			else {
				char *msg2 = "No such job: resumed by signal\n";
				write(STDERR_FILENO, msg2, strlen(msg2));
			}
		}

	}
}

void reap_foreground(pid_t child, job_list_t *joblist) {
	int status;
	waitpid(child, &status, WUNTRACED); 
	int job_id = get_job_jid(joblist, child);
	if (WIFEXITED(status)) {
		// exited normally
		if(remove_job_pid(joblist, child) == -1) {
			char *msg2 = "No such job: exited normally\n";
			write(STDERR_FILENO, msg2, strlen(msg2));
		} 
	}	
	if (WIFSIGNALED(status)) {
		// terminated by a signal
		if(remove_job_pid(joblist, child) == 0) {
			int sig = WTERMSIG(status);
			char msg1[128];
			sprintf(msg1, "[%d] (%ld) terminated by signal %d\n",job_id, (long int)child, sig);
			write(STDOUT_FILENO, msg1, strlen(msg1));
		} else {
			char *msg2 = "No such job: terminated by signal\n";
			write(STDERR_FILENO, msg2, strlen(msg2));
		}	
	}
	if (WIFSTOPPED(status)) {
		//stopped
		if(update_job_pid(joblist, child, _STATE_STOPPED) == 0) {
			int sig = WSTOPSIG(status);
			char msg1[128];
			sprintf(msg1, "[%d] (%ld) suspended by signal%d\n",job_id, (long int)child, sig);
			write(STDOUT_FILENO, msg1, strlen(msg1));

		} else {
			char *msg2 = "No such job: suspended by signal\n";
			write(STDERR_FILENO, msg2, strlen(msg2));
		}

	}
}

int built_in(char *command, char **argv, int arg_num, job_list_t *joblist) {
	if(strcmp(command, "cd") == 0) {
		if (arg_num == 1) {
			chdir(getenv("HOME"));
			return 0;	
		}	
		char *newpath = argv[1];
		if (chdir(newpath) == 0) return 0;
		else {
			perror("chdir() error");
			return 0;
		}
	}
	else if(strcmp(command, "ln") == 0) {
		if (arg_num < 3) {
			char *msg = "Bad Command: Insufficient arguments for ln\n";
			write(STDERR_FILENO, msg, strlen(msg));	
			return 0;
		}
		if (link(argv[1], argv[2]) == 0) return 0;
		else {
			perror("link() error");
			return 0;
		}	
	}
	else if(strcmp(command, "rm") == 0) {
		if (arg_num == 1) {
			char *msg = "Bad Command: Insufficient arguments for rm\n";
			write(STDERR_FILENO, msg, strlen(msg));
			return 0;
		}
		if (unlink(argv[1]) == 0) return 0;
		else {
			perror("unlink() error");
			return 0;
		}
	}
	else if(strcmp(command, "jobs") == 0) {
		jobs(joblist);
		return 0;
	}
	else if(strcmp(command, "fg") == 0) {
		if (arg_num == 1) {
			char *msg = "Usage: fg [job id]\n";
			write(STDERR_FILENO, msg, strlen(msg));
			return 0;
		}
		if(*argv[1] == '%') argv[1]++;
		int job_resurrect = atoi(argv[1]);
		pid_t pid_resurrect;
		if ((pid_resurrect = get_job_pid(joblist, job_resurrect)) == -1) {
			char msg[128];
			sprintf(msg, "No job number %d exists in joblist\n", job_resurrect);
			write(STDERR_FILENO, msg, strlen(msg));
		} else {
			if (kill(-pid_resurrect, SIGCONT) == -1) { // Attempts to send SIGCONT to target process
				char *msg = "Cannot send SIGCONT to target process\n";
				write(STDERR_FILENO, msg, strlen(msg));
				return 0;
			}

			signal(SIGTTOU, SIG_IGN);	
			if (tcsetpgrp(STDIN_FILENO, pid_resurrect) == -1) {
				char msg[128];
				sprintf(msg, "failed to change target process group to %ld\n", (long int)pid_resurrect);
				write(STDERR_FILENO, msg, strlen(msg));
				exit(1);	
			} // forward signal delivery to that foreground job
			signal(SIGTTOU, SIG_DFL);

			reap_foreground(pid_resurrect, joblist);
			// wait for this process to finish and reap it

			signal(SIGTTOU, SIG_IGN);
			if (tcsetpgrp(STDIN_FILENO, getpid()) == -1) {
				char *msg = "Cannot revert back to current progress group\n";
				write(STDERR_FILENO, msg, strlen(msg));
				exit(1);
			} // reverts signal and input handling back to current shell
			signal(SIGTTOU, SIG_DFL);
		}
		return 0;
	}
	else if(strcmp(command, "bg") == 0) {
		if (arg_num == 1) {
			char *msg = "Usage: bg [job id]\n";
			write(STDERR_FILENO, msg, strlen(msg));
			return 0;
		}
		if(*argv[1] == '%') argv[1]++;
		int job_resurrect = atoi(argv[1]);
		pid_t pid_resurrect;
		if ((pid_resurrect = get_job_pid(joblist, job_resurrect)) == -1) {
			char msg[128];
			sprintf(msg, "No job number %d exists in joblist\n", job_resurrect);
			write(STDERR_FILENO, msg, strlen(msg));
		} else { // simply sends SIGCONT to target process and continues
			if (kill(-pid_resurrect, SIGCONT) == -1) {
				char *msg = "Cannot send SIGCONT to target process\n";
				write(STDERR_FILENO, msg, strlen(msg));
			} 
		}
		return 0;
	}
	return -1;
}

int main() {

    /* TODO: everything! */
	char buffer[BUF_SIZE];
	char *argv[ARG_SIZE];

	signal(SIGINT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);

	job_list_t *joblist = init_job_list();
	int jid = 1;

	while(1) {
		for (int i = 0; i < BUF_SIZE; i++) {
		 	buffer[i] = '\0';
		 	argv[i] = NULL;
		}
		//char buffer[BUF_SIZE];
		//char *argv[ARG_SIZE];

		signal(SIGINT, SIG_IGN);
		#ifdef PROMPT	
		char cwd[BUF_SIZE];
		if (getcwd(cwd, BUF_SIZE) == NULL) {
			perror("getcwd() error");
			cleanup_job_list(joblist);
			exit(1);
		}	
		write(STDIN_FILENO, cwd, strlen(cwd));
		write(STDIN_FILENO, " $ ", 3);
		#endif
		ssize_t buf_read;
		if ((buf_read = read(STDIN_FILENO, buffer, BUF_SIZE)) == -1) {
			perror("read() error");
			cleanup_job_list(joblist);
			exit(1);
		}
		if (buf_read == 0) {
			cleanup_job_list(joblist);
			exit(0);
		} 
		buffer[buf_read] = '\0';

		reap_background(joblist);

		int red_val = 0;
		char input[BUF_SIZE];
		char output[BUF_SIZE];
		int arg_num; 
		if ((arg_num = parse_string(buffer, argv, input, output, &red_val)) == -1) {
			write(STDERR_FILENO,"Error parsing arguments\n",24);
			continue;
		}		
		
		// string parsed.
		if (arg_num == 0) {
			continue;
		}
		argv[arg_num] = NULL;
		
		char *command = argv[0];
		if (strcmp(command, "exit") == 0) {
			cleanup_job_list(joblist);
			exit(0);
		}
		
		if(built_in(command, argv, arg_num, joblist) == 0) continue;

		// handled built-in function
		int is_background;
		if (strcmp(argv[arg_num-1], "&") == 0) {
			arg_num--;
			if (arg_num == 0) {
				char *msg = "Unexpected syntax near &\n";
				write(STDERR_FILENO, msg, strlen(msg));
				continue;
			}
			argv[arg_num] = NULL;
			is_background = 1;
		} else is_background = 0;

		
		pid_t child = fork();
	
		if (child > 0) {
			//still in the parent process
			if (add_job(joblist, jid, child, _STATE_RUNNING, command) == -1) {
				char *msg = "failed to add job to joblist\n";
				write(STDERR_FILENO, msg, strlen(msg));
				exit(1);
			} else jid++;
	
			//printf("new pid: %ld\n", (long int)child);
			if (setpgid(child, child) == -1) {
				char msg[128];
				sprintf(msg, "failed to set process id to %ld\n", (long int)child);
				write(STDERR_FILENO, msg, strlen(msg));
				exit(1);
			}
			if (is_background == 0) { // foreground job
				signal(SIGTTOU, SIG_IGN);	
				if (tcsetpgrp(STDIN_FILENO, child) == -1) {
					char msg[128];
					sprintf(msg, "failed to change target process group to %ld\n", (long int)child);
					write(STDERR_FILENO, msg, strlen(msg));
					exit(1);	
				} // forward signal delivery to that foreground job
				signal(SIGTTOU, SIG_DFL);
			}

			else if (is_background == 1) { // background job
				char msg[BUF_SIZE];
				sprintf(msg, "[%d] (%ld)\n", (jid-1), (long int)child);
				write(STDOUT_FILENO, msg, strlen(msg)); // inform background job
			}
		}// end parent

		if (child < 0) {
			char *msg = "Cannot create new child process\n";
			write(STDERR_FILENO, msg, strlen(msg));
			exit(1);
		}

		if (child == 0) {
			// in child process	
			signal(SIGINT, SIG_DFL);
			signal(SIGTSTP, SIG_DFL);
			signal(SIGQUIT, SIG_DFL);


			char newpath[BUF_SIZE]; // storing path before I destroy command
			char *c;
			strcpy(newpath, command); // I know newpath will never overflow
			while((c = strchr(command, '/')) != NULL) {
				command = ++c; // modifying command to actual executable file name
			}
			// redirection - My redirection integer was constructed in a fashion that:
			// firt bit - <, second bit - >, third bit - >>
			if ((red_val & 1) == 1) {
				// input
				int inpfil = open(input, O_RDONLY);
				if(inpfil == -1) {
					char msg[128];
					sprintf(msg, "failed to open file %s\n", input);
					write(STDERR_FILENO, msg, strlen(msg));	

					exit(1);
				}
				if (dup2(inpfil, STDIN_FILENO) == -1) {
					char msg[128];
					sprintf(msg, "failed to connect file to input %s\n", input);
					write(STDERR_FILENO, msg, strlen(msg));

					exit(1);
				}
			}	
			if ((red_val & 2) == 2) {
				// new_output
				int out_tru = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0666);
				if (out_tru == -1) {
					char msg[128];
					sprintf(msg, "failed to open or create file %s\n", output);
					write(STDERR_FILENO, msg, strlen(msg));

					exit(1);
				}
				if (dup2(out_tru, STDOUT_FILENO) == -1) {
					char msg[128];
					sprintf(msg, "failed to connect file to output %s\n", output);
					write(STDERR_FILENO, msg, strlen(msg));

					exit(1);
				}
			}
			if ((red_val & 4) == 4) {
				// output_append
				int out_append = open(output, O_WRONLY | O_CREAT | O_APPEND, 0666);
				if (out_append == -1) {
					char msg[128];
					sprintf(msg, "failed to open or create file %s\n", output);
					write(STDERR_FILENO, msg, strlen(msg));

					exit(1);
				}
			       if (dup2(out_append, STDOUT_FILENO) == -1) {
					char msg[128];
					sprintf(msg, "failed to connect file to output %s\n", output);
					write(STDERR_FILENO, msg, strlen(msg));

					exit(1);
			       }	       
			}

			execv(newpath, argv);
			
			// execv failedNULL
			char msg[128];
			if (errno == ENOENT){
				sprintf(msg, "Command not found:%s\n", command);
				write(STDERR_FILENO, msg, strlen(msg));	
			}
			else {		
				sprintf(msg, "Execution of %s failed:%s\n", command, strerror(errno));
				write(STDERR_FILENO, msg, strlen(msg));
			}
			exit(1);
		}
		
		if (is_background == 0) {	
			reap_foreground(child, joblist);
		}


		//reaping
		reap_background(joblist);

		signal(SIGTTOU, SIG_IGN);
		if (tcsetpgrp(STDIN_FILENO, getpid()) == -1) {
			char *msg = "Cannot revert back to current progress group\n";
			write(STDERR_FILENO, msg, strlen(msg));
			exit(1);
		}
		signal(SIGTTOU, SIG_DFL);

	}
	return 0;
}


