#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>



// MACROS
#define PORT 9999
#define HOST "192.168.83.133"
#define BACKLOG 3
#define MAX_COMM_LEN 1000
#define MAX_OUTPUT_LEN 10000



// TYPES DEFINITIONS:
typedef enum{WRITE, READ} operation;