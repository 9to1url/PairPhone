#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
//runtime
int tx(int job, int sock, struct sockaddr_in server_addr);