#ifndef LRU_H
#define LRU_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "headers.h"

// Node structure for LRU cache
typedef struct Node {
    char *data;          // File path (string)
    int server_index;    // Associated server index
    struct Node *prev;   // Pointer to the previous node
    struct Node *next;   // Pointer to the next node
} Node;

// LRU Cache structure
typedef struct {
    int capacity;    // Maximum capacity of the cache
    int size;        // Current size of the cache
    Node *head;      // Head of the doubly linked list (most recent)
    Node *tail;      // Tail of the doubly linked list (least recent)
} LRUCache;

// Function declarations
Node* createNode(const char *data, int server_index);
LRUCache* createLRUCache(int capacity);
void moveToFront(LRUCache *cache, Node *node);
void insert(LRUCache *cache, const char *data, int server_index);
int scn(LRUCache *cache, const char *data);
void printCache(LRUCache *cache);
void freeCache(LRUCache *cache);
void deleteCh(LRUCache *cache, const char *data);

#endif // LRU_H