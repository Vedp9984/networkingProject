// tic_tac_toe_client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

// #include "game/game.h"

#define PORT 8080

int main()
{
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};

    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

   
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
    {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

 
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("\nConnection Failed \n");
        return -1;
    }

    while (1)
    {
        memset(buffer, 0, sizeof(buffer)); // Clear the buffer
        int read_bytes = read(sock, buffer, sizeof(buffer) - 1);

        if (read_bytes <= 0)
        {
            printf("Server disconnected\n");
            break;
        }

        buffer[read_bytes] = '\0'; 

        if (strstr(buffer, "turn") != NULL)
        {
            printf("%s", buffer);
            int row, col;
            scanf("%d %d", &row, &col);
            sprintf(buffer, "%d %d", row, col);
            send(sock, buffer, strlen(buffer), 0);
        }
        else if (strstr(buffer, "Do you want to play again? (yes/no)") != NULL)
        {
            char response[10];
            printf("Do you want to play again? (yes/no): ");
            scanf("%s", response);
            send(sock, response, strlen(response), 0);
        }
        else if (strstr(buffer, "Goodbye!") != NULL)
        {
            printf("%s", buffer);
            break;
        }
        else
        {
            printf("%s", buffer);
        }
    }

    // Close socket
    close(sock);
    return 0;
}
