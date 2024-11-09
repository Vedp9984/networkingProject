// tic_tac_toe_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define SIZE 3

char board[SIZE][SIZE];
char current_player = 'X';

void initialize_board()
{
    for (int i = 0; i < SIZE; i++)
    {
        for (int j = 0; j < SIZE; j++)
        {
            board[i][j] = ' ';
        }
    }
}

void display_board(char *display)
{
    sprintf(display, "Current board:\n");
    for (int i = 0; i < SIZE; i++)
    {
        for (int j = 0; j < SIZE; j++)
        {
            sprintf(display + strlen(display), " %c ", board[i][j]);
            if (j < SIZE - 1)
            {
                sprintf(display + strlen(display), "|");
            }
        }
        sprintf(display + strlen(display), "\n");
        if (i < SIZE - 1)
        {
            sprintf(display + strlen(display), "-----------\n");
        }
    }
}

int check_winner()
{
    for (int i = 0; i < SIZE; i++)
    {
        if (board[i][0] == current_player && board[i][1] == current_player && board[i][2] == current_player)
            return 1;
        if (board[0][i] == current_player && board[1][i] == current_player && board[2][i] == current_player)
            return 1;
    }
    if (board[0][0] == current_player && board[1][1] == current_player && board[2][2] == current_player)
        return 1;
    if (board[0][2] == current_player && board[1][1] == current_player && board[2][0] == current_player)
        return 1;
    return 0;
}

int is_draw()
{
    for (int i = 0; i < SIZE; i++)
        for (int j = 0; j < SIZE; j++)
            if (board[i][j] == ' ')
                return 0; // Not a draw
    return 1;             // Draw
}

void play_game(int sockfd, struct sockaddr_in *client1_addr, struct sockaddr_in *client2_addr, socklen_t addr_len)
{
    int player = 1; // Player 1 starts
    char display[100] = "";
    char msg[50] = "";
    char buffer[1024] = "";

    while (1)
    {
        memset(display, 0, sizeof(display));
        display_board(display);

        struct sockaddr_in *client_addr = player == 1 ? client1_addr : client2_addr;

        // Send current board to both players
        sendto(sockfd, display, strlen(display), 0, (struct sockaddr *)client1_addr, addr_len);
        sendto(sockfd, display, strlen(display), 0, (struct sockaddr *)client2_addr, addr_len);

        // Inform current player
        sprintf(msg, "Player %d's turn. Enter row and column (0-2): ", player);
        sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr *)client_addr, addr_len);

        // Get the move
        recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)client_addr, &addr_len);
        int row, col;
        sscanf(buffer, "%d %d", &row, &col);

        // Validate move
        if (row >= 0 && row < SIZE && col >= 0 && col < SIZE && board[row][col] == ' ')
        {
            board[row][col] = current_player;
            if (check_winner())
            {
                display_board(display);
                sendto(sockfd, display, strlen(display), 0, (struct sockaddr *)client1_addr, addr_len);
                sendto(sockfd, display, strlen(display), 0, (struct sockaddr *)client2_addr, addr_len);
                sprintf(msg, "Player %d wins!\n", player);
                sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr *)client1_addr, addr_len);
                sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr *)client2_addr, addr_len);
                break;
            }
            if (is_draw())
            {
                display_board(display);
                sendto(sockfd, display, strlen(display), 0, (struct sockaddr *)client1_addr, addr_len);
                sendto(sockfd, display, strlen(display), 0, (struct sockaddr *)client2_addr, addr_len);
                sprintf(msg, "It's a draw!\n");
                sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr *)client1_addr, addr_len);
                sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr *)client2_addr, addr_len);
                break;
            }
            current_player = (current_player == 'X') ? 'O' : 'X';
            player = (player == 1) ? 2 : 1;
        }
        else
        {
            sendto(sockfd, "Invalid move. Try again.\n", 25, 0, (struct sockaddr *)client_addr, addr_len);
        }
    }
}

int main()
{
    int sockfd;
    struct sockaddr_in server_addr, client1_addr, client2_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    // Create socket

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

  
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Waiting for players to connect...\n");

    // Receive connection from player 1
    recvfrom(sockfd, NULL, 0, 0, (struct sockaddr *)&client1_addr, &addr_len);
    printf("Player 1 connected\n");

    // Receive connection from player 2
    recvfrom(sockfd, NULL, 0, 0, (struct sockaddr *)&client2_addr, &addr_len);
    printf("Player 2 connected\n");

    // Game loop
    while (1)
    {
        initialize_board();
        current_player = 'X';
        play_game(sockfd, &client1_addr, &client2_addr, addr_len);

        // Ask if they want to play again
        char play_again_msg[] = "Do you want to play again?(yes/no)\n";
        sendto(sockfd, play_again_msg, strlen(play_again_msg), 0, (struct sockaddr *)&client1_addr, addr_len);
        sendto(sockfd, play_again_msg, strlen(play_again_msg), 0, (struct sockaddr *)&client2_addr, addr_len);

        char response1[10] = {0}, response2[10] = {0};
        recvfrom(sockfd, response1, sizeof(response1), 0, (struct sockaddr *)&client1_addr, &addr_len);
        recvfrom(sockfd, response2, sizeof(response2), 0, (struct sockaddr *)&client2_addr, &addr_len);

        // Remove newlines from responses
        response1[strcspn(response1, "\n")] = 0;
        response2[strcspn(response2, "\n")] = 0;

        // Check the responses
        if (strcmp(response1, "yes") == 0 && strcmp(response2, "yes") == 0)
        {
            // Both players want to play again
            continue;
        }
        else
        {
            // At least one player does not want to play again
            if (strcmp(response1, "no") == 0 && strcmp(response2, "no") == 0)
            {
                sendto(sockfd, "Goodbye! Thanks for playing.\n", 30, 0, (struct sockaddr *)&client1_addr, addr_len);
                sendto(sockfd, "Goodbye! Thanks for playing.\n", 30, 0, (struct sockaddr *)&client2_addr, addr_len);
            }
            else
            {
                if (strcmp(response1, "yes") == 0)
                {
                    sendto(sockfd, "Your opponent does not want to play again. Thanks for playing till now. Goodbye!\n", 85, 0, (struct sockaddr *)&client1_addr, addr_len);
                    sendto(sockfd, "Thanks for playing till now. Goodbye!\n", 85, 0, (struct sockaddr *)&client2_addr, addr_len);
                }
                else
                {
                    sendto(sockfd, "Your opponent does not want to play again. Thanks for playing till now. Goodbye!\n", 85, 0, (struct sockaddr *)&client2_addr, addr_len);
                    sendto(sockfd, " Thanks for playing till now. Goodbye!\n", 85, 0, (struct sockaddr *)&client1_addr, addr_len);
                }
            }
            break;
        }
    }
    close(sockfd);
    return 0;
}
