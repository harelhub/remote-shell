#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>



// MACROS:
#define PORT 9999
#define HOST "192.168.83.133"
#define MAX_COMM_LEN 1000
#define MAX_OUTPUT_LEN 10000
#define MAX_PATH_LEN 500



// TYPES DEFINITIONS:
typedef enum{CONNECT, WRITE, READ} operation;