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
#define MAX_BYTES (1 << 20)    // 1 MB 

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

void *thread_func(void *NewSocket){
    sem_wait(&semaphore);
    int sem_value;
    sem_getvalue(&semaphore, sem_value);
    printf("Semaphore value is %d\n", sem_value);

    int socket = *(int *)NewSocket;
    int bytes_client_sends, len;

    char *buffer = (char *)calloc(MAX_BYTES, sizeof(char));
    bzero(buffer, MAX_BYTES);
    bytes_client_sends = recv(socket, buffer, MAX_BYTES, 0);

    while(bytes_client_sends > 0){
        len = strlen(buffer);
        if(strstr(buffer, "\r\n\r\n") == NULL){
            bytes_client_sends = recv(socket, buffer + len, MAX_BYTES - len, 0);
        }
        else{
            break;
        }
    }
 
    char *tempReq = (char *)malloc(sizeof(buffer)*sizeof(char) + 1);
    for(int i = 0; i < strlen(buffer); i ++){
        tempReq[i] = buffer[i];
    }
  
    struct cache_element *temp = find(tempReq);

    if(temp != NULL){
         // req is present in our Cache
         int size = temp->len / sizeof(char);
         int idx = 0;
         char response[MAX_BYTES];
         while(idx < size){
            for(int i = 0; i < MAX_BYTES; i ++){
                response[i] = temp->data[i];
                idx ++;
            }
            send(socket, response, MAX_BYTES, 0);
         }
         printf("Response from the LRU Cache\n");
         printf("Data : %s\n", response);
    }
    else if(bytes_client_sends > 0){
        // find from the targeted server
        int len = strlen(buffer);
        ParsedRequest *request = ParsedRequest_create();

        if(ParsedRequest_parse(request, buffer, len) < 0){
            printf("Parsing failed\n");
        }
        else{
            bzero(buffer, MAX_BYTES);
            if(!strcmp(request->method,"GET")){
               if(request->host && request->path && checkHTTPversion(request->version) == 1){
                  bytes_client_sends = handle_request(socket, request, tempReq);
                  if(bytes_client_sends == -1){
                     sendErrMsg(socket, 500);
                  }
               }
               else{
                  sendErrMsg(socket, 500)
               }
            }
            else {
                print("Supports GET method only\n");
            }
        }
        ParsedRequest_destroy(request);
    }
    else if(bytes_client_sends == 0){
        printf("Client gone down !");
    }
    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);
    free(tempReq);
    sem_post(&semaphore);
    sem_getvalue(&semaphore, sem_value);
    printf("Post semaphore value %d\n", sem_value);
    
    return NULL;
}

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
    server_addr.sin_addr.s_addr = INADDR_ANY;    

     /* When binding a socket to an IP address, setting sin_addr.s_addr to INADDR_ANY tells the 
     operating system to listen for incoming connections on any and all network interfaces on the host machine.  */


    if(bind(proxy_socketId, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
        perror("Port is unavailable");
        exit(1);
    }

    printf("Binding on port %d\n", port_number);

    int listen_status = listen(proxy_socketId, MAX_CLIENTS);
    if(listen_status < 0){
        perror("Error in listening");
        exit(1);
    }

    int connected_socketId[MAX_CLIENTS];
    int i = 0;
    
    while(1){
        bzero((char *)&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr);
        client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_addr, (socklen_t)&client_len);

        /* accept() is responsible for accepting
         incoming client connections and creating a new socket (clientSocket) for communication */

        if(client_socketId < 0){
            perror("Not able to connect");
            exit(1);
        }

        connected_socketId[i] = client_socketId;
        
        struct sockaddr_in *client_ptr = &client_addr;
        struct in_addr ip_addr = client_ptr->sin_addr;
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);

        printf("Client connected at port %d with ip address: %s\n", ntohs(client_addr.sin_port), str);

        pthread_create(&tid[i], NULL, thread_func, &connected_socketId[i]);
        i ++;
             
    }
    close(proxy_socketId);
    return 0;
}

