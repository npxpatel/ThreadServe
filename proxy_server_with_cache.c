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
#include <semaphore.h>
#include <time.h>
#include <sys/time.h>


#define MAX_CLIENTS 40
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

    if (lock_value != 0) {
       printf("Mutex lock failed: %d\n", lock_value);
       return NULL;
    }
 
    printf("Lock accquired value is %d\n", lock_value);
         /* although it will prohibit another thread to even read also, we can use (Read-Write Locks ) */

    site = head;
       while(site){
           if(strcmp(site->url, url) == 0){
                printf("LRU Time track before access %ld,  Url found!\n", site->lru_time);
                site->lru_time = time(NULL);
                printf("LRU Time track after access %ld\n", site->lru_time);
                pthread_mutex_unlock(&lock);
                printf("Unlocked\n");
             
                return site;
          }
          else{
                site = site->next;
              }
        }         
    pthread_mutex_unlock(&lock);
    printf("Url not found\n\n");
    printf("Unlocked\n");
    return NULL;
}

int add_cache_element(char *data, char *url, int len){
    int lock_value = pthread_mutex_lock(&lock);
    printf("Lock accquired value is %d\n", lock_value);
    
    while(cache_size + len > MAX_CACHE_SIZE){
        remove_cache_element();
    }
    
    int element_size = 1 + len + strlen(url) + sizeof(cache_element);
    cache_element *new_element = (cache_element *)malloc(sizeof(cache_element));
    new_element->data = (char *)malloc(len + 1);
    strcpy(new_element->data, data);
    new_element->url = (char*)malloc(1+( strlen( url )*sizeof(char)  ));
    strcpy(new_element->url, url);
    new_element->lru_time = time(NULL);

    new_element->next = head;
    head = new_element;
    cache_size += element_size;
    
    lock_value = pthread_mutex_unlock(&lock);
    printf("Unlocked");

    return 1;
}

void remove_cache_element(){
    int lock_value = pthread_mutex_lock(&lock);
    printf("Lock accquired and the value is %d\n", lock_value);

    if (!head) {
        pthread_mutex_unlock(&lock);
        return;
    }

    cache_element *temp = head;
    cache_element *old = head;
    while(temp != NULL){
        if(temp->lru_time < old->lru_time){
            old = temp;
        }
        temp = temp->next;
    }

    if(old == head){
        head = head->next;
        pthread_mutex_unlock(&lock);
        return ;
    }

    temp = head;
    while(temp->next != old){
        temp = temp->next;
    }
    temp->next = temp->next->next;

    cache_size -= (old -> len) + sizeof(cache_element) + strlen(old -> url) + 1; 
    free(old->data);
    free(old->url);
    pthread_mutex_unlock(&lock);
    
}

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
         perror("Error in creating remoteSocket");
        return -1;
    }

    struct hostent *host = gethostbyname(host_addr);
    if (host == NULL) {
        fprintf(stderr, "Error resolving hostname %s: %s\n", host_addr, strerror(errno));
        return -1;
    }

    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // Copy the resolved IP address
   bcopy((char *)host->h_addr_list[0], (char *)&server_addr.sin_addr.s_addr, host->h_length);


    // Set a timeout for the connection
    struct timeval timeout;
    timeout.tv_sec = 5;  // 5 seconds timeout
    timeout.tv_usec = 0;

    if (setsockopt(remoteSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
        perror("Error setting socket timeout");
        close(remoteSocket);
        return -1;
    }

    // Attempt connection
    printf("Connecting to %s:%d...\n", host_addr, port);
    if (connect(remoteSocket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("Error in connection");
        close(remoteSocket);
        return -1;
    }
    printf("206\n");

    printf("Connected to %s:%d\n", host_addr, port);
    return remoteSocket;
}

int handle_request(int clientSocket,struct ParsedRequest *request, char *tempReq)
{
	char *buf = (char*)malloc(sizeof(char)*MAX_BYTES);
	strcpy(buf, "GET ");
	strcat(buf, request->path);
	strcat(buf, " ");
	strcat(buf, request->version);
	strcat(buf, "\r\n");

	size_t len = strlen(buf);

	if (ParsedHeader_set(request, "Connection", "close") < 0){
		printf("set header key not work\n");
	}

	if(ParsedHeader_get(request, "Host") == NULL)
	{
		if(ParsedHeader_set(request, "Host", request->host) < 0){
			printf("Set \"Host\" header key not working\n");
		}
	}

	if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0) {
		printf("unparse failed\n");
	}

	int server_port = 80;				// Remote Server Port
	if(request->port != NULL)
		server_port = atoi(request->port);

	int remoteSocketID = connectRemoteServer(request->host, server_port);

	if(remoteSocketID < 0)
		return -1;

	int bytes_send = send(remoteSocketID, buf, strlen(buf), 0);

	bzero(buf, MAX_BYTES);

	bytes_send = recv(remoteSocketID, buf, MAX_BYTES-1, 0);
	char *temp_buffer = (char*)malloc(sizeof(char)*MAX_BYTES); 
	int temp_buffer_size = MAX_BYTES;
	int temp_buffer_index = 0;

	while(bytes_send > 0)
	{
		bytes_send = send(clientSocket, buf, bytes_send, 0);
		
		for(size_t i=0;i<bytes_send/sizeof(char);i++){
			temp_buffer[temp_buffer_index] = buf[i];
			temp_buffer_index++;
		}
		temp_buffer_size += MAX_BYTES;
		temp_buffer=(char*)realloc(temp_buffer,temp_buffer_size);

		if(bytes_send < 0)
		{
			perror("Error in sending data to client socket.\n");
			break;
		}
		bzero(buf, MAX_BYTES);

		bytes_send = recv(remoteSocketID, buf, MAX_BYTES-1, 0);

	} 
	temp_buffer[temp_buffer_index]='\0';
	free(buf);
	add_cache_element(temp_buffer, tempReq, strlen(temp_buffer));
	printf("Done\n");
	free(temp_buffer);
	
	
 	close(remoteSocketID);
	return 0;
}

void *thread_func(void *NewSocket){
    sem_wait(&semaphore);
    int sem_value;
    sem_getvalue(&semaphore, &sem_value);
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
    
    char *tempReq = (char *)malloc(strlen(buffer)*sizeof(char) + 1);
    if (tempReq != NULL) {
        strcpy(tempReq, buffer);
    } else {
      
          printf("Memory allocation failed.\n");
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
        len = strlen(buffer);
        struct ParsedRequest *request = ParsedRequest_create();

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
                  send(socket, str , strlen(str), 0);
               }
            }
            else {
                printf("Supports GET method only\n");
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
    sem_getvalue(&semaphore, &sem_value);
    printf("Post semaphore value %d\n", sem_value);

    return NULL;
}

int main(int argc, char *argv[]){
    int client_socketId, client_len;
    struct sockaddr_in server_addr, client_addr;
    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);
    
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
        client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_addr, (socklen_t *)&client_len);

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

