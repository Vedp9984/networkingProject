#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

void handle_turn(int sockfd, struct sockaddr_in *server_addr, socklen_t addr_len)
{
    char buffer[BUFFER_SIZE];
    int row, col;

    printf("Your turn. Enter row and column (0-2): ");
    scanf("%d %d", &row, &col);
    sprintf(buffer, "%d %d", row, col);
    sendto(sockfd, buffer, strlen(buffer), 0, (const struct sockaddr *)server_addr, addr_len);
}

void handle_play_again(int sockfd, struct sockaddr_in *server_addr, socklen_t addr_len)
{
    char msg[BUFFER_SIZE];
    scanf("%s", msg);
    sendto(sockfd, msg, strlen(msg), 0, (const struct sockaddr *)server_addr, addr_len);
}

int main()
{
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(server_addr);

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Replace with actual server IP if different

    const char *init_message = "Hello Server";
    sendto(sockfd, init_message, strlen(init_message), 0, (const struct sockaddr *)&server_addr, addr_len);
    printf("Connected to the server. Waiting for opponent...\n");

    while (1)
    {
        memset(buffer, 0, BUFFER_SIZE);
        recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&server_addr, &addr_len);
        printf("%s", buffer);

        if (strstr(buffer, "turn") != NULL)
        {
            handle_turn(sockfd, &server_addr, addr_len);
        }

        if (strstr(buffer, "Do you want to play again?") != NULL)
        {
            handle_play_again(sockfd, &server_addr, addr_len);
        }
        else if (strstr(buffer, "Your opponent does not want to play again.") != NULL ||
                 strstr(buffer, "Goodbye!") != NULL)
        {
            break;
        }
    }

    close(sockfd);
    return 0;
}
