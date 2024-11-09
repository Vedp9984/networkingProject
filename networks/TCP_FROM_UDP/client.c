#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <time.h>

#define CHUNK_SIZE 3
#define MAX_CHUNKS 40
#define TIME_OUT 0.1
#define TIMEOUT_USEC 1000000 // 0.1 seconds
#define PORT 8080
#define BUFFER_SIZE 1024
#define HANDSHAKE_MSG "Hello from client"

struct chunk
{
    int sequence_number;
    char data[CHUNK_SIZE + 1];
};

void shuffle(struct chunk *chunks, int n);
void send_message(int sockfd, struct sockaddr_in *addr, char *message);
void receive_messages(int sockfd);
void perform_handshake(int sockfd, struct sockaddr_in *server_addr);
int main(int argc, char *argv[])
{
    int sockfd;
    struct sockaddr_in server_addr;

    if (argc < 2)
    {
        printf("usage: ./a.out <server_ip>");
        exit(0);
    }
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("socket creation failed");
        exit(1);
    }

    // Set server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr); // Use appropriate server IP

    // Establish connection
    perform_handshake(sockfd, &server_addr);
    // set_nonblocking(sockfd);

    char message[256];
    while (1)
    {
        printf("Enter message to send (type 'exit' to quit): ");
        fgets(message, sizeof(message), stdin);
        message[strcspn(message, "\n")] = '\0'; // Remove newline character

        if (strcmp(message, "exit") == 0)
        {
            break;
        }

        send_message(sockfd, &server_addr, message);
        receive_messages(sockfd);
    }

    close(sockfd);
    return 0;
}

void shuffle(struct chunk *chunks, int n)
{
    if (n > 1)
    {
        srand(time(NULL));
        for (int i = n - 1; i > 0; i--)
        {
            int j = rand() % (i + 1);
            struct chunk temp = chunks[i];
            chunks[i] = chunks[j];
            chunks[j] = temp;
        }
    }
}

void perform_handshake(int sockfd, struct sockaddr_in *server_addr)
{
    char buffer[BUFFER_SIZE];

    sendto(sockfd, HANDSHAKE_MSG, strlen(HANDSHAKE_MSG), 0, (struct sockaddr *)server_addr, sizeof(*server_addr));
    printf("Client sent handshake: %s\n", HANDSHAKE_MSG);

    socklen_t addr_len = sizeof(*server_addr);
    ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)server_addr, &addr_len);
    if (n < 0)
    {
        perror("Error receiving acknowledgment");
        exit(EXIT_FAILURE);
    }

    buffer[n] = '\0';
    printf("Client received acknowledgment from server: %s\n", buffer);
}
void receive_acknowledgement(int sockfd, int *acknowledgements, int total_chunks)
{
    struct chunk ack;

    while (1)
    {
        fd_set read_fds;
        struct timeval timeout;

        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);

        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        int activity = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);

        if (activity < 0)
        {
            perror("select error");
            exit(EXIT_FAILURE);
        }
        else if (activity == 0)
        {

            break;
        }
        else if (FD_ISSET(sockfd, &read_fds))
        {
            ssize_t recv_size = recvfrom(sockfd, &ack, sizeof(ack), MSG_DONTWAIT, NULL, NULL);
            if (recv_size < 0)
            {
                perror("Failed to receive ACK");
            }
            else if (recv_size == sizeof(ack))
            {
                if (ack.sequence_number >= 0 && ack.sequence_number < total_chunks)
                {
                    printf("Received ACK for chunk %d\n", ack.sequence_number);
                    acknowledgements[ack.sequence_number] = 1;
                }
                else
                {
                    printf("Received invalid ACK for chunk %d\n", ack.sequence_number);
                }
            }
        }
    }
}

// Function to send chunks
void send_chunks(int sockfd, struct sockaddr_in *addr, struct chunk *chunks, int total_chunks)
{
    socklen_t addr_len = sizeof(*addr);
    for (int i = 0; i < total_chunks; i++)
    {
        int sent = sendto(sockfd, &chunks[i], sizeof(struct chunk), 0, (struct sockaddr *)addr, addr_len);
        if (sent < 0)
        {
            perror("Failed to send chunk");
            exit(EXIT_FAILURE);
        }
        
    }
}

void check_timeouts_and_resend(int sockfd, struct sockaddr_in *addr, struct chunk *chunks, int *acknowledgements, struct timeval *send_times, int total_chunks)
{
    struct timeval current_time;
    socklen_t addr_len = sizeof(*addr);

    gettimeofday(&current_time, NULL);
    for (int i = 0; i < total_chunks; i++)
    {
        if (!acknowledgements[i])
        {
            long taken_time = (current_time.tv_sec - send_times[i].tv_sec) * 1000 + (current_time.tv_usec - send_times[i].tv_usec) / 1000;
            if (taken_time >= TIME_OUT)
            {
                printf("Timeout for chunk %d, resending\n", i);
                int sent = sendto(sockfd, &chunks[i], sizeof(struct chunk), 0, (struct sockaddr *)addr, addr_len);
                if (sent < 0)
                {
                    perror("Failed to resend chunk");
                }
                else
                {
                    gettimeofday(&send_times[i], NULL);
                }
            }
        }
    }
}

int all_chunks_acknowledged(int *acknowledgements, int total_chunks)
{
    for (int i = 0; i < total_chunks; i++)
    {
        if (!acknowledgements[i])
        {
            return 0;
        }
    }
    return 1;
}

void split_message_into_chunks(char *message, struct chunk *chunks, int *total_chunks)
{
    int message_length = strlen(message);
    *total_chunks = 0;

    for (int i = 0; i < message_length; i += CHUNK_SIZE)
    {
        chunks[*total_chunks].sequence_number = *total_chunks;
        strncpy(chunks[*total_chunks].data, message + i, CHUNK_SIZE);
        chunks[*total_chunks].data[CHUNK_SIZE] = '\0'; // Null-terminate
        (*total_chunks)++;
        if (*total_chunks >= MAX_CHUNKS)
        {
            printf("Too many chunks, truncating message.\n");
            break;
        }
    }
}

void send_message(int sockfd, struct sockaddr_in *addr, char *message)
{
    struct chunk chunks[MAX_CHUNKS];
    int total_chunks;
    int acknowledgements[MAX_CHUNKS] = {0};
    struct timeval send_times[MAX_CHUNKS];
    socklen_t addr_len = sizeof(*addr);

    split_message_into_chunks(message, chunks, &total_chunks);

    shuffle(chunks, total_chunks);
    sendto(sockfd, &total_chunks, sizeof(total_chunks), 0, (struct sockaddr *)addr, addr_len);

    send_chunks(sockfd, addr, chunks, total_chunks);

    for (int i = 0; i < total_chunks; i++)
    {
        gettimeofday(&send_times[i], NULL);
    }

    while (1)
    {

        receive_acknowledgement(sockfd, acknowledgements, total_chunks);

        check_timeouts_and_resend(sockfd, addr, chunks, acknowledgements, send_times, total_chunks);

        if (all_chunks_acknowledged(acknowledgements, total_chunks))
        {
            printf("All chunks sent and acknowledged.\n");
            break;
        }

        usleep(60000);
    }
}

void receive_messages(int sockfd)
{
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    struct chunk received_chunk;
    int total_chunks;
    struct chunk chunks[MAX_CHUNKS];
    memset(chunks, 0, sizeof(chunks));
    int received_flags[MAX_CHUNKS] = {0};
    recvfrom(sockfd, &total_chunks, sizeof(total_chunks), 0, (struct sockaddr *)&client_addr, &addr_len);
    printf("Total number of chunks: %d\n", total_chunks);

    while (1)
    {
        struct chunk recv_chunk;
        recvfrom(sockfd, &recv_chunk, sizeof(recv_chunk), 0, (struct sockaddr *)&client_addr, &addr_len);

        if (recv_chunk.sequence_number < total_chunks)
        {
            recv_chunk.data[CHUNK_SIZE] = '\0';

            if (!received_flags[recv_chunk.sequence_number]) //
            {

                chunks[recv_chunk.sequence_number] = recv_chunk;
                received_flags[recv_chunk.sequence_number] = 1;
                printf("Received chunk %d: %s\n", recv_chunk.sequence_number, recv_chunk.data);
            }

            struct chunk ack;
            ack.sequence_number = recv_chunk.sequence_number;
            sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&client_addr, addr_len);
            printf("Sent ACK for chunk %d\n", recv_chunk.sequence_number);

            int all_received = 1;
            for (int i = 0; i < total_chunks; i++)
            {
                if (!received_flags[i])
                {
                    all_received = 0;
                    break;
                }
            }

            if (all_received)
            {
                break;
            }
        }
    }

    char message[CHUNK_SIZE * MAX_CHUNKS + 1] = {0};
    for (int i = 0; i < total_chunks; i++)
    {
        strncat(message, chunks[i].data, CHUNK_SIZE);
    }

    printf("server: %s\n", message);
}
