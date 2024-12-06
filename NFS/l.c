#include "l.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Function to create a new node with string data (with MAX_PATH) and associated server index
Node* createNode(const char *data, int server_index) {
    Node *newNode = (Node*)malloc(sizeof(Node));
    if (!newNode) {
        perror("Node allocation failed");
        return NULL;
    }

    // Allocate memory for the data (file path) manually using malloc
    newNode->data = (char *)malloc(strlen(data) + 1);  // +1 for null terminator
    if (newNode->data == NULL) {
        perror("Data allocation failed");
        free(newNode);
        return NULL;
    }
    strcpy(newNode->data, data);  // Copy the data into the allocated space
    
    newNode->server_index = server_index;  // Store the server index
    newNode->prev = newNode->next = NULL;
    return newNode;
}

// Function to initialize the LRU cache
LRUCache* createLRUCache(int capacity) {
    LRUCache *cache = (LRUCache*)malloc(sizeof(LRUCache));
    if (!cache) {
        perror("Cache allocation failed");
        return NULL;
    }
    cache->capacity = capacity;
    cache->size = 0;
    cache->head = NULL;
    cache->tail = NULL;
    return cache;
}

// Function to move a node to the front of the list (most recently used)
void moveToFront(LRUCache *cache, Node *node) {
    if (cache->head == node) {
        return; // Already at the front
    }
    // Remove node from its current position
    if (node->prev) {
        node->prev->next = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    }
    
    // If it's the tail node, update the tail
    if (cache->tail == node) {
        cache->tail = node->prev;
    }
    
    // Place the node at the front (head)
    node->next = cache->head;
    node->prev = NULL;
    if (cache->head) {
        cache->head->prev = node;
    }
    cache->head = node;
    
    if (cache->tail == NULL) { // If the list was empty
        cache->tail = node;
    }
}

// Function to add a new string (file path) and server index to the cache
void insert(LRUCache *cache, const char *data, int server_index) {
    // Check if the data already exists in the cache
    Node *curr = cache->head;
    while (curr) {
        if (strcmp(curr->data, data) == 0) {
            // Data already in the cache, move it to the front
            moveToFront(cache, curr);
            return;
        }
        curr = curr->next;
    }

    // Create a new node for the string with associated server index
    Node *newNode = createNode(data, server_index);
    
    // If the cache is full, evict the least recently used entry
    if (cache->size == cache->capacity) {
        // Evict the tail node
        Node *tailNode = cache->tail;
        cache->tail = tailNode->prev;
        if (cache->tail) {
            cache->tail->next = NULL;
        }
        free(tailNode->data);
        free(tailNode);
        cache->size--;
    }

    // Add the new node to the front of the list (most recent)
    if (cache->size == 0) {
        cache->head = cache->tail = newNode;
    } else {
        newNode->next = cache->head;
        cache->head->prev = newNode;
        cache->head = newNode;
    }

    cache->size++;
}

// Function to search a string from the cache (move it to the front if found)
// Return the server index if found, otherwise return -1
int scn(LRUCache *cache, const char *data) {
    Node *curr = cache->head;
    while (curr) {
        if (strcmp(curr->data, data) == 0) {
            moveToFront(cache, curr);  // Move it to the front as most recent
            return curr->server_index; // Return the associated server index
        }
        curr = curr->next;
    }
    return -1; // Data not found, return -1
}

// Function to print the contents of the cache
void printCache(LRUCache *cache) {
    Node *curr = cache->head;
    printf("Cache (most recent -> least recent):\n");
    while (curr) {
        printf("%s (Server index: %d) ", curr->data, curr->server_index);
        curr = curr->next;
    }
    printf("\n");
}

void deleteCh(LRUCache *cache, const char *data) {
    Node *curr = cache->head;

    // Traverse the list to find the node
    while (curr) {
        // If the data matches
        if (strcmp(curr->data, data) == 0) {
            // If node is the head
            if (curr == cache->head) {
                cache->head = curr->next;
                if (cache->head) {
                    cache->head->prev = NULL; // Set the new head's prev pointer to NULL
                }
            }

            // If node is the tail
            if (curr == cache->tail) {
                cache->tail = curr->prev;
                if (cache->tail) {
                    cache->tail->next = NULL; // Set the new tail's next pointer to NULL
                }
            }

            // If the node is in the middle
            if (curr->prev) {
                curr->prev->next = curr->next; // Link the previous node to the next node
            }
            if (curr->next) {
                curr->next->prev = curr->prev; // Link the next node to the previous node
            }

            // Free the memory of the node and its data
            free(curr->data);  // Free the dynamically allocated memory for the node's data
            free(curr);         // Free the node itself

            cache->size--;  // Decrease the cache size
            return;  // Node found and deleted, exit the function
        }
        curr = curr->next;  // Move to the next node in the list
    }

    printf("Data not found in cache\n");  // If the data is not found in the cache
}


// Function to free all memory used by the cache
void freeCache(LRUCache *cache) {
    Node *curr = cache->head;
    while (curr) {
        Node *temp = curr;
        curr = curr->next;
        free(temp->data);
        free(temp);
    }
    free(cache);
}
