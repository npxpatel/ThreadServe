#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define MAX_CLIENTS 5

struct cache_element{
    char *data;
    int len;
    char *url;
    time_t lru_time;   // will remove when converting to hashmap + DLC
    struct cache_element *next;
};

// ( will group all to together in doubly link-list )
struct cache_element *find(char *url);
int add_cache_element(char *data, char *url, int len);
void remove_cache_element();

int port_number = 8080;    //port of proxy server :
int proxy_socketId;

pthread_t tid[MAX_CLIENTS];
sem_t semaphore;
pthread_mutex_t lock;

struct cache_element *head;
int cache_size;

int main(int argc, char *argv[]){
    int client_socketId, client_len;
    struct sockaddr_in server_addr, client_addr;
    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_lock(&lock);
    
    if(argc == 2){
        //for custom port
        port_number = atoi(argv[1]);
    }
    else{
        printf("Specify port number \n");
        exit(1);
    }

    printf("Starting proxy server at %d\n", port_number);

    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);    //ipv4
    if(proxy_socketId < 0){
        perror("Failed to create a socket");
        exit(1);
    }
    int reuse = 1;  // global socket : proxy socket
    if(setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0){
        perror("SetSockOpt Failed");
    }

    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;    // target server for now as any


    if(bind(proxy_socketId, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
        perror("Port is unavailable");
        exit(1);
    }

}

