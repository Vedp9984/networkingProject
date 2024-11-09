
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

void display_board(int client_fd)
{
    char display[100] = "";
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
    send(client_fd, display, strlen(display), 0);
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

void play_game(int client1_fd, int client2_fd)
{
    int player = 1; // Player 1 starts
    // printf("khjljl");
    while (1)
    {
        display_board(player == 1 ? client1_fd : client2_fd);
        int row, col;
        int client_fd = player == 1 ? client1_fd : client2_fd;

        // Inform current player

        char msg[50];
        sprintf(msg, "Player %d's turn. Enter row and column (0-2): ", player);
        send(client_fd, msg, strlen(msg), 0);

        // Get the move
        
        recv(client_fd, msg, sizeof(msg), 0);
        sscanf(msg, "%d %d", &row, &col);

        // Validate move
        if (row >= 0 && row < SIZE && col >= 0 && col < SIZE && board[row][col] == ' ')
        {
            board[row][col] = current_player;
            if (check_winner())
            {
                display_board(client1_fd);
                display_board(client2_fd);
                sprintf(msg, "Player %d wins!\n", player);
                send(client1_fd, msg, strlen(msg), 0);
                send(client2_fd, msg, strlen(msg), 0);
                break;
                return;
            }
            if (is_draw())
            {
                display_board(client1_fd);
                display_board(client2_fd);
                sprintf(msg, "It's a draw!\n");
                send(client1_fd, msg, strlen(msg), 0);
                send(client2_fd, msg, strlen(msg), 0);
                break;
                return;
            }
            current_player = (current_player == 'X') ? 'O' : 'X';
            player = (player == 1) ? 2 : 1; // Switch player
        }
        else
        {
            send(client_fd, "Invalid move. Try again.\n", 25, 0);
        }
    }
}

int main()
{
    int server_fd, client1_fd, client2_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Bind the socket
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_fd, 2) < 0)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Waiting for players to connect...\n");

    // Accept connections
    client1_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
    if (client1_fd < 0)
    {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }
    printf("Player 1 connected\n");

    client2_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
    if (client2_fd < 0)
    {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }
    printf("Player 2 connected\n");

    // Game loop
    // int play_again = 1;
    while (1)
    {
        initialize_board();
        current_player = 'X';
        // printf("67868\n");
        play_game(client1_fd, client2_fd);

        // Ask if they want to play again
        char play_again_msg[] = "Do you want to play again? (yes/no)\n";
        send(client1_fd, play_again_msg, strlen(play_again_msg), 0);
        send(client2_fd, play_again_msg, strlen(play_again_msg), 0);

        char response1[10] = {0}, response2[10] = {0};
        recv(client1_fd, response1, sizeof(response1), 0);
        recv(client2_fd, response2, sizeof(response2), 0);

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
            
            if (strcmp(response1, "no") == 0 && strcmp(response2, "no") == 0)
            {
                send(client1_fd, "Goodbye! Thanks for playing.\n", 30, 0);
                send(client2_fd, "Goodbye! Thanks for playing.\n", 30, 0);
            }
            else
            {
                if (strcmp(response1, "yes") == 0)
                {
                    send(client1_fd, "Your opponent does not want to play again. Thanks for playing till now Goodbye!\n", 53, 0);
                }
                else
                {
                    send(client2_fd, "Your opponent does not want to play again. Thanks for playing till now Goodbye!\n", 53, 0);
                }
            }
            break;
        }
    }


    close(client1_fd);
    close(client2_fd);
    close(server_fd);
    return 0;
}
