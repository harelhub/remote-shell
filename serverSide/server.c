#include "server.h"



// GLOBAL VARIABLES
static int pipee[2];  // a pipe between the server process and the background process
static int serverSocket;
static int privateSocket;
static int pipe_to_shell[2];
static int pipe_from_shell[2];




// "function_loop" function is a function which tries to do write/read operation for 3 attempts and exits if fails. This code is needed in several places in the program, sometimes for write operation and sometimes for read operations, so this general function covers both cases in one function.
void function_loop(operation op, int fd, char * str, int constant, pid_t shell_pid) {
    int itr = 1;
    int op_success = 0;

    while (1) {
        if (itr == 4) {
            fprintf(stderr, "There is either a problem with the client or connectivity issue with server. Client_handler_process is dead...\n");  // the problem is apparently not a system failure to fulfill the operation since we had 3 attempts with time space in between. It is either server connectivity (internet_disconnection cause main_server_process to halt but not the client_handler_process which then may fail write/read socket) or problem with client (if there is no system failure or server connectivity issue).
            kill(shell_pid, SIGKILL);
            exit(1); //cleanup is executed
        }

        switch (op) {
            case WRITE:
                if(write(fd, str, constant) != -1)
                    op_success = 1;
            break;
            case READ:
                if(read(fd, str, constant) > 0)
                    op_success = 1;
            break;
        }
        if (op_success)
            break;

        // we get here if the operation has failed
        sleep(5);
        itr++;
    }
}

// "no_connectivity_handler" function is what the server process does when it gets notified that it lost connection to internet (it halts its run and prints a message to user. When internet is back, a message is printed to user again)
void no_connectivity_handler() {
    char str[13]; //str variable is for reading "can continue" from background process when internet is back
    fprintf(stderr, "Please connect to internet.\n");
    while (1) {
        if(read(pipee[0], str, 13) <=0)
            continue;
        break;
    }
    if (strcmp(str, "can continue") == 0) {
        printf("Great, there is internet connection right now.\n");
        return;
    }
}

// "close_pipee" function is a cleanup routine before exit of the background process which monitors network connection
void close_pipee() {
    close(pipee[1]);
    exit(0);
}

// "cleanup_main" function is a cleanup routine before exit of the server process
void cleanup_main() {
    close(serverSocket);
    close(privateSocket);
    close(pipee[0]);
}

// "cleanup_client_handler" function is a cleanup routine before exit of the client handler process
void cleanup_client_handler() {
    close(privateSocket);
    close(pipe_to_shell[1]);
    close(pipe_from_shell[0]);
}

// "some_configurations_piece1" function is just a collection of technical operations (done by "main" function) exported to an external function for code readability
void some_configurations_piece1() {
    atexit(cleanup_main);
    signal(SIGUSR1, no_connectivity_handler);
    pipe(pipee);
}

// "open_server_for_clients" function gets the server ready for service clients by opening a listening socket binded to a constant ip address
void open_server_for_clients(struct sockaddr_in address, int addressLength) {
    if((serverSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "socket creation failed\n");
        exit(1);
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = inet_addr(HOST);

    if(bind(serverSocket, (struct sockaddr *) &address, addressLength) == -1) {
        printf("binding socket failed\n");
        exit(1);
    }

    if(listen(serverSocket, BACKLOG) == -1) {
        printf("listening to socket failed\n");
        exit(1);
    }
}

// "background_process_function" function is a backgroud process which constantly monitors internet connectivity
void background_process_function() {
    int internet_flag = 1;

    signal(SIGTERM, close_pipee);
    close(serverSocket);
    close(pipee[0]);

    while (1) {
        // Ping Google's DNS server (8.8.8.8) once
        int response = system("ping -c 1 8.8.8.8 > /dev/null 2>&1");  // system is doing a ping to Google DNS server in order to check connectivity, but the output printing isn't needed for us so it printed to /dev/null which is a "garbage" virtual output device
        if (response != 0) {
            // if we get here, there is no connectivity to internet
            internet_flag = 0;
            kill(getppid(), SIGUSR1);
            sleep(30);
            continue;
        }
        if(!internet_flag)
            // we get here if there is a transition from being internet disconnected to being internet connected (internet_flag is going to change from 0 to 1)
            write(pipee[1], "can continue", strlen("can continue"));
        internet_flag = 1;
        sleep(1);
    }
}

// "shell_process_function" function is a process which executes "Bash"
void shell_process_function() {
    close(privateSocket);
    close(pipe_to_shell[1]);
    close(pipe_from_shell[0]);
    // shell is going to read input from pipe_to_shell[0]
    dup2(pipe_to_shell[0], STDIN_FILENO);
    // shell is going to write output to pipe_from_shell[1]
    dup2(pipe_from_shell[1], STDOUT_FILENO);
    dup2(pipe_from_shell[1], STDERR_FILENO);
    execl("/bin/sh", "sh", (char *) NULL);
    // if execl fails:
    fprintf(stderr, "failed opening shell\n");
    close(pipe_to_shell[0]);
    close(pipe_from_shell[1]);
    exit(1);
}

// "some_configurations_piece2" function is just a collection of technical operations (done by "client_handler_process_function" function) exported to an external function for code readability
void some_configurations_piece2() {
    close(serverSocket);
    close(pipee[0]);
    atexit(cleanup_client_handler);
    pipe(pipe_to_shell);
    pipe(pipe_from_shell);
}

// "no_input_from_client_or_fail_to_check_input" function is what the client handler process does in case it fails to check if there is a command inmput from the client or in case there is no command input from the client for 5 minutes
void no_input_from_client_or_fail_to_check_input(int select_result, pid_t shell_pid) {
    if(select_result == -1)
        fprintf(stderr, "failed selecting\n");
    else
        fprintf(stderr, "client isn't active for 5 minutes... goodbye.\n");
    kill(shell_pid, SIGKILL);
    exit(1);
}

// "client_handler_process_function" function is the client handler process which handles privately a client. It creates a Bash process and then serves as a mediator between the client and the Bash. It transfers to the Bash a command from the client and sends back to the client the output from the Bash (if there is an output).
void client_handler_process_function(){
    some_configurations_piece2();
    pid_t shell_pid = fork();

    if(shell_pid == 0) {
        // shell process
        shell_process_function();
    }

    // client handler process
    char input[MAX_COMM_LEN];
    char output[MAX_OUTPUT_LEN];
    struct timeval timeout;
    fd_set readfds;
    int select_result;

    close(pipe_to_shell[0]);
    close(pipe_from_shell[1]);

    while(1) {
        timeout.tv_sec = 300;
        timeout.tv_usec = 0;
        FD_ZERO(&readfds);
        FD_SET(privateSocket, &readfds);
        // Wait for data from client to be available for reading (up to 300 seconds)
        select_result = select(privateSocket + 1, &readfds, NULL, NULL, &timeout);

        if(select_result <= 0) {
            no_input_from_client_or_fail_to_check_input(select_result, shell_pid);
        }

        // if we got here, there is input from client
        // we read it
        function_loop(READ, privateSocket, input, MAX_COMM_LEN - 1, shell_pid);
        // we send it to the shell
        if(write(pipe_to_shell[1], input, strcspn(input, "\n") + 1) == -1) {
            kill(shell_pid, SIGKILL);
            exit(1);
        }


        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        FD_ZERO(&readfds);
        FD_SET(pipe_from_shell[0], &readfds);
        // Wait for data from shell to be available for reading (up to 5 seconds)
        select_result = select(pipe_from_shell[0] + 1, &readfds, NULL, NULL, &timeout);

        if(select_result == -1) {
            // select returns -1 if select function fails to be executed
            fprintf(stderr, "failed selecting\n");
            kill(shell_pid, SIGKILL);
            exit(1);
        }
        else {
            if(select_result == 0) {
                // select returns 0 if the timeout has expired and select didn't notice any data ready for reading. Therefore, select returning 0 actually means we should not commit "read()" on the socket since it may be stuck.
                continue;
            }
            else {
                // select returns a positive value if there is data ready for reading in the socket or that the socket is invalid. Therefore, select returning a positive value actually means we can commit "read()" on the socket since in both cases the read will not be stuck (but will return different values).
                memset(output, 0, sizeof(output));
                // read the output from shell
                if(read(pipe_from_shell[0], output, MAX_OUTPUT_LEN - 1) <= 0) {
                    kill(shell_pid, SIGKILL);
                    exit(1);
                }
                // send it to client
                function_loop(WRITE, privateSocket, output, strlen(output), shell_pid);
            }
        }
    }
}



// "main" function is the main function in the program
int main(void) {

    struct sockaddr_in address = {0};
    int addressLength = sizeof(address);
    pid_t pidChild;

    some_configurations_piece1();

    printf("server is booting...\n");

    pid_t background_pid = fork();

    if(background_pid == 0) {
        // background process which constantly checks internet connectivity
        background_process_function();
    }

    // main server process

    open_server_for_clients(address, addressLength);

    close(pipee[1]);

    printf("server is ready for clients\n");

    while(1) {
        // accept a client
        if((privateSocket = accept(serverSocket, (struct sockaddr *) &address, &addressLength)) == -1) {
            fprintf(stderr, "failed accepting a client connection request\n");
            continue;
        }

        pidChild = fork();

        if(pidChild == 0)
            // client-handler process
            client_handler_process_function();

        else if(pidChild > 0)
            // server process
            close(privateSocket);

        }
    }
