#include "headers.h" // Ensure this includes standard libraries and necessary headers for the client
#include "signal.h"
#include "wait.h"
// Define constants
#define MAX_PATH_SIZE 4099
#define BUFFER_SIZE 4099
#define MAX_PENDING 20000
#define ACK_LENGTH 256
// Structure to store Naming Server connection information
typedef struct
{
    int socket_fd;
    struct sockaddr_in server_address;
} NamingServerConnection;

typedef struct
{
    char operation[32];
    char src_path[256];
    char dest_path[256];
    char data[256];

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
    char ip[50];
    int ss_port;
    int cl_port;
    int num;
    int temp;
    char file_paths[100000];
    char file_path_org[1000];
} StorageServerInfo;

struct FileMetadata
{
    char file_path[1024];
    long long file_size;
    int access_rights;
    char last_accessed[1024];
    char last_modified[1024];
    char last_status_change[1024];
};

void playAudio(int socket)
{
    int x;
    size_t recvv = recv(socket, &x, sizeof(int), 0);
    if (x == 0)
        printf("Streaming the song successfully\n");

    int pipe_fds[2];
    if (pipe(pipe_fds) == -1)
    {
        perror("Pipe creation failed");
        return;
    }
    pid_t pid = fork();
    if (pid == -1)
    {
        perror("Fork failed");
        return;
    }
    else if (pid == 0)
    {
        // Child process: plays audio using mpv
        close(pipe_fds[1]);              // Close write end of the pipe
        dup2(pipe_fds[0], STDIN_FILENO); // Redirect stdin to read end of pipe
        close(pipe_fds[0]);              // Close read end of pipe
        execlp("mpv", "mpv", "--no-terminal", "--", "-", NULL);
        perror("execlp failed");
        // exit(EXIT_FAILURE);
    }
    else
    {
        // Parent process: handles audio data and manages mpv lifecycle
        close(pipe_fds[0]); // Close read end of the pipe
        char buffer[BUFFER_SIZE];
        ssize_t bytesRead;

        while (1)
        {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(STDIN_FILENO, &read_fds);     // Monitor standard input for user commands
            FD_SET(socket, &read_fds);           // Monitor socket for incoming audio data
            struct timeval timeout = {0, 10000}; // 10ms timeout for select
            int activity = select(socket + 1, &read_fds, NULL, NULL, &timeout);

            if (activity > 0)
            {
                if (FD_ISSET(STDIN_FILENO, &read_fds))
                {
                    // Handle user commands for audio control
                    char command[32];
                    if (fgets(command, sizeof(command), stdin) != NULL)
                    {
                        if (strncmp(command, "stop", 4) == 0)
                        {
                            kill(pid, SIGKILL);
                            printf("Playback stopped.\n");
                            break;
                        }
                        else
                        {
                            printf("Unknown command: %s", command);
                        }
                    }
                }
                if (FD_ISSET(socket, &read_fds))
                {
                    // Read audio data from socket and write to pipe
                    bytesRead = recv(socket, buffer, sizeof(buffer), 0);
                    if (bytesRead > 0)
                    {
                        if (write(pipe_fds[1], buffer, bytesRead) == -1)
                        {
                            perror("Error writing to pipe");
                            break;
                        }
                    }
                    else if (bytesRead == 0)
                    {
                        // Server closed connection gracefully
                        printf("Server closed the connection.\n");
                        break;
                    }
                    else
                    {
                        perror("Error reading from socket");
                        break;
                    }
                }
            }

            // Check if the child process (mpv) has terminated
            int status;
            pid_t result = waitpid(pid, &status, WNOHANG);
            if (result > 0) // Child process terminated
            {
                if (WIFEXITED(status))
                {
                    printf("mpv exited with status %d.\n", WEXITSTATUS(status));
                }
                else if (WIFSIGNALED(status))
                {
                    printf("mpv terminated by signal %d.\n", WTERMSIG(status));
                }

                // Optionally restart the player or handle termination gracefully
                printf("mpv has terminated. Waiting for new commands or data...\n");
                pid = -1; // Mark as terminated
            }
        }

        // Clean up resources
        close(pipe_fds[1]);
        if (pid > 0)
        {
            kill(pid, SIGKILL);    // Ensure child process is terminated
            waitpid(pid, NULL, 0); // Wait for child process
        }
    }
}

int connect_to_naming_server(NamingServerConnection *ns_conn, const char *server_ip, int server_port)
{
    ns_conn->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ns_conn->socket_fd < 0)
    {
        perror("Socket creation failed");
        return -1;
    }

    memset(&ns_conn->server_address, 0, sizeof(ns_conn->server_address));
    ns_conn->server_address.sin_family = AF_INET;
    ns_conn->server_address.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip, &ns_conn->server_address.sin_addr) <= 0)
    {
        perror("Invalid address");
        close(ns_conn->socket_fd);
        return -1;
    }

    if (connect(ns_conn->socket_fd, (struct sockaddr *)&ns_conn->server_address, sizeof(ns_conn->server_address)) < 0)
    {
        perror("Connection to Naming Server failed");
        close(ns_conn->socket_fd);
        return -1;
    }

    printf("Connected to Naming Server at %s:%d\n", server_ip, server_port);
    return 0;
}

// Connect to a storage server
int connect_to_ss(const char *ip, int ss_port)
{
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1)
    {
        perror("Socket creation failed");
        return -1;
    }

    struct sockaddr_in ss_address = {0};
    ss_address.sin_family = AF_INET;
    ss_address.sin_port = htons(ss_port);

    if (inet_pton(AF_INET, ip, &ss_address.sin_addr) <= 0)
    {
        perror("Invalid address");
        close(client_socket);
        return -1;
    }

    if (connect(client_socket, (struct sockaddr *)&ss_address, sizeof(ss_address)) == -1)
    {
        perror("Connection to storage server failed");
        close(client_socket);
        return -1;
    }

    printf("Connected to storage server at %s:%d\n", ip, ss_port);
    return client_socket;
}

void send_request(NamingServerConnection *ns_conn, const char *request)
{
    printf("Sent request: %s\n", request);
    if (send(ns_conn->socket_fd, request, strlen(request), 0) < 0)
    {
        perror("Failed to send request");
        //  exit(EXIT_FAILURE);
    }
}

ServerInfo receive_server_info(NamingServerConnection *ns_conn)
{
    ServerInfo server_info;
    ssize_t bytes_received = recv(ns_conn->socket_fd, &server_info, sizeof(ServerInfo), 0);

    if (bytes_received <= 0)
    {
        perror("Failed to receive StorageServerInfo struct");
        //  exit(EXIT_FAILURE);
    }

    printf("Received IP: %s\n", server_info.ip);
    printf("Received SS Port: %d\n", server_info.ss_port);
    return server_info;
}

int connect_to_storage_server(const char *ip, int port)
{
    int ss_sock = connect_to_ss(ip, port);
    if (ss_sock == -1)
    {
        fprintf(stderr, "Failed to connect to storage server at %s:%d\n", ip, port);
        return -1;
    }
    return ss_sock;
}

void parse_request(Request *client_args, const char *request)
{
    char *operation = strtok(strdup(request), " ");
    if (!operation)
    {
        fprintf(stderr, "Error: Missing operation in request\n");
        return;
    }
    strcpy(client_args->operation, operation);

    if (strstr(request, "READ") || strstr(request, "STREAM") || strstr(request, "GET_INFO"))
    {
        printf("hiii\n");
        char *path = strtok(NULL, " ");
        if (!path)
        {
            fprintf(stderr, "Error: Missing path in request\n");
            return;
        }
        strcpy(client_args->src_path, path);
    }
    else if (strstr(request, "WRITE"))
    {
        char *path = strtok(NULL, " ");
        if (!path)
        {
            fprintf(stderr, "Error: Missing path in WRITE request\n");
            return;
        }
        strcpy(client_args->src_path, path);

        char *data = strtok(NULL, "\n");
        if (!data)
        {
            fprintf(stderr, "Error: Missing data in WRITE request\n");
            //  exit(EXIT_FAILURE);
            return;
        }
        strncpy(client_args->data, data, strlen(data) - 1);
    }
    else
    {
        fprintf(stderr, "Error: Unknown operation in request\n");
        exit(EXIT_FAILURE);
    }

    client_args->dest_path[0] = '\0';
}

void receive_acknowledgment(int ss_sock)
{
    char ack_buffer[ACK_LENGTH];
    ssize_t bytes_received = recv(ss_sock, ack_buffer, sizeof(ack_buffer) - 1, 0);

    if (bytes_received <= 0)
    {
        perror("Failed to receive acknowledgment");
        // exit(EXIT_FAILURE);
    }

    ack_buffer[bytes_received] = '\0'; // Null-terminate the string
    printf("Acknowledgment from server: %s\n", ack_buffer);
}

void handle_server_response(NamingServerConnection *ns_conn, int ss_sock, const char *operation)
{
    if (strcmp(operation, "STREAM") == 0)
    {
        playAudio(ss_sock);
    }
    else if (strcmp(operation, "GET_INFO") == 0)
    {
        struct FileMetadata *file_info = malloc(sizeof(struct FileMetadata));
        if (!file_info)
        {
            perror("Memory allocation failed");
            close(ss_sock);
            return;
        }

        // Receive the FileMetadata struct from the server
        ssize_t bytes_received = recv(ss_sock, file_info, sizeof(struct FileMetadata), 0);
        if (bytes_received <= 0)
        {
            perror("Error receiving data");
            //   free(file_info);
            close(ss_sock);
            return;
        }

        // Print the received file information
        printf("File Path: %s\n", file_info->file_path);
        printf("File Size: %lld bytes\n", file_info->file_size);
        printf("Access Rights: %d\n", file_info->access_rights);
        printf("Last Accessed: %s\n", file_info->last_accessed);
        printf("Last Modified: %s\n", file_info->last_modified);
        printf("Last Status Change: %s\n", file_info->last_status_change);

        int ack;

        size_t aaa = recv(ss_sock, &ack, sizeof(int), 0);
        char ack_buffer[ACK_LENGTH];
        if (ack == 0)
        {
            strcpy(ack_buffer, "GET_INFO succesful");
            printf("GET_INFO succesful\n");
            send(ns_conn->socket_fd, ack_buffer, sizeof(ack_buffer), 0);
        }
        else
        {
            strcpy(ack_buffer, "GET_INFO failed");
            send(ns_conn->socket_fd, ack_buffer, sizeof(ack_buffer), 0);
        }
    }
    else if (strcmp(operation, "READ") == 0)
    {
        // Receive file content
        char response_buffer[BUFFER_SIZE];
        ssize_t bytes_received = recv(ss_sock, response_buffer, sizeof(response_buffer) - 1, 0);
        if (bytes_received > 0)
        {
            response_buffer[bytes_received] = '\0'; // Null-terminate the response
            printf("Received file content: %s\n", response_buffer);
        }
        else
        {
            perror("Failed to receive file content");
        }

        int ack;

        size_t aaa = recv(ss_sock, &ack, sizeof(int), 0);
        char ack_buffer[ACK_LENGTH];
        if (ack == 0)
        {
            strcpy(ack_buffer, "Read succesful");
            printf("Read succesful\n");
            send(ns_conn->socket_fd, ack_buffer, sizeof(ack_buffer), 0);
        }
        else
        {
            strcpy(ack_buffer, "Read failed");
            send(ns_conn->socket_fd, ack_buffer, sizeof(ack_buffer), 0);
        }
    }
    else if (strstr("WRITE", operation) != NULL)
    {

        printf("Enter choice 1 for synchronous and 0 for asynchronously:");
        int choice;
        scanf("%d", &choice);
        if (send(ss_sock, &choice, sizeof(int), 0) < 0)
        {
            perror("Failed to send data");
            close(ss_sock);
            //   exit(EXIT_FAILURE);
        }

        char ack_buffer[ACK_LENGTH];
        ssize_t bytes_received = recv(ss_sock, ack_buffer, sizeof(ack_buffer) - 1, 0);

        if (bytes_received <= 0)
        {
            perror("Failed to receive acknowledgment");
            //  exit(EXIT_FAILURE);
        }

        ack_buffer[bytes_received] = '\0'; // Null-terminate the string
        printf("Acknowledgment from server: %s\n", ack_buffer);

        send(ns_conn->socket_fd, ack_buffer, sizeof(ack_buffer), 0);
        printf("Sent to nm : %s", ack_buffer);

        if (choice == 0)
        {
            char ack[ACK_LENGTH];
            size_t bytesrecv = recv(ns_conn->socket_fd, ack, sizeof(ack), 0);
            printf("Recieved from nm : %s", ack);
        }
    }
    else
    {
        char response_buffer[1024];
        ssize_t bytes_received = recv(ss_sock, response_buffer, sizeof(response_buffer) - 1, 0);
        if (bytes_received > 0)
        {
            response_buffer[bytes_received] = '\0'; // Null-terminate the response
            printf("Received from server: %s\n", response_buffer);
        }
        else
        {
            perror("Failed to receive data");
        }
    }
}

void ss_client(NamingServerConnection *ns_conn, const char *request)
{
    send_request(ns_conn, request);

    ServerInfo server_info = receive_server_info(ns_conn);
    if (server_info.server_index < 0)
    {
        char ack_buffer[256] = "No such storage server found";
        send(ns_conn->socket_fd, ack_buffer, sizeof(ack_buffer), 0);
        printf("Such Storage Server doesn't exsist\n");
        return;
    }
    int ss_sock = connect_to_storage_server(server_info.ip, server_info.ss_port);

    Request *client_args = malloc(sizeof(Request));
    if (!client_args)
    {
        perror("Failed to allocate memory for Request");
        close(ss_sock);
        // exit(EXIT_FAILURE);
    }

    parse_request(client_args, request);

    if (send(ss_sock, client_args, sizeof(Request), 0) < 0)
    {
        perror("Failed to send data");
        // free(client_args);
        close(ss_sock);
        // exit(EXIT_FAILURE);
    }

    printf("Request struct sent successfully\n");

    handle_server_response(ns_conn, ss_sock, client_args->operation);

    // free(client_args);
    close(ss_sock);
}

void nm_client(NamingServerConnection *ns_conn, const char *request)
{
    if (send(ns_conn->socket_fd, request, strlen(request), 0) < 0)
    {
        perror("Failed to send request");
        return;
    }

    if (strstr(request, "CREATE") != NULL)
    {
        int x;
        printf("Enter 1 to Create a File or Enter 0 to Create a Folder: ");
        scanf("%d", &x);
        send(ns_conn->socket_fd, &x, sizeof(int), 0);
        int server_index;
        printf("Enter the ss index in which file is to be created: ");
        scanf("%d", &server_index);
        send(ns_conn->socket_fd, &server_index, sizeof(int), 0);
        char buf[256] = {0};
        size_t bytesrecv;                                              // Ensure buffer is cleared
        bytesrecv = recv(ns_conn->socket_fd, buf, sizeof(buf) - 1, 0); // -1 for null terminator
        printf("recieved acknowledgement from nm:%s\n", buf);
    }
    else if (strstr(request, "DELETE") != NULL || strstr(request, "COPY") != NULL)
    {
        char buf[256] = {0};
        size_t bytesrecv;                                              // Ensure buffer is cleared
        bytesrecv = recv(ns_conn->socket_fd, buf, sizeof(buf) - 1, 0); // -1 for null terminator
        printf("recieved acknowledgement from nm:%s\n", buf);
    }
    else if (strstr(request, "LIST") != NULL)
    {
        int size = 0;
        ssize_t bytesrecv;

        // Receive the size of the list
        bytesrecv = recv(ns_conn->socket_fd, &size, sizeof(int), 0);
        if (bytesrecv <= 0)
        {
            if (bytesrecv == 0)
                fprintf(stderr, "Connection closed by peer.\n");
            else
                perror("Failed to receive size of list");
            return; // Exit or handle gracefully
        }

        printf("size: %d\n", size);
        if (size == 0)
        {
            printf("No files found\n");
            return;
        }
        // Loop to receive `size` number of entries
        while (size--)
        {
            int idx;
            bytesrecv = recv(ns_conn->socket_fd, &idx, sizeof(int), 0);
            if (bytesrecv <= 0)
            {
                if (bytesrecv == 0)
                    fprintf(stderr, "Connection closed while receiving index.\n");
                else
                    perror("Failed to receive index");
                return; // Exit or handle gracefully
            }

            char buf[10000] = {0};                                         // Ensure buffer is cleared
            bytesrecv = recv(ns_conn->socket_fd, buf, sizeof(buf) - 1, 0); // -1 for null terminator
            if (bytesrecv <= 0)
            {
                if (bytesrecv == 0)
                    fprintf(stderr, "Connection closed while receiving data.\n");
                else
                    perror("Failed to receive data");
                return; // Exit or handle gracefully
            }

            buf[bytesrecv] = '\0'; // Null-terminate the received string
            printf("Index: %d, Data: %s\n", idx, buf);
        }
        char ack[ACK_LENGTH] = "List succesful";
        send(ns_conn->socket_fd, ack, sizeof(ack), 0);
    }
}

// Main function
int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <NamingServer IP> <NamingServer Port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *naming_server_ip = argv[1];
    int naming_server_port = atoi(argv[2]);

    NamingServerConnection ns_conn;

    if (connect_to_naming_server(&ns_conn, naming_server_ip, naming_server_port) < 0)
    {
        return EXIT_FAILURE;
    }

    char input[BUFFER_SIZE];
    while (1)
    {
        printf("Enter command (READ <path>, WRITE <path> <data>, or QUIT to exit): ");
        if (!fgets(input, sizeof(input), stdin))
            break;

        input[strcspn(input, "\n")] = '\0'; // Remove newline
        if (strcmp(input, "QUIT") == 0)
        {
            char feedback[500];
            char feedback2[600];

            send(ns_conn.socket_fd, "QUIT", strlen("QUIT"), 0);
            fgets(feedback, sizeof(feedback), stdin);
            send(ns_conn.socket_fd, feedback, sizeof(feedback), 0);
            sleep(1);
            return 0;
        }

        if (strncmp(input, "READ", 4) == 0 || strncmp(input, "STREAM", 6) == 0 ||
            strncmp(input, "WRITE", 5) == 0 || strstr(input, "GET_INFO") != NULL)
        {
            ss_client(&ns_conn, input);
        }
        else if (strncmp(input, "CREATE", 6) == 0 || strncmp(input, "DELETE", 6) == 0 || strncmp(input, "COPY", 4) == 0 || strstr(input, "LIST") != NULL)
        {
            nm_client(&ns_conn, input);
        }
        else
        {
            printf("Invalid Command\n");
            continue;
        }
    }

    close(ns_conn.socket_fd);
    printf("Disconnected from Naming Server\n");
    return EXIT_SUCCESS;
}