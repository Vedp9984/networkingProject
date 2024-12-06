#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <limits.h>

#include <dirent.h>
#define PATH_MAX 4096
#include "ss_function.h"
//#include "get_ip.h"
#include <asm-generic/socket.h>
#include "ss_function.h"
// #include <climits>

#define MAX_CLIENTS 2000
#define BUFFER_SIZE 1024
#define PATH_LENGTH 1024
#define NAME_SIZE 128
#define MAX_PATHS 10000

struct FileMetadata *metadata;
typedef struct
{
    char operation[32];
    char src_path[256];
    char dest_path[256];
    char data[1024];

} Request;

typedef struct
{
    int socket_fd;
    struct sockaddr_in address;
} C_args;

struct storage_server
{
    char ip[INET_ADDRSTRLEN];
    int port_nm;
    int port_client;
    int num;
    int extra_ss_port;
    int temp;
    char file_paths[999999];
    char file_path_org[1000];
};
 char shared_var [150];

void *client_handler(void *args);
void process_request(int client_sock, Request *request);
void *naming_server_listener(void *args);
int create_server_socket(int port);
void *handle_client_request(void *args);
void *handle_naming_request(void *args);
void collect_file_paths(const char *directory, char *paths, int *num_paths);
void collect_file_paths_recursively(const char *directory, char *paths, int *num_paths, int *capacity, int *current_length);
void send_server_details(int sock, struct storage_server *server_details);
int create_socket_and_connect(const char *ip, int port);

char home_directory[128];
struct storage_server server_details;
int main(int argc, char *argv[])
{
    if (argc != 5)
    {
        fprintf(stderr, "Usage: %s<ip> <client_storage_port> <port_nm> <extraport> \n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int client_storage_port = atoi(argv[2]);
    int port_nm = atoi(argv[3]);
    int extra = atoi(argv[4]);

    printf("%d %d %d\n", port_nm, client_storage_port, extra);
    char current_directory[MAX_PATHS];
    getcwd(current_directory, sizeof(current_directory));
   
    strncpy(shared_var , get_ip_address(),sizeof(shared_var));
    strncpy(server_details.ip, shared_var, sizeof(server_details.ip));
    server_details.port_nm = port_nm;
    server_details.port_client = client_storage_port;
    server_details.extra_ss_port = extra;
    server_details.temp = 2005;
    memset(server_details.file_paths, 0, sizeof(server_details.file_paths));
    server_details.num = 0;
    char dir_path[MAX_PATHS];
    printf("Enter directory path to collect file paths: ");
    if (fgets(dir_path, sizeof(dir_path), stdin) != NULL)
    {  strncpy(server_details.file_path_org, dir_path,sizeof(server_details.file_path_org)-1);
        size_t len = strlen(dir_path);
        if (len > 0 && dir_path[len - 1] == '\n')
        {
            dir_path[len - 1] = '\0';
        }
    }
   
    collect_file_paths(dir_path, server_details.file_paths, &server_details.num);
    int sock = create_socket_and_connect(argv[1], port_nm);
    send_server_details(sock, &server_details);
    pthread_t naming_server_thread, client_handler_thread;
    int p_client = server_details.port_client;
    int p_nm = server_details.extra_ss_port;

    if (pthread_create(&naming_server_thread, NULL, naming_server_listener, &p_nm) != 0)
    {

        perror("Error creating naming server thread");

        exit(EXIT_FAILURE);
    }

    if (pthread_create(&client_handler_thread, NULL, client_handler, &p_client) != 0)
    {
        perror("Error creating client handler thread");
        exit(EXIT_FAILURE);
    }
    pthread_join(naming_server_thread, NULL);
    pthread_join(client_handler_thread, NULL);
    printf("Disconnected from naming server.\n");
    return 0;
}

int create_socket_and_connect(const char *ip, int port)
{
    int sock;
    struct sockaddr_in server_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0)
    {
        perror("Socket error");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    printf("Attempting to connect to IP: %s, Port: %d\n", ip, port);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("Connection error");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Connected successfully\n");

    return sock;
}

void collect_file_paths(const char *directory, char *paths, int *num_paths)
{
    int capacity =999999;
    int current_length = 0;
    collect_file_paths_recursively(directory, paths, num_paths, &capacity, &current_length);
}

void collect_file_paths_recursively(const char *directory, char *paths, int *num_paths, int *capacity, int *current_length)
{
    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;
    char path[MAX_PATHS];

    if ((dir = opendir(directory)) == NULL)
    {
        perror("opendir() error");
        return;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        if (stat(path, &statbuf) == -1)
        {
            perror("stat() error");
            continue;
        }

        if (S_ISDIR(statbuf.st_mode))
        {

            collect_file_paths_recursively(path, paths, num_paths, capacity, current_length);
        }
        else
        {

            int path_len = strlen(path);
            if (*current_length + path_len + 2 >= *capacity)
            {
                fprintf(stderr, "Buffer overflow, not enough space to store all paths.\n");
                closedir(dir);
                return;
            }
            strcpy(&paths[*current_length], path);
            *current_length += path_len;
            paths[*current_length] = '\n';
            (*current_length)++;
            paths[*current_length] = '\0';

            (*num_paths)++;
            if (*num_paths >= MAX_PATHS)
            {
                break;
            }
        }
    }
    closedir(dir);
}

void send_server_details(int sock, struct storage_server *server_details)
{
    size_t struct_size = sizeof(struct storage_server);

    if (send(sock, server_details, struct_size, 0) == -1)
    {
        perror("Error sending struct to naming server");
        exit(EXIT_FAILURE);
    }
}

int create_server_socket(int port)
{
    int server_sock;
    struct sockaddr_in server_addr;

    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket failed");
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("setsockopt");
        close(server_sock);
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        close(server_sock);
        return -1;
    }

    if (listen(server_sock, 3) < 0)
    {
        perror("Listen failed");
        close(server_sock);
        return -1;
    }

    return server_sock;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~below this client is being handled~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void *client_handler(void *args)
{
    int port = *(int *)args;
    int server_sock = create_server_socket(port);

    if (server_sock < 0)
        return NULL;

    printf("Storage server listening on port %d...\n", port);
    while (1)
    {
        printf("Entered in while loop\n");
        C_args *client_args = malloc(sizeof(C_args));

        socklen_t addr_len = sizeof(client_args->address);
        client_args->socket_fd = accept(server_sock, (struct sockaddr *)&client_args->address, &addr_len);

        if (client_args->socket_fd < 0)
        {
            perror("Error accepting client connection");
            free(client_args);
            continue;
        }

        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, (void *)handle_client_request, client_args) != 0)
        {
            perror("Error creating client handler thread");
            close(client_args->socket_fd);
            free(client_args);
        }
        else
        {
            pthread_detach(client_thread);
        }
    }
    close(server_sock);
    return NULL;
}

void *handle_client_request(void *args)
{
    C_args *client_args = (C_args *)args;
    int client_sock = client_args->socket_fd;
    printf("%d\n", client_sock);
    Request *request = malloc(sizeof(Request));
    if (request == NULL)
    {
        perror("Failed to allocate memory for request");
        close(client_sock);
        return NULL;
    }
    if (recv(client_sock, request, sizeof(Request), 0) <= 0)
    {
        perror("Failed to receive request from client");
        free(request);
        close(client_sock);
        return NULL;
    }

    printf("Received request: %s %s %s %s\n", request->operation, request->src_path, request->dest_path, request->data);
    process_request(client_sock, request);
    free(request);
    close(client_sock);
    free(client_args);
    return NULL;
}

void process_request(int client_sock, Request *request)
{
    // printf("aa gya hoon process main\n");
    printf("Received requestin process Request: %s %s %s %s\n", request->operation, request->src_path, request->dest_path, request->data);
    char full_path[PATH_LENGTH];
    snprintf(full_path, sizeof(full_path), "%s/%s", home_directory, request->src_path);

    if (strcmp(request->operation, "READ") == 0)
    {

        readFile(full_path, client_sock);
    }
    else if (strcmp(request->operation, "WRITE") == 0)
    {
        int kya;
        if (recv(client_sock, &kya, sizeof(int), 0) <= 0)
        {
            perror("Failed to receive request from client");
            close(client_sock);
            return;
        }
        printf("flag taken-->%d\n", kya);
        int flag = writeFile_with_sync_and_async(full_path, client_sock, request->data, kya);
        printf("flag==:%d\n", flag);

        if (kya == 0)
        { 
            
             int port=8080;
         struct sockaddr_in server_addr, client_addr;
         int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return ;
    }

               server_addr.sin_family = AF_INET;
               
     if (inet_pton(AF_INET, shared_var, &server_addr.sin_addr) <= 0)
{
    perror("");
    
}
    server_addr.sin_port = htons(8080);

    // Bind the socket to the specified address and port
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        return ;
    }

    // Listen for incoming connections
    if (listen(sockfd, 100) < 0) {
        perror("Listen failed");
        close(sockfd);
        return ;
    }

    // Accept a client connection
    int addr_size = sizeof(client_addr);
   int new_sock = accept(sockfd, (struct sockaddr*)&client_addr, &addr_size);
    if (new_sock < 0) {
        perror("Accept failed");
        close(sockfd);
        return ;
    }
            ssize_t bytes_sent = send(new_sock, &flag, sizeof(int), 0);
            if (bytes_sent == -1)
            {
                ;
            }
            else if (bytes_sent != sizeof(int))
            {
               
                fprintf(stderr, "Partial send: only %zd bytes sent instead of %zu\n", bytes_sent, sizeof(int));
                  
            }
            close(sockfd);
        }
    }
    else if (strcmp(request->operation, "SIZE") == 0)
    {
        getFileSize(full_path, client_sock);
    }
    else if (strcmp(request->operation, "PERMISSION") == 0)
    {
        getFilePermissions(full_path, client_sock);
    }
    else if (strcmp(request->operation, "STREAM") == 0)
    {
        streamAudioFile(full_path, client_sock);
    }
    else if (strcmp(request->operation, "CREATE") == 0)
    {

        int kya;
        if (recv(client_sock, &kya, sizeof(int), 0) <= 0)
        {
            perror("Failed to receive request from client");
            close(client_sock);
            return;
        }
        
        printf("kya recv\n");
        createFileOrDirectory(client_sock, request->src_path, request->data, kya);
    }
    else if (strcmp(request->operation, "DELETE") == 0)
    {

        deleteFileOrDirectory(client_sock, request->src_path);
    }
    else if (strcmp(request->operation, "COPY") == 0)
    {

        copyPath(request->src_path, request->dest_path, client_sock);
    }
    else if (strcmp(request->operation, "GET_INFO") == 0)
    {
        struct FileMetadata *metadata = malloc(sizeof(struct FileMetadata));
        if (!metadata)
        {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        get_file_metadata(request->src_path, metadata, client_sock);
        free(metadata); // Don't forget to free the allocated memory when done
    }
    else
    {
        ;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~below work are related to naming server~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void *naming_server_listener(void *args)
{
    int port = *(int *)args;
    int naming_sock = create_server_socket(port);
    if (naming_sock < 0)
        return NULL;

    printf("Naming server listening on port %d...\n", port);
    while (1)
    {
        int *client_sock = malloc(sizeof(int));
        if (client_sock == NULL)
        {
            perror("Failed to allocate memory for client socket");
            continue;
        }

        *client_sock = accept(naming_sock, NULL, NULL);
        if (*client_sock < 0)
        {
            perror("Error accepting naming server connection");
            free(client_sock);
            continue;
        }
        pthread_t naming_thread;
        if (pthread_create(&naming_thread, NULL, (void *)handle_naming_request, client_sock) != 0)
        {
            perror("Error creating naming server handler thread");
            close(*client_sock);
            free(client_sock);
        }
        else
        {
            pthread_detach(naming_thread);
        }
    }
    close(naming_sock);
    return NULL;
}

void *handle_naming_request(void *args)
{
    C_args *client_args = (C_args *)args;
    int nm_ss_sock = client_args->socket_fd;

    Request *request = malloc(sizeof(Request));
    if (request == NULL)
    {
        perror("Failed to allocate memory for request");
        close(nm_ss_sock);
        return NULL;
    }
    if (recv(nm_ss_sock, request, sizeof(Request), 0) <= 0)
    {
        perror("Failed to receive request from client");
        free(request);
        close(nm_ss_sock);
        return NULL;
    }
    printf("Received request in naming server : %s %s %s %s\n", request->operation, request->src_path, request->dest_path, request->data);
    process_request(nm_ss_sock, request);
    free(request);
    close(nm_ss_sock);
    free(client_args);
    return NULL;
}