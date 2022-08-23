#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <linux/limits.h>
#include <fcntl.h>

int check_redirect(char** args, int number, int counter);
int redirect_io(char **args, int a, char* file, int value);
void term_job(int pid);
void child_died(int sig);
void susp_job_handler(int sig);
void my_jobs();
void set_status(int pid, int status);
void my_fg(char* arg);
void my_bg(char* arg);

int jobs[10][3];
int jobs_num = 0;
int fg_pid = -1;
int state_id = 0;

int num_builtins = 5;
char *builtin_func[] = {
    "cd",
    "exit",
    "jobs",
    "fg",
    "bg"
};


void print_dir() {
    char directory[PATH_MAX];
    printf("\033[1;36m%s ", getcwd(directory, sizeof(directory)));
}


void cd(char* path) {
    if(chdir(path) != 0) {
        printf("Wrong path!\n");
    }
}


char* read_line() {
    char* line = NULL;
    ssize_t bufsize = 0;

    if(getline(&line, &bufsize, stdin) == -1) {
        printf("\n");
        exit(0);
    }
    return line;
}


char** parse_line(char* str) {
    char** args_holder;
    char* arg;
    int i = 0;
    char* DELIMITER = " ";
    int ARGS_SIZE = 60;
    args_holder = malloc(ARGS_SIZE * sizeof(char*));
    str[strlen(str) - 1] = '\0';

    arg = strtok(str, DELIMITER);

    while(arg != NULL) {
        args_holder[i] = arg;
        i++;
        arg = strtok(NULL, DELIMITER);
    }

    args_holder[i] = NULL;
    return args_holder;
}


int ampersand(char** args) {
    int i = 0;

    while(args[i] != NULL) {
        if(strcmp(args[i], "&") == 0) {
            if(args[i + 1] == NULL) {
                args[i] = NULL;
                return 1;
            }
        }
        i++;
    }

    return 0;
}


int execute(char** args) {
    int status;

    if(!strcmp(args[0], "cd")) {
        if(args[2] != NULL) {
            printf("Too many arguments for command cd!\n");
        } else if(args[1] == NULL) {
            cd("/");
        } else {
            cd(args[1]);
        }
    } else if(!strcmp(args[0], "exit")) {
        return 0;
    } else if(!strcmp(args[0], "jobs")) {
        if(args[1] != NULL) {
            printf("Too many arguments for command \"jobs\"!\n");
        } else {
            my_jobs();
        }
    } else if(!strcmp(args[0], "fg")) {
        if(args[2] != NULL) {
            printf("Too many arguments for command \"fg\"!\n");
        } else {
            my_fg(args[1]);
        }
    } else if(!strcmp(args[0], "bg")) {
        if(args[2] != NULL) {
            printf("Too many arguments for command \"bg\"!\n");
        } else {
            my_bg(args[1]);
        }
    }

    return 1;
}


int pipe_handler(char ***args, int counter, int amp) {
    int pipefd[2][2];
    int i = 0;
    int status;

    while(i < counter + 1) {
        fg_pid = -1;

        if(i % 2 == 0) {
            pipe(pipefd[1]);
        } else {
            pipe(pipefd[0]);
        }

        int child = fork();

        if(child == 0) {
            if(check_redirect(args[i], i, counter) == -1) {
                printf("ERROR\n");
                return 1;
            }

            if(i == 0 && counter != 0) {
                dup2(pipefd[1][1], 1);
            } else if(i == counter) {
                if ((counter + 1) % 2 != 0) {
                    dup2(pipefd[0][0], STDIN_FILENO);
                } else {
                    dup2(pipefd[1][0], STDIN_FILENO);
                }
            } else {
                if (i % 2 != 0) {
                    dup2(pipefd[1][0], STDIN_FILENO);
                    dup2(pipefd[0][1], STDOUT_FILENO);
                } else {
                    dup2(pipefd[0][0], STDIN_FILENO);
                    dup2(pipefd[1][1], STDOUT_FILENO);
                }
            }

            if(amp == 0) {
                signal(SIGINT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);
            }

            if(execvp(args[i][0], args[i])) {
                perror("Error executing command\n");
            }

            exit(1);
        }

        if(i == 0) {
            close(pipefd[1][1]);
        } else if(i == counter) {
            if((counter + 1) % 2 == 0) {
                close(pipefd[1][0]);
            } else {
                close(pipefd[0][0]);
            }
        } else {
            if(i % 2 == 0) {
                close(pipefd[0][0]);
                close(pipefd[1][1]);
            } else {
                close(pipefd[0][1]);
                close(pipefd[1][0]);
            }
        }

        if(amp == 0) {
            jobs[jobs_num][0] = child;
            jobs[jobs_num][1] = 1;
            jobs[jobs_num][2] = 1;
            jobs_num++;
            fg_pid = child;
            state_id = 1;
            waitpid(child, &status, WUNTRACED);

            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                term_job(child);
            }
        } else {
            jobs[jobs_num][0] = child;
            jobs[jobs_num][1] = 300;
            jobs[jobs_num][2] = 1;
            jobs_num++;
            signal(SIGCHLD, child_died);
        }

        i++;
    }
    return 1;
}


void my_jobs() {
    int i = 0;

    while(jobs[i][0] > 0) {
        printf("[%d] PID: %d STATUS: %d\n", i, jobs[i][0], jobs[i][1]);
        i++;
    }
}

void child_died(int sig) {
    int pid = -1;
    int x = 0;

    while(jobs[x][0] != 0) {
        x++;
    }

    if(state_id < 1) {
        pid = wait(NULL);
    }

    if(pid > 0) {
        printf("\nTERMINATING PROCCESS WITH PID: %d\n", pid);
        term_job(pid);
    }

    if(state_id > 0) {
        state_id = 0;
    }
}

void susp_job_handler(int sig) {
    int pid = fg_pid;

    if(pid > 0) {
        set_status(pid, 100);
        kill(pid, SIGTSTP);
    }
}


void set_status(int pid, int status) {
    int x = 0;

    while(jobs[x][0] != pid && x < jobs_num) {
        x++;
    }

    jobs[x][1] = status;
    printf("\nSTATUS CHANGED FOR PID: %d\n", pid);
}


void term_job(int pid) {
    int index = 0;
    int x = 0;

    while(jobs[index][0] != pid && index < jobs_num) {
        index++;
    }

    for(x = index; x < jobs_num; x++) {
        jobs[x][0] = jobs[x + 1][0];
        jobs[x][1] = jobs[x + 1][1];
        jobs[x][2] = jobs[x + 1][2];
    }
    jobs_num--;
}

void my_fg(char* arg) {
    int pid = atoi(arg);
    int i;
    int found = 0;

    for(i = 0; i < jobs_num; i++) {
        if(jobs[i][0] == pid) {
            found = 1;
        }
    }

    if(found) {
        int status;
        printf("PUTTING PID: %d IN FOREGROUND\n", pid);
        set_status(pid, 200);
        state_id = 2;
        kill(pid, SIGCONT);
        waitpid(pid, &status, WUNTRACED);

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            term_job(pid);
        } else if (WIFSTOPPED(status)) {
            printf("Status error\n");
        }
    } else {
        printf("Wrong pid\n");
    }

}


void my_bg(char* arg) {
    int pid = atoi(arg);
    int i;
    int found = 0;

    for(i = 0; i < jobs_num; i++) {
        if(jobs[i][0] == pid) {
            found = 1;
        }
    }

    if(found) {
        printf("PUTTING PID: %d IN BACKGROUND\n", pid);
        set_status(pid, 300);
        state_id = 3;
        kill(pid, SIGCONT);
    } else {
        printf("Wrong pid\n");
    }
}


char*** parse_command(char **args, int *pipe_count) {
    char ***result = malloc(20 * 20 * 20 * sizeof(char*));
    int i = 0;
    int j = 0;
    int k = 0;
    result[0] = malloc(6400 * sizeof(char*));

    while(args[i] != NULL) {
        if(strcmp(args[i], "|") != 0) {
            result[j][k] = args[i];
            k++;
        } else {
            (*pipe_count)++;
            j++;
            result[j] = malloc(6400 * sizeof(char*));
            k = 0;
        }

        i++;
    }
    return result;
}


int check_redirect(char **args, int number, int counter) {
    int a = 0;

    while(args[a] != NULL) {
        if(strcmp(args[a], "<") == 0) {
            if(number != 0) {
                return -1;
            } else if (args[a + 1] == NULL) {
                printf("Not enough arguments for \"<\" operator\n");
                return -1;
            } else {
                return redirect_io(args, a, args[a + 1], 0);
            }
        } else if(strcmp(args[a], ">") == 0) {
            if(number != counter) {
                return -1;
            } else if(args[a + 1] == NULL) {
                printf("Not enough arguments for \">\" operator\n");
                return -1;
            } else {
                return redirect_io(args, a, args[a + 1], 1);
            }
        } else if(strcmp(args[a], "2>") == 0) {
            if(number != counter) {
                return -1;
            } else if(args[a + 1] == NULL) {
                printf("Not enough arguments for \"2>\" operator\n");
                return -1;
            } else {
                return redirect_io(args, a, args[a + 1], 2);
            }
        }
        a++;
    }

    return 0;
}


int redirect_io(char **args, int a, char* file, int value) {
    int temp = 0;
    char *command[100];

    while(temp < a) {
        command[temp] = args[temp];
        temp++;
    }
    command[temp] = NULL;

    args[a] = NULL;

    if(value == 0) {
        int input_fd = open(file, O_RDONLY, 0600);
        dup2(input_fd, 0);
        close(input_fd);
    } else if(value == 1) {
        int output_fd = open(file, O_CREAT | O_TRUNC | O_WRONLY, 0600);
        dup2(output_fd, 1);
        close(output_fd);
    } else if(value == 2) {
        int output_fd = open(file, O_CREAT | O_TRUNC | O_WRONLY, 0600);
        dup2(output_fd, 2);
        close(output_fd);
    }

    return 0;
}


void start_loop() {
    char *line;
    char **args;
    char ***pipe_parsed;
    int status = 1;
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, susp_job_handler);

    while(status) {
        print_dir();
        printf("\033[1;31mlsh > \033[0m");
        line = read_line();

        if(strlen(line) == 1) {
            continue;
        }

        args = parse_line(line);
        int amp = ampersand(args);
        int pipe_count = 0;
        pipe_parsed = parse_command(args, &pipe_count);

        for (int i = 0; i < num_builtins; i++) {
            if (strcmp(args[0], builtin_func[i]) == 0) {
                status = 2;
            }
        }

        if(status == 2) {
            status = execute(args);
        } else {
            status = pipe_handler(pipe_parsed, pipe_count, amp);
        }
    }
}

int main() {
    start_loop();
    return 0;
}
