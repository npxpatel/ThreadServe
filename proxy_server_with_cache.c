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
#define MAX_CACHE_SIZE (10 * 1024 * 1024)  // 10 MB

struct cache_element{
    char *data;
    int len;
    char *url;
    time_t lru_time;   // will remove when converting to hashmap + DLC
    struct cache_element *next;
};

typedef struct cache_element cache_element;
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
int cache_size = 0;


cache_element *find(char *url){
    cache_element *site = NULL;
    int lock_value = pthread_mutex_lock(&lock);
    printf("Lock accquired value is %d\n", lock_value);
         /* although it will prohibit another thread to even read also, we can use (Read-Write Locks ) */

    site = head;
       while(site){
           if(strcmp(site->url, url) == 0){
                printf("LRU Time track before access %d\n Url found!\n", site->lru_time);
                site->lru_time = time(NULL);
                printf("LRU Time track after access %d\n", site->lru_time);
                pthread_mutex_unlock(&lock);
                printf("Unlocked\n");
             
                return site;
          }
          else{
                site = site->next;
              }
        }         
    pthread_mutex_unlock(&lock);
    printf("Unlocked\n");
    return NULL;
}

int add_cache_element(char *data, char *url, int len){
    int lock_value = pthread_mutex_lock(&lock);
    printf("Lock accquired value is %d\n", lock_value);
    
    while(cache_size + len > MAX_CACHE_SIZE){
        remove_cache_element();
    }
    
    int element_size = len + strlen(url) + sizeof(cache_element) + len;
    cache_element *new_element = (cache_element *)malloc(sizeof(cache_element));
    new_element->data = (char *)malloc(len + 1);
    strcpy(new_element->data, data);
    new_element->url = (char *)malloc(strlen(url) * sizeof(char));
    strcpy(new_element->url, url);
    new_element->lru_time = time(NULL);

    new_element->next = head;
    head = new_element;
    cache_size += element_size;
    
    lock_value = pthread_mutex_unlock(&lock);
    printf("Unlocked");

    return 1;
}

void 

int checkHTTPversion(char *msg)
{
	int version = -1;
	if(strncmp(msg, "HTTP/1.1", 8) == 0)
	{
		version = 1;
	}
	else if(strncmp(msg, "HTTP/1.0", 8) == 0)			
	{
		version = 1;										
	}
	else
		version = -1;

	return version;
}

int connectRemoteServer(char *host_addr, int port){
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(remoteSocket < 0){
        printf("Err in creating remoteSocket");
        return -1;
    }
    
    struct hostent *host = gethostbyname(host_addr);     // resolving the IP from name
    if(!host){
        printf("No such host exits");
    }

    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = port;

    bcopy(host->h_addr_list[0], (char *)&server_addr.sin_addr.s_addr, host->h_length);   //copt the resolved Ip
    if(connect(remoteSocket, (char *)&server_addr, sizeof(server_addr)) < 0){
        fprintf(stderr, "Error in connection\n");
        return -1;
    }

    return remoteSocket;
}

int handle_request(int clientSocketId, ParsedRequest *request, char *tempReq){
    char *buff = (char *)calloc(MAX_BYTES, sizeof(char));
    strcpy(buff, "GET");
    strcat(buff, request->path);
    strcat(buff, " ");
    strcat(buff, request->version);
    strcat(buff, "\r\n");          //http request ends

    int len = sizeof(buff);
    if(ParsedHeader_set(request, "Connection", "close") < 0){
        printf("Failed at parsed header key setting");
    }

    if(ParsedHeader_set(request, "Host") == NULL){
        if(ParsedHeader_set(request, "Host", request->host) < 0){
            printf("Failed at parsed header key setting");
        }
    }

    if(ParsedRequest_unparse_headers(request, buff + len, MAX_BYTES - len) < 0){
        printf("Unparse failed!");
    }

    int server_port = 80;    // our END SERVER
    if(request->port){
        server_port = atoi(request->port);
    }
    
    int remoteServerId = connectRemoteServer(request->host, server_port);
    
    if(remoteServerId < 0){
        return -1;
    }

    int bytes_send = send(remoteServerId, buff, strlen(buff), 0);
    bzero(buff, MAX_BYTES);

    bytes_send = recv(remoteServerId, buff, MAX_BYTES - 1, 0);
    char *temp_buffer = (char *)malloc(MAX_BYTES * sizeof(char));
    
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_idx = 0;
    while(bytes_send > 0){
        bytes_send = send(clientSocketId, buff, bytes_send, 0);
        for(int i = 0; i < bytes_send / sizeof(char); i ++){
            temp_buffer[temp_buffer_idx] = buff[i];
            temp_buffer_idx ++;
        }
        temp_buffer_size += MAX_BYTES;
        temp_buffer = (char *)realloc(temp_buffer, temp_buffer_size);
        if(bytes_send < 0){
            perror("Error in sending data to client \n");
            break;
        }
        bzero(buff, MAX_BYTES);
        bytes_send = recv(remoteServerId, buff, MAX_BYTES - 1, 0);
    }

    // temp_buffer to stote the data in Cache for later requests
    temp_buffer[temp_buffer_idx] = '\0';
    add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
    free(buff);
    free(temp_buffer);
    close(remoteServerId);
    
    return 0;
}

void *thread_func(void *NewSocket){
    sem_wait(&semaphore);
    int sem_value;
    sem_getvalue(&semaphore, sem_value);
    printf("Semaphore value is %d\n", sem_value);

    int socket = *(int *)NewSocket;
    int bytes_client_sends, len;

    char *buffer = (char *)calloc(MAX_BYTES, sizeof(char));
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
                     char str[1024];
                     str[0] = -1;
                     printf("500 Internal Server Error\n");
                     send(socket, str, strlen(str), 0);
                  }
               }
               else{
                  char str[1024];
                  str[0] = -1;
                  printf("500 Internal Server Error\n");
                  send(socket, str , strlen(str), 0)
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

