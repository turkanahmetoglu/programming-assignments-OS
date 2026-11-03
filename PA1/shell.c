#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "parser.h"

// file input redirection function
void handle_input_redirection(compiledCmd *cmd){
    if (cmd->inFile != NULL){
        int fd = open(cmd->inFile, O_RDONLY);

        // issue opening the file
        if (fd == -1){
            perror("open failed");
            exit(1);
        }

        dup2(fd, STDIN_FILENO);
        close(fd);
    }
}

// file output redirection function
void handle_output_redirection(compiledCmd *cmd){
    if (cmd->outFile != NULL){
        int fd = open(cmd->outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        // issue opening the file
        if (fd == -1){ 
            perror("open failed");
            exit(1);
        }

        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

// execution function
void executeCmd(CmdNode *node, compiledCmd *cmd){
    if(node->type == CMD_PIPE){

        int n = node->data.pipe.n; // no. of commands
        int pipes[n - 1][2]; // n-1 pipes each with read and write end
        
        // create pipes
        for(int i = 0 ; i < n-1 ; i++){

            if (pipe(pipes[i]) == -1) {
                perror("pipe failed");
                exit(1);
            }
        }

        // create processes 
        for (int i = 0; i < n; i++) {
            int rc = fork();

            if (rc == -1) {
                perror("fork failed");
                exit(1);
            }
            else if (rc == 0){
                char **args = node->data.pipe.argvs[i];

                // input from previous pipe
                if (i > 0){
                    dup2(pipes[i-1][0], STDIN_FILENO);
                }

                // output to next pipe
                if (i < n-1){
                    dup2(pipes[i][1], STDOUT_FILENO);
                }

                // handle if any file redirections
                if (i == 0) { // input redirection if first command
                    handle_input_redirection(cmd);
                }
                if (i == n - 1) { // output redirection if final result
                    handle_output_redirection(cmd);
                }
                
                // close all pipes
                for (int j = 0; j < n-1; j++){
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }

                execvp(args[0], args);
                perror("execvp failed");
                exit(1);

            }
            if (i > 0) {
            close(pipes[i - 1][0]);  // previous read end
            }
            if (i < n - 1) {
                close(pipes[i][1]);  // current write end
            }
        }

        // parent waits for all processes
        for (int i = 0; i < n; i++){
            wait(NULL);
        }
    }

    else if(node->type == CMD_CONDITIONAL){

        int error_pipe[2];
        if (pipe(error_pipe) == -1) {
            perror("pipe failed");
            exit(1);
        }

        int rc = fork();
        if (rc < 0) {
            perror("fork failed");
            exit(1);
        }

        // child
        if (rc == 0) {
            close(error_pipe[0]);  // close read end

            // redirect stderr to pipe
            dup2(error_pipe[1], STDERR_FILENO);
            close(error_pipe[1]);

            // condition gets input redirection
            handle_input_redirection(cmd);

            char *saved_out = cmd->outFile;
            cmd->outFile = NULL;

            // execute condition
            executeCmd(node->data.conditional.cond, cmd);

            cmd->outFile = saved_out;

            exit(0);
        }

        // parent
        close(error_pipe[1]);  // close write end

        wait(NULL);

        // read all stderr output
        char buffer[1024];
        ssize_t total = 0, n;

        while ((n = read(error_pipe[0], buffer + total, sizeof(buffer) - total)) > 0) {
            total += n;
        }

        close(error_pipe[0]);

        CmdNode *branch;
        int is_error = (total > 0);

        if (!is_error) { // success

            branch = node->data.conditional.thenBranch;
        } else { // failure
 
            write(STDERR_FILENO, buffer, total);
            branch = node->data.conditional.elseBranch;
        }


        int rc2 = fork();
        if (rc2 < 0) {
            perror("fork failed");
            exit(1);
        }
        
        // child executes branch
        if (rc2 == 0) {

            handle_output_redirection(cmd);

            executeCmd(branch, cmd);

            exit(0);
        } else {
            wait(NULL);
        }
    }
}

int main(int argc, char *argv[]){

    // parser setup
    sparser_t parser;

    if(initParser(&parser) != 1){
        fprintf(stderr, "Parser initialization failed\n");
        return 1;
    }

    char *input = NULL;
    size_t len = 0;

    // interactive loop
    while(true){

        // getting input from the user
        printf("SUShell$ ");
        fflush(stdout);

        ssize_t nread = getline(&input, &len, stdin);

        if (nread == -1){
            break;
        }
    
        // parsing part
        compiledCmd c;
        int cmd = compileCommand(&parser, input, &c);

        if (cmd == 0){
            fprintf(stderr, "Parse error\n");
            freeCompiledCmd(&c);
            continue;
        }

        // shutdown
        if(c.isQuit){
            printf("Exiting shell...\n");
            freeCompiledCmd(&c);
            break;
        }

        // execution
        executeCmd(c.root, &c);

        freeCompiledCmd(&c);
    }

    // cleanup and resourse managment   
    free(input);
    freeParser(&parser);
    return 0;
}