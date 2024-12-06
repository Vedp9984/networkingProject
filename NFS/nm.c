#include "headers.h"
#include <pthread.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <limits.h>
#include <ctype.h>
#include <fcntl.h>
#define LOG_FILE "nm_log.txt"



void log_message(const char *format, ...) {
    va_list args;

    // Get the current time and format it as a string
    time_t now;
    time(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    // Prepare the log message
    char log_msg[1024];
    snprintf(log_msg, sizeof(log_msg), "[%s] ", time_str);

    // Add the user-defined log message
    va_start(args, format);
    vsnprintf(log_msg + strlen(log_msg), sizeof(log_msg) - strlen(log_msg), format, args);
    va_end(args);

    // Open the log file in append mode
    FILE *log_file = fopen(LOG_FILE, "a");
    if (log_file != NULL) {
        // Write the log message to the file
        fprintf(log_file, "%s\n", log_msg);
        fclose(log_file);  // Close the log file
    } else {
        // If there's an error opening the file, print to the console
        perror("Error opening log file");
    }

    // Print the log message to the console as well
    printf("%s\n", log_msg);
    return;
}



#define MAX_PENDING_LOCKS 20000
#define MAX_PENDING 20000
#define MAX_PATH_LEN 50000
#define MAX_STORAGE_SERVERS 500

char *nm_ip; int ss_fd , ss_fd1;

typedef struct
{
    char function[32];
    char src_path[256];
    char dest_path[256];
    char data[1024];

} Request;

typedef struct
{
    char ip_address[INET_ADDRSTRLEN];
    int client_port;
    int ss_port;
} NamingServerInfo;

typedef struct
{
    char ip[INET_ADDRSTRLEN]; // IP address of the storage server
    int ss_port;              // Storage server port
    int server_index;         // Index of the storage server
} ServerInfo;

typedef struct
{
    char ip[INET_ADDRSTRLEN];
    int ss_port;
    int cl_port;
    int num;
    int extra_ss_port;
    int temp;
    char file_paths[100000];
    char file_path_org[1000];
    
} StorageServerInfo;

int smallest_org = INT_MAX;

StorageServerInfo ss_info[MAX_STORAGE_SERVERS];
int c_ss = 0; // Track the number of storage servers

NamingServerInfo naming_server;
TrieNode *path_trie; // Global trie root for path storage
LRUCache *path_cache;

int yactive_reads = 0;


pthread_mutex_t path_locks[MAX_PENDING_LOCKS];  // Mutex array for each path
pthread_cond_t path_conditions[MAX_PENDING_LOCKS];  // Condition variable array for each path

// Function to initialize the locks and condition variables for all paths
void init_locks_and_conditions() {
    for (int i = 0; i < MAX_PENDING_LOCKS; i++) {
        pthread_mutex_init(&path_locks[i], NULL);  // Initialize mutex
        pthread_cond_init(&path_conditions[i], NULL);  // Initialize condition variable
    }
}

// Function to acquire lock for a specific path
void lock_path(int path_index) {
    pthread_mutex_lock(&path_locks[path_index]);  // Lock corresponding mutex for path
}

// Function to release lock for a specific path
void unlock_path(int path_index) {
    pthread_mutex_unlock(&path_locks[path_index]);  // Unlock the mutex for path
}

// Function to signal other threads waiting on a path
void signal_path(int path_index) {
    pthread_cond_signal(&path_conditions[path_index]);  // Signal condition variable
}

// Function to wait for the signal for a specific path (use when waiting for another thread to finish)
void wait_for_path(int path_index) {
    pthread_cond_wait(&path_conditions[path_index], &path_locks[path_index]);  // Wait for signal from other thread
}





void clean_file_paths(char *file_paths)
{
    char *src = file_paths, *dest = file_paths;
    while (*src)
    {
        if (*src == '\n' || *src == '\t')
        {
            *dest++ = ','; // Replace newline or tab with a comma
            while (*(src + 1) == '\n' || *(src + 1) == '\t')
                src++; // Skip consecutive delimiters
        }
        else
        {
            *dest++ = *src;
        }
        src++;
    }
    *dest = '\0'; // Null-terminate the cleaned string
}

// Function declarations
void *client_listener(void *args);
void *ss_listener(void *args);
void *handle_client_request(void *client_socket);
void *handle_ss_registration(void *ss_socket);
void initialize_naming_server(int client_port, int ss_port);

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s <ip> <Client Port> <Storage Server Port>\n", argv[0]);
        return 1;
    }
    FILE* logFile = fopen("nm_log.txt", "a");
    if (logFile == NULL) {
        perror("Error opening log file");
       // exit(EXIT_FAILURE);
    }
    
    int client_port = atoi(argv[2]);
    int ss_port = atoi(argv[3]);

    nm_ip = strdup(argv[1]);

    initialize_naming_server(client_port, ss_port);

    pthread_t client_listener_thread, ss_listener_thread;

    if (pthread_create(&client_listener_thread, NULL, client_listener, &client_port) != 0)
    {
        perror("Failed to create client listener thread");
        return 1;
    }
    if (pthread_create(&ss_listener_thread, NULL, ss_listener, &ss_port) != 0)
    {
        perror("Failed to create storage server listener thread");
        return 1;
    }

    pthread_join(client_listener_thread, NULL);
    pthread_join(ss_listener_thread, NULL);

    // Free the trie structure
    freeTrie(path_trie);

    return 0;
}

void initialize_naming_server(int client_port, int ss_port)
{
    char hostname[256];
    
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        perror("gethostname failed");
        strcpy(naming_server.ip_address, "Unknown IP");
        return;
    }

    struct addrinfo hints, *info;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    /*if (getaddrinfo(hostname, NULL, &hints, &info) == 0)
    {
        struct sockaddr_in *addr = (struct sockaddr_in *)info->ai_addr;
        inet_ntop(AF_INET, &addr->sin_addr, "10.2.137.212", sizeof("10.2.137.212"));
        freeaddrinfo(info);
    }
    else
    {
        strcpy(naming_server.ip_address, "Unknown IP");
    } */

    naming_server.client_port = client_port;
    naming_server.ss_port = ss_port;

    path_trie = createTrieNode(); // Initialize the trie

    path_cache = createLRUCache(90); 
    

    printf("Naming Server initialized with IP: %s , Client Port: %d, Storage Server Port: %d\n",nm_ip, naming_server.client_port, naming_server.ss_port);

    log_message("Naming Server initialized with IP: %s , Client Port: %d, Storage Server Port: %d\n",nm_ip, naming_server.client_port, naming_server.ss_port);
}


void *client_listener(void *args)
{
    int client_port = *(int *)args;
    int client_sock;
    struct sockaddr_in client_addr;

    client_sock = socket(AF_INET, SOCK_STREAM, 0);
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(client_port);
      if (inet_pton(AF_INET, nm_ip, &client_addr.sin_addr) <= 0)
{
    perror("");
    
}
    bind(client_sock, (struct sockaddr *)&client_addr, sizeof(client_addr));
    listen(client_sock, MAX_PENDING);

    printf("Client listener started.\n");

    while (1)
    {
        int addrlen = sizeof(client_addr);
        int new_client = accept(client_sock, (struct sockaddr *)&client_addr, (socklen_t *)&addrlen);
        if (new_client != -1)
        {
            printf("New client connected from IP: %s \n",
                   inet_ntoa(client_addr.sin_addr));
            pthread_t client_thread;
            if (pthread_create(&client_thread, NULL, handle_client_request, (void *)&new_client) == 0)
            {
                pthread_detach(client_thread); // Detach thread to automatically free resources on exit
            }
            else
            {
                perror("Failed to create client handler thread");
                close(new_client);
            }
        }
        else
        {
            perror("Error accepting client connection");
        }
    }
    close(client_sock);
}

void *ss_listener(void *args)
{
    int ss_port = *(int *)args;
    int ss_sock;
    struct sockaddr_in ss_addr;

    ss_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ss_sock < 0)
    {
        perror("Failed to create socket");
        pthread_exit(NULL);
    }

    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(ss_port);
    if (inet_pton(AF_INET, nm_ip, &ss_addr.sin_addr) <= 0)
{
    perror("");
    
}

    if (bind(ss_sock, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0)
    {
        perror("Failed to bind socket");
        close(ss_sock);
        pthread_exit(NULL);
    }

    if (listen(ss_sock, MAX_PENDING) < 0)
    {
        perror("Failed to listen on socket");
        close(ss_sock);
        pthread_exit(NULL);
    }

    printf("Storage server listener started on port %d.\n", ss_port);

    while (1)
    {
        int ss_addrlen = sizeof(ss_addr);
        int new_ss = accept(ss_sock, (struct sockaddr *)&ss_addr, (socklen_t *)&ss_addrlen);
        if (new_ss != -1)
        {
            printf("New storage server connected from IP: %s \n",
                   inet_ntoa(ss_addr.sin_addr) );
            pthread_t ss_thread;
            pthread_create(&ss_thread, NULL, handle_ss_registration, (void *)&new_ss);
            pthread_detach(ss_thread); // Detach thread to handle multiple clients concurrently
        }
        else
        {
            perror("Error accepting storage server connection");
        }
    }

    close(ss_sock);
    pthread_exit(NULL);
}



void *handle_ss_registration(void *ss_socket)
{
    ss_fd = *(int *)ss_socket;
    StorageServerInfo new_ss_info;

    if (recv(ss_fd, &new_ss_info, sizeof(StorageServerInfo),0) >= 0)
    {              
        if (c_ss < MAX_STORAGE_SERVERS)
        {
            
            clean_file_paths(new_ss_info.file_paths);
           
              if(c_ss>0) { 
              for (int i =0; i<c_ss ; i++){
                if(strcmp(new_ss_info.file_paths,ss_info[i].file_paths)==0){
                    printf("SS %d came back\n",i);
                    goto cc4;
                }
             }
            }
            ss_info[c_ss] = new_ss_info;
            printf("Registered storage server with IP: %s, Port: %d, %d    %s   \n", new_ss_info.ip, new_ss_info.ss_port,new_ss_info.extra_ss_port,new_ss_info.file_path_org);
                           log_message("Registered storage server with IP: %s, Port: %d, %d    %s   \n", new_ss_info.ip, new_ss_info.ss_port,new_ss_info.extra_ss_port,new_ss_info.file_path_org);

               
               clean_file_paths( ss_info[c_ss].file_paths);

            char *path = strtok(new_ss_info.file_paths, ",");
           
            while (path != NULL)
            {   if (strlen(path) < smallest_org) smallest_org = strlen(path);
                insertTrie(path_trie, path, c_ss);
                path = strtok(NULL, ",");
            }

            c_ss++;
            printTrie(path_trie);
        }
    }
 cc4:   
    while (1)
    {
        char buffer[1024];
        int bytes_received = recv(ss_fd, buffer, sizeof(buffer), 0);
        if (bytes_received == 0)
        {
            // Connection lost
            printf("Connection lost with storage server %s\n", new_ss_info.ip);
             log_message("Connection lost with storage server %s\n", new_ss_info.ip);

            // Handle the lost connection, notify, clean up, etc.
            // Optionally, try reconnecting or perform other actions as necessary
            break;
        }
        else if (bytes_received == -1)
        {
            // Error occurred
            perror("Error receiving data from storage server");
            log_message("Connection lost with storage server %s\n", new_ss_info.ip);
            break;
        }
        else
        {
            continue;
        }
    }

    // Cleanup after connection loss or error
    close(ss_fd);
    pthread_exit(NULL);

   // close(ss_f);
}

void change_ss(int index, const char *path, const char *operation)
{
    if (strcmp(operation, "CREATE") == 0)
    {
        // Insert the path into the file_paths array of the storage server info
        strcat(ss_info[index].file_paths, path);
        strcat(ss_info[index].file_paths, ",");  // Add a comma delimiter for multiple paths

        // Insert the path into the trie
        insertTrie(path_trie, path, index); // Insert into the global trie
        printf("Inserted path %s for server index %d\n", path, index);
    }
   else if (strcmp(operation, "DELETE") == 0)
    {
        char *file_paths_copy = strdup(ss_info[index].file_paths);
        if (!file_paths_copy)
        {
            perror("Failed to duplicate file paths");
            return;
        }

        char *new_file_paths = malloc(MAX_PATH_LEN);
        if (!new_file_paths)
        {
            perror("Failed to allocate memory for new file paths");
            free(file_paths_copy);
            return;
        }

        *new_file_paths = '\0'; // Initialize empty string
        char *token = strtok(file_paths_copy, ",");
        while (token)
        {
            if (strstr(token, path) == 0)
            {
                if (*new_file_paths != '\0')
                    strcat(new_file_paths, ",");
                strcat(new_file_paths, token);
            }
            else
            {
                deleteTrie(path_trie, token);
                //printf("D path %s from trie for server index %d\n", token, index);
            }
            token = strtok(NULL, ",");
        }

        strncpy(ss_info[index].file_paths, new_file_paths, MAX_PATH_LEN - 1);
        ss_info[index].file_paths[MAX_PATH_LEN - 1] = '\0';

        free(file_paths_copy);
        free(new_file_paths);
        printf("Updated file paths for server index %d\n", index);
        printTrie(path_trie);
    }
    else
    {
        printf("Invalid operation: %s\n", operation);
    }
}

char *gather_all_paths() {
    // Allocate enough space for all paths. Max size will be (MAX_PATH_LEN * c_ss) + extra space for labels.
    char *all_paths = malloc(MAX_PATH_LEN * c_ss + c_ss * 32);  // Allocate space for labels like "ss_index 0: "
    if (all_paths == NULL) {
        perror("Failed to allocate memory for accessible paths");
        return NULL;
    }

    all_paths[0] = '\0';  // Initialize the string as empty

    // Iterate through all the storage servers and concatenate their paths
    for (int i = 0; i < c_ss; i++) {
        // Add the label for this storage server
        char label[32];
        snprintf(label, sizeof(label), "ss_index %d: ", i);

        // Add the label to the all_paths string
        strcat(all_paths, label);

        // Check if the file_paths for this server is not empty
        if (strlen(ss_info[i].file_paths) > 0) {
            strcat(all_paths, ss_info[i].file_paths);  // Concatenate the file paths for this server
        } else {
            strcat(all_paths, "No paths available.");
        }
        
        strcat(all_paths, "\n");  // Add a newline after each server's paths
    }

    return all_paths;  // Return the dynamically allocated string
}

void send_paths_to_client(int client_fd, char *all_paths) {
    if (all_paths == NULL) {
        send(client_fd, "ERROR: Failed to gather accessible paths", strlen("ERROR: Failed to gather accessible paths"), 0);
        return;
    }

    size_t length = strlen(all_paths);  // Get the length of the string

   // printf("tttt == %s\n",all_paths);

    // Send the length first
    if (send(client_fd, &length, sizeof(length), 0) < 0) {
        send(client_fd, "ERROR: Failed to send length of accessible paths", strlen("ERROR: Failed to send length of accessible paths"), 0);
        return;
    }

    // Now send the actual content
    if (send(client_fd, all_paths, length, 0) < 0) {
        send(client_fd, "ERROR: Failed to send accessible paths", strlen("ERROR: Failed to send accessible paths"), 0);
    }
}

void *handle_client_request(void *client_socket)
{
    int client_fd = *(int *)client_socket;
    char client_command[6501];

    while (1)
    {
        int bytes_received = recv(client_fd, client_command, sizeof(client_command), 0);
        if (bytes_received <= 0)
        {
            if (bytes_received == 0)
            {
                printf("Client disconnected.\n");
                 log_message("Client disconnected.\n");
                

            }
            else
            {
                perror("Client disconnected. () ");
                log_message("Client disconnected.\n");
            }
            break;
        }
int ackn = 1;
        //if(client_command == NULL) continue;
           // send(client_fd, &ackn,sizeof(int),0);
        
        client_command[bytes_received] = '\0'; // Ensure null-terminated string
        
        {
        printf("Received command from client: %s\n", client_command); 
        log_message("Received command from client: %s\n", client_command);  
        }

    


        if(strlen(client_command)!=0 && strlen(client_command)!=1 && strlen(client_command)!=2 ) {

        // Parse operation and path
        char *operation = strtok(client_command, " ");
       // char *path = strtok(NULL, " ");  // Get the full path string (src_path and dest_path)

        if(strcmp(operation,"LIST")==0) goto cc1;
          if(strcmp(operation,"QUIT")==0) goto cc10;

      
         
        // Separate source path, destination path (if any), and the integer
        char *src_path = strtok(NULL, " ");  // First part is the source path

        if (src_path == NULL)
        {
            send(client_fd, "ERROR: Path is required", strlen("ERROR: Path is required"), 0);
             log_message("ENTER PATH\n"); 

            continue;
        }

        char *dest_path = strtok(NULL, " ");  // Second part is the destination path (if any)
        //int path_int = (strtok(NULL, " ") != NULL) ? atoi(strtok(NULL, " ")) : -1;  // The integer part, convert it to int
 
        // Print the parsed values for debugging
        printf("Operation: %s\n", operation);
        printf("Source Path: %s\n", src_path);
        if (dest_path != NULL)
        {
            printf("Destination Path or DATA: %s\n", dest_path);
        }
       // printf("Integer: %d\n", path_int);
       int server_index;

        // Handle the READ, WRITE, STREAM, or GET_INFO operations
         if (strcmp(operation, "READ") == 0)
        {    
            if(scn(path_cache,src_path)==-1) {
                
             server_index = searchTrie(path_trie, src_path);
             insert(path_cache,src_path,server_index);

            }
            else {
            printf("Cache Hit\n");    
            server_index = scn(path_cache,src_path); 
            }

            ServerInfo server_info;

            if (server_index != -1)
            {

                 lock_path(server_index);  // Lock for cache read
                yactive_reads++;
               unlock_path(server_index);
                
                strcpy(server_info.ip, ss_info[server_index].ip);  // Copy IP
                server_info.ss_port = ss_info[server_index].cl_port;  // Copy port
                server_info.server_index = server_index;  // Add server index

                // Send the struct to the client
                if (send(client_fd, &server_info, sizeof(ServerInfo), 0) < 0)
                {
                    send(client_fd, "ERROR: Failed to send server information", strlen("ERROR: Failed to send server information"), 0);
                     lock_path(server_index);  // Lock for cache read
                  yactive_reads--;
                   if(yactive_reads==0) {
                      signal_path(server_index);
                }
               unlock_path(server_index);
                    continue;
                }
                char rep[1024];
                recv(client_fd, rep, sizeof(rep),0);
                printf("recieved ack from client %s\n",rep); int p=-1;
                 log_message("recieved ack from client %s\n",rep);

                // printf("%s received from client for %s",rep,operation);
                 lock_path(server_index);  // Lock for cache read
                  yactive_reads--;
                   if(yactive_reads==0) {
                      signal_path(server_index);
                }
               unlock_path(server_index);

             
                

                 
            }
            else
            {
                 memset(server_info.ip, 0, sizeof(server_info.ip));  // Copy IP
                server_info.ss_port = -1;  // Copy port
                server_info.server_index = -1;  // Add server index

                // Send the struct to the client
                if (send(client_fd, &server_info, sizeof(ServerInfo), 0) < 0)
                {
                   // send(client_fd, "ERROR: Failed to send server information", strlen("ERROR: Failed to send server information"), 0);
                    continue;
                }
                 char rep[256];
               
                recv(client_fd, rep, sizeof(rep),0);
                //printf("recieved ack from client %s\n",rep); 
                 printf("%s received from client for %s",rep,operation);
                  log_message("%s received from client for %s",rep,operation);
            }

        }
     else   if ( strcmp(operation, "WRITE") == 0 || strcmp(operation, "STREAM") == 0 || strcmp(operation, "GET_INFO") == 0)
        {    
            if(scn(path_cache,src_path)==-1) {
                
             server_index = searchTrie(path_trie, src_path);
             insert(path_cache,src_path,server_index);

            }
            else {
            printf("Cache Hit\n");    
             log_message("Cache Hit\n");  
            server_index = scn(path_cache,src_path); 
            }

            ServerInfo server_info;

            if (server_index != -1)
            {
               
                while(yactive_reads > 0) {
                    wait_for_path(server_index);
                } 
                 lock_path(server_index);
                strcpy(server_info.ip, ss_info[server_index].ip);  // Copy IP
                server_info.ss_port = ss_info[server_index].cl_port;  // Copy port
                server_info.server_index = server_index;  // Add server index

                // Send the struct to the client
                if (send(client_fd, &server_info, sizeof(ServerInfo), 0) < 0)
                {      
                    //send(client_fd, "ERROR: Failed to send server information", strlen("ERROR: Failed to send server information"), 0);
                    unlock_path(server_index);
                    continue;
                }
  if(strcmp(operation, "STREAM") == 0 ) unlock_path(server_index);

                if(strcmp(operation, "STREAM") != 0 ) {

                char rep[1024];
                unlock_path(server_index);
                recv(client_fd, rep, sizeof(rep),0); int p=-1;
                //printf("recieved ack from client %s\n",rep); 
                 printf("%s received from client for %s",rep,operation);
                   log_message("%s received from client for %s",rep,operation);

                  if (strcmp(rep, "Asynchronously writing data to file") ==0 ) {
                    
                  //printf("async\n");
                    ss_fd1 = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in ss_addr;
                ss_addr.sin_family = AF_INET;
                ss_addr.sin_port =  htons(8080);
                if (inet_pton(AF_INET, ss_info[server_index].ip, &ss_addr.sin_addr) <= 0)
{
    perror("");
    
}
 if (connect(ss_fd1, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) == 0) {
   //  printf("async11\n");
                if(recv(ss_fd1,&p,sizeof(int),0)<0) {
                     printf("async22\n");
                      send(client_fd, "Asynch not done fully\n", sizeof( "Asynch not done fully\n"),0);
                }

            if(p==0) {send(client_fd, "Asynch DONE fully\n", sizeof("Asynch  done fully\n"),0);
            log_message("Asynch DONE fully\n");
             }

            else {send(client_fd, "Asynch not done fully\n", sizeof( "Asynch not done fully\n"),0);
            log_message("Asynch not done fully\n");
            }
             close(ss_fd1);
 }
                  
  close(ss_fd1);
                  
 
            }
                }

         }
            else
            {
                 memset(server_info.ip, 0, sizeof(server_info.ip));  // Copy IP
                server_info.ss_port = -1;  // Copy port
                server_info.server_index = -1;  // Add server index

                // Send the struct to the client
                if (send(client_fd, &server_info, sizeof(ServerInfo), 0) < 0)
                {  
                    
                    //send(client_fd, "ERROR: Failed to send server information", strlen("ERROR: Failed to send server information"), 0);
                    continue;
                }
                 char rep[1024];
               
                recv(client_fd, rep, sizeof(rep),0);
                //printf("recieved ack from client %s\n",rep); 
                 printf("%s received from client for %s",rep,operation);
                  log_message("%s received from client for %s",rep,operation);
            }

        }

  else if (strcmp(operation, "DELETE") == 0)
        {

                
           
           if(scn(path_cache,src_path)==-1) {
                
             server_index = searchTrie(path_trie, src_path);
               if (server_index != -1)
             insert(path_cache,src_path,server_index);

            }
            else {
            printf("Cache Hit\n");    
            server_index = scn(path_cache,src_path); 
            }
            ServerInfo server_info;
            

            if (server_index != -1)

            { 
                
                 while(yactive_reads > 0) {
                    wait_for_path(server_index);
                }
                 lock_path(server_index);
                //printf("%s\n",ss_info[server_index].file_path_org);
                 if( (strlen(src_path)) <  smallest_org )  {
                     printf("risk\n");
                     log_message("ERROR: Path not found\n");
                    send(client_fd, "ERROR: Path not found", strlen("ERROR: Path not found"), 0);
                     unlock_path(server_index);
                   
                    continue;
                 }


                // Connect to the corresponding storage server (SS)
                strncpy(server_info.ip, ss_info[server_index].ip, INET_ADDRSTRLEN);
                server_info.ss_port = ss_info[server_index].cl_port;
                server_info.server_index = server_index;

                ss_fd1 = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in ss_addr;
                ss_addr.sin_family = AF_INET;
                ss_addr.sin_port =  htons(ss_info[server_index].extra_ss_port);
                if (inet_pton(AF_INET, ss_info[server_index].ip, &ss_addr.sin_addr) <= 0)
{
    perror("");
    
}

                if (connect(ss_fd1, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) == 0)
                {
                    Request *request = malloc(sizeof(Request));
                    if (request == NULL)
                    {
                        perror("Failed to allocate memory for request");
                        close(ss_fd1);
                         unlock_path(server_index);
                        return NULL;
                    }

                    strncpy(request->function, "DELETE", sizeof(request->function) - 1);
                    strncpy(request->src_path, src_path, sizeof(request->src_path) - 1);
                    memset(request->dest_path, 0, sizeof(request->dest_path));
                    memset(request->data, 0, sizeof(request->data));

                    if (send(ss_fd1, request, sizeof(Request), 0) == -1)
                    {
                        perror("Failed to send request to SS");
                        free(request);
                        close(ss_fd1);
                         unlock_path(server_index);
                        return NULL;
                    }

                    char ack[256]; unlock_path(server_index);
                    int ack_len = recv(ss_fd1, ack, sizeof(ack), 0); 
                    

                    if (ack_len > 0)
                    {
                        printf("ack-- %s\n",ack);
                         log_message("ack-- %s\n",ack);
                         if(strstr(ack,"success")!=NULL) {
                    change_ss(server_index,src_path,"DELETE"); deleteCh(path_cache,src_path);
                     }
                        send(client_fd, ack, ack_len, 0);  // Send acknowledgment back to the client
                    }
                    else
                    {   log_message("NO Ack from SS\n");
                        send(client_fd, "ERROR: No acknowledgment from Storage Server", strlen("ERROR: No acknowledgment from Storage Server"), 0);
                    }

                    free(request);
                    close(ss_fd1);
                }
                else
                {   unlock_path(server_index);
                     log_message("ERROR: Failed to connect to Storage Server");
                    send(client_fd, "ERROR: Failed to connect to Storage Server", strlen("ERROR: Failed to connect to Storage Server"), 0);
                }
            }
            else
            {  
              send(client_fd, "ERROR: Path not found", strlen("ERROR: Path not found"), 0);;
              log_message("ERROR: Path not found");
            }
        }
else if (strcmp(operation, "CREATE") == 0)
        {
            
            server_index = searchTrie(path_trie, src_path);
            ServerInfo server_info;
          
            if (server_index == -1)
            {  int sdx = 0; 
               int p; recv(client_fd,&p,sizeof(int),0);
               printf("folder or file %d\n",p);
               
                recv(client_fd,&sdx,sizeof(int),0);
               printf("index   %d\n",sdx);
              if (sdx > c_ss) goto cc5;
               if(sdx < 0) sdx = 0; 

                ss_fd1 = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in ss_addr;
                ss_addr.sin_family = AF_INET;
               // printf("e_ss_p %d\n",3109);
                ss_addr.sin_port =  htons(ss_info[sdx].extra_ss_port);
               if (inet_pton(AF_INET, ss_info[sdx].ip, &ss_addr.sin_addr) <= 0)
{
    perror("");
    
}

                if (connect(ss_fd1, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) == 0)
                {
                   // printf("dfhjfsdm,sjrknf\n");
                    Request *request = malloc(sizeof(Request));
                    if (request == NULL)
                    {
                        perror("Failed to allocate memory for request");
                        close(ss_fd1);
                        return NULL;
                    }

                    strncpy(request->function, "CREATE", sizeof(request->function) - 1);
                    strncpy(request->src_path, src_path, sizeof(request->src_path) - 1);
                    memset(request->dest_path, 0, sizeof(request->dest_path));
                    memset(request->data, 0, sizeof(request->data));
                    int l = p;
                    
                      //printf("e_ss_p \n");

                    if (send(ss_fd1, request, sizeof(Request), 0) == -1)
                    {
                        perror("Failed to send request to SS");
                        free(request);
                        close(ss_fd1);
                        return NULL;
                    }

                     send(ss_fd1, &p, sizeof(int), 0);

                   

                    char ack[1024];
                    int ack_len = recv(ss_fd1, ack, sizeof(ack), 0); 
                    if (ack_len > 0)
                     
                           
                    {    if(strstr(ack,"success")!=NULL) change_ss(sdx,src_path,"CREATE"); 
                         printf("ack-- %s\n",ack);
                          log_message("ack-- %s\n",ack);
                        send(client_fd, ack, ack_len, 0);  // Send acknowledgment back to the client
                    }
                    else
                    {  log_message("NO ack from SS\n ");
                        send(client_fd, "ERROR: No acknowledgment from Storage Server", strlen("ERROR: No acknowledgment from Storage Server"), 0);
                    }

                    free(request);
                    close(ss_fd1);
                }
                else
                {  log_message("NO connection to SS\n ");
                    send(client_fd, "ERROR: Failed to connect to Storage Server", strlen("ERROR: Failed to connect to Storage Server"), 0);
                }
            }
            else
            { cc5: log_message("Path there already\n");
                send(client_fd, "ERROR: Path already there", strlen("ERROR: Path already there"), 0);
            }
        }
        else if (strcmp(operation, "COPY") == 0)
        {
          
            if (src_path != NULL && dest_path != NULL)
            {
                  int src_server_index;
                if(scn(path_cache,src_path)==-1) {
                
             src_server_index = searchTrie(path_trie, src_path);
               if (src_server_index != -1)
             insert(path_cache,src_path,src_server_index);

            }
            else {
            printf("Cache Hit\n");    
            src_server_index = scn(path_cache,src_path); 
            }


                int dest_server_index;
                if(scn(path_cache,dest_path)==-1) {
                
             dest_server_index = searchTrie(path_trie, dest_path);
               if (dest_server_index != -1)
             insert(path_cache ,dest_path, dest_server_index);

            }
            else {
            printf("Cache Hit\n");    
            dest_server_index = scn(path_cache,dest_path); 
            }



                printf("%d %d\n",src_server_index, dest_server_index);

                if (src_server_index != -1 && dest_server_index != -1)
                {
                  


                    ss_fd1 = socket(AF_INET, SOCK_STREAM, 0);
                    struct sockaddr_in ss_addr;
                    ss_addr.sin_family = AF_INET;
                    ss_addr.sin_port = htons(ss_info[dest_server_index].extra_ss_port);
                    if (inet_pton(AF_INET, ss_info[dest_server_index].ip, &ss_addr.sin_addr) <= 0)
{
    perror("");
    
}
                    // printf("yess\n") ; 

                    if (connect(ss_fd1, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) == 0)
                    {
                        // printf("yess1\n") ; 
                        Request *request = malloc(sizeof(Request));
                        if (request == NULL)
                        {
                            perror("Failed to allocate memory for request");
                            close(ss_fd1);
                            return NULL;
                        }

                        strncpy(request->function, "COPY", sizeof(request->function) - 1);
                        strncpy(request->src_path, src_path, sizeof(request->src_path) - 1);
                        strncpy(request->dest_path, dest_path, sizeof(request->dest_path) - 1);
                        memset(request->data, 0, sizeof(request->data));

                        if (send(ss_fd1, request, sizeof(Request), 0) == -1)
                        {
                            perror("Failed to send request to SS");
                            free(request);
                            close(ss_fd1);
                            return NULL;
                        }

                        free(request);

                        char ack[1024];
                        int ack_len = recv(ss_fd1, ack, sizeof(ack), 0); 
                        if (ack_len > 0)
                        {
                             printf("ack-- %s\n",ack);
                              log_message("ack-- %s\n",ack);
                            send(client_fd, ack, ack_len, 0);  // Send acknowledgment back to the client
                        }
                        else
                        { log_message("ERROR: No acknowledgment from Storage Server\n");
                            send(client_fd, "ERROR: No acknowledgment from Storage Server", strlen("ERROR: No acknowledgment from Storage Server"), 0);
                        }

                        close(ss_fd1);
                    }
                }
                else
                {
                    log_message("ERROR: Source or destination path not found\n");
                    send(client_fd, "ERROR: Source or destination path not found", strlen("ERROR: Source or destination path not found"), 0);
                }
            }

            else
            {
                log_message("ERROR: Source and destination paths required\n");
                send(client_fd, "ERROR: Source and destination paths required", strlen("ERROR: Source and destination paths required"), 0);
            }
        }
      
    
        
        else if (strcmp(operation, "LIST") == 0)
        {
cc1:
            // Handle listing all accessible paths
            printf("%d\n",c_ss);
         send(client_fd,&c_ss,sizeof(int),0);
         for(int i = 0 ; i<=c_ss;i++){
            printf("INDEX : %d   ",i);
            send(client_fd,&i,sizeof(int),0);
            printf("PATHS: %s\n",ss_info[i].file_paths);
            send(client_fd,ss_info[i].file_paths,sizeof(ss_info[i].file_paths),0);
         }
         char buff[1024];
         recv(client_fd,buff,sizeof(buff),0);
         log_message("%s\n",buff);
         printf("%s\n",buff);
        }
        else if (strcmp(operation, "QUIT") == 0){
cc10:         
//  printf("ADSJKJADKD\n");
            char  buffQt [1024];
            recv(client_fd,buffQt,sizeof(buffQt)-1,0);
            printf("FEEDBACK: %s\n",buffQt);
            log_message("FEEDBACK: %s\n",buffQt);
            //free(buff);
        }
        else
        {
            log_message( "ERROR: Invalid operation\n");
            send(client_fd, "ERROR: Invalid operation", strlen("ERROR: Invalid operation"), 0);
        }
    }
    }






    close(client_fd); // Clean up client connection
    pthread_exit(NULL);
}
