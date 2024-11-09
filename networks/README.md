# networking

# Tic-Tac-Toe Game using TCP

## Overview

This project implements a networked Tic-Tac-Toe game using TCP sockets in C. The game allows two players to play Tic-Tac-Toe over a network connection.

## Features

- Two-player Tic-Tac-Toe game
- Network communication using TCP sockets
- Handles game state and communication between client and server
- Option to play multiple rounds

## Requirements and Assumptions

- Both server and clients are on the same network or can reach each other over the network.
- The default port `8080` is not used by other applications. Modify the `PORT` in the code if necessary.
- Players will input valid row and column numbers (0-2).
- Both server and client code are compiled and run on machines with network capabilities.


# using UDP

## Features
- Two-player Tic-Tac-Toe game
- Network communication using UDP sockets
- Handles game state and communication between client and server
- Option to play multiple rounds

 Requirements and Assumptions
- Both server and clients are on the same network or can reach each other over the network.
- The default port `8080` is not used by other applications. Modify the `PORT` in the code if necessary.
- Players will input valid row and column numbers (0-2).
- Both server and client code are compiled and run on machines with network capabilities.



# partB

- I have implemented the sending messege in alternate way 
- both way works but only for once or it is non deterministic sometime works sometime doesnot work
- I am suffling tha packets before sending 
- reciever get full messege by using sequince number