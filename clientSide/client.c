#include "client.h"



// GLOBAL VARIABLES:
static int ClientSocket;
static struct sockaddr_in addr;
static int pipe_child_to_parent[2];
static pid_t child_pid;





// "what_my_ip" function extracts the ip address of the system and puts it into the chars array that it gets as a parameter
void what_my_ip(char * my_ip) {
    FILE * temp_file_for_ip;

    temp_file_for_ip = popen("hostname -I", "r");  // Run "hostname -I" to get IP addresses
    if (!temp_file_for_ip) {
        printf("Error opening internal file!\n");
        my_ip[0] = '\0';
    }
    fgets(my_ip, 16, temp_file_for_ip);
}

// "no_connectivity_handler" function is what the client process does when it gets notified that it lost connection to internet (it halts its run and prints a message to user. When internet is back, a message is printed to user again)
void no_connectivity_handler() {
    char str[13]; //str variable is for reading "can continue" from background process when internet is back
    fprintf(stderr, "\nPlease connect to internet.\n");
    while (1) {
        if(read(pipe_child_to_parent[0], str, 13) <=0)
            continue;
        break;
    }
    if (strcmp(str, "can continue") == 0) {
        printf("Great, Internet is back. You can continue.\n");
        return;
    }
}

// "ip_change_handler" function is what the client process does when it gets notified that its ip address has changed (it creates a new connection to the server by a new socket which is updated with the new ip address)
void ip_change_handler() {
    // closing the old socket and opening a new one related to the new ip address
    close(ClientSocket);
    if((ClientSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "socket creation failed\n");
        exit(1);
    }
    if((connect(ClientSocket, (struct sockaddr *)&addr, sizeof(addr))) == -1) {
        fprintf(stderr, "connection to server failed\n");
        close(ClientSocket);
        exit(1);
    }
}

// "close_pipe_child" function is a cleanup routine before exit of the background process which monitors network connection
void close_pipe_child() {
    close(pipe_child_to_parent[1]);
    exit(0);
}

// "function_loop" function is a function which tries to do connect/write/read operation for 3 attempts and exits if fails. This code is needed in several places in the program, sometimes for connect operation and sometimes for write or read operations, so this general function covers all cases in one function.
void function_loop(operation op, char * str) {
    int itr = 1;
    int op_success = 0;
    int bytes_cnt = 0;

    while (1) {
        if (itr == 4) {
            fprintf(stderr, "There is a problem with the server. Bye Bye...\n"); // the problem is apparently not a client connectivity or a system failure to fulfill the operation since we had 3 attempts with time space in between (if the problem was client connectivity, them we would already get into the "no_connectivity_handler". A system failure to fulfill the operation is too less realistic since we had 3 attempts with time space in between).
            exit(1);
        }

        switch (op) {
            case CONNECT:
                if((connect(ClientSocket, (struct sockaddr *)&addr, sizeof(addr))) != -1)
                    op_success = 1;
                break;
            case WRITE:
                if(write(ClientSocket, str, strcspn(str, "\n") + 1) != -1) // we write also the '\n' char which is in the end since the shell should get '\n' as a terminator char.
                    op_success = 1;
                break;
            case READ:
                if((bytes_cnt = read(ClientSocket, str, MAX_OUTPUT_LEN - 1)) > 0) {
                    str[bytes_cnt] = '\0';
                    op_success = 1;
                }
                break;
        }
        if (op_success)
            break;

        // we get here if the operation has failed
        printf("PLease wait, program is thinking...\n");
        sleep(5);
        itr++;
    }
}

// "cleanup" function is a cleanup routine before exit of the client process
void cleanup() {
    close(ClientSocket);
    close(pipe_child_to_parent[0]);
    kill(child_pid, SIGTERM);
}

// "child_process_function" function is a backgroud process which constantly monitors internet connectivity and ip address change
void child_process_function() {
    char current_ip[16];
    char new_ip[16];
    int internet_flag = 1;

    close(pipe_child_to_parent[0]);
    signal(SIGTERM, close_pipe_child);

    current_ip[0] = '\0';

    while (1) {
        // Ping Google's DNS server (8.8.8.8) once
        int response = system("ping -c 1 8.8.8.8 > /dev/null 2>&1"); // system is doing a ping to Google DNS server in order to check connectivity, but the output printing isn't needed for us so it printed to /dev/null which is a "garbage" virtual output device
        if (response != 0) {
            // if we get here, there is no connectivity to internet
            internet_flag = 0;
            kill(getppid(), SIGUSR1);
            sleep(30);
            continue;
        }
        if(!internet_flag)
            // we get here if there is a transition from being internet disconnected to being internet connected (internet_flag is going to change from 0 to 1)
            write(pipe_child_to_parent[1], "can continue", strlen("can continue"));
        internet_flag = 1;

        // if get here then there is internet connectivity. Let's now check if ip address has changed.
        what_my_ip(new_ip);
        if(strcmp(new_ip, current_ip) != 0) {
            if(strcmp(current_ip, "") != 0)  // which means a real change in ip, excluding the first time in which current_ip turns from "" into a real (first) ip address
                kill(getppid(), SIGUSR2);
            strcpy(current_ip, new_ip);
        }
        sleep(1);
    }
}

// "configurations_for_process_piece1" function is just a collection of technical operations (done by "main" function) exported to an external function for code readability
void configurations_for_process_piece1() {
    close(pipe_child_to_parent[1]);
    signal(SIGUSR1, no_connectivity_handler);
    signal(SIGUSR2, ip_change_handler);
    atexit(cleanup);
}

// "connection_to_server" function opens a socket and connects to the server
void connection_to_server() {
    if((ClientSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "socket creation failed\n");
        exit(1);
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(HOST);
    addr.sin_port = htons(PORT);

    function_loop(CONNECT, "");
}

// "main_loop" function is the main routine of the client process which takes a command from the user, sends it to the server, gets back an output and prints it.
void main_loop() {
    char command[MAX_COMM_LEN];
    char output[MAX_OUTPUT_LEN];
    struct timeval timeout;
    fd_set readfds;
    int select_result;
    char cwd[MAX_PATH_LEN]; // current working directory of remote shell


    printf("Remote shell is opening now. Press [q] to quit.\n\n");
    while(1) {
        // let's first extract the shell's current path for printing it as a prompt
        function_loop(WRITE, "pwd\n");
        function_loop(READ, cwd);
        cwd[strcspn(cwd, "\n")] = '\0';
        printf("%s>> ", cwd);

        // getting command from the user
        fgets(command, MAX_COMM_LEN - 1, stdin);
        if(strcmp(command, "q\n") == 0) {
            exit(0);
        }

        // sending the command for executing to the server
        function_loop(WRITE, command);

        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        FD_ZERO(&readfds);
        FD_SET(ClientSocket, &readfds);
        // Wait for data from server to be available for reading (up to 5 seconds)
        select_result = select(ClientSocket + 1, &readfds, NULL, NULL, &timeout);

        if (select_result == -1) {
            // select returns -1 if select function fails to be executed
            printf("select failed");
            exit(1);
        }
        else {
            if(select_result == 0)
                // select returns 0 if the timeout has expired and select didn't notice any data ready for reading. Therefore, select returning 0 actually means we should not commit "read()" on the socket since it may be stuck.
                    continue;
            else {
                // select returns a positive value if there is data ready for reading in the socket or that the socket is invalid. Therefore, select returning a positive value actually means we can commit "read()" on the socket since in both cases the read will not be stuck (but will return different values).
                //reading the output from server and printing it
                function_loop(READ, output);
                fprintf(stdout, "%s", output);
            }
        }
    }
}



// "main" function is the main function in the program
int main(void)
{
    pipe(pipe_child_to_parent);

    child_pid = fork();
    if (child_pid == 0)
        // this is the child process. Its purpose is: 1). check that there is connectivity to internet 2). check if ip address was changed due to network transition
        child_process_function();
    else {
        // parent process - which is actually the client process

        configurations_for_process_piece1();

        connection_to_server();

        main_loop();

    }

    return 0;
}
