#include "headers.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_ASCII 128  // Adjust for full ASCII characters, including '/'

// Create a new TrieNode
TrieNode* createTrieNode() {
    TrieNode* node = (TrieNode*)malloc(sizeof(TrieNode));
    if (node) {
        for (int i = 0; i < MAX_ASCII; i++) {
            node->children[i] = NULL;
        }
        node->server_index = -1;  // No server assigned
    }
    return node;
}

// Insert a path into the trie with the given server index
void insertTrie(TrieNode* root, const char* path, int server_index) {
    TrieNode* current = root;
    for (int i = 0; path[i] != '\0'; i++) {
        char c = path[i];
        int index = (int)c;  // Use ASCII value directly for indexing

        if (index < 0 || index >= MAX_ASCII) continue;  // Skip out-of-range characters

        if (current->children[index] == NULL) {
            current->children[index] = createTrieNode();
        }
        current = current->children[index];
    }
    current->server_index = server_index;  // Assign storage server index
}

// Search for a path in the trie and return the server index, or -1 if not found
int searchTrie(TrieNode* root, const char* path) {
    TrieNode* current = root;

    // Traverse through the path character by character
    for (int i = 0; path[i] != '\0'; i++) {
        char c = path[i];
        int index = (int)c;  // Use ASCII value directly for indexing

        // If a child node is not found, return "not found"
        if (index < 0 || index >= MAX_ASCII || current->children[index] == NULL) {
            return -1;  // Path not found
        }
        current = current->children[index];
    }

    // After traversing the entire path, check if the node has:
    // 1. A valid server index (exact match), or
    // 2. Children (prefix match)
    
    if (current->server_index != -1) {
        // Exact match found
        return current->server_index;  // Return the server index associated with the exact path
    }

    // If there are children, it means the path is a prefix and there are files beneath it
    for (int i = 0; i < MAX_ASCII; i++) {
        if (current->children[i] != NULL) {
            return 0;  // Prefix found, files exist beneath this path
        }
    }

    // If no server index and no children, return "not found"
    return -1;
}


// Helper function to print the trie contents recursively
void printTrieHelper(TrieNode* node, char* buffer, int depth) {
    if (node->server_index != -1) {
        buffer[depth] = '\0';
        printf("Path: %s, Server Index: %d\n", buffer, node->server_index);
    }
    for (int i = 0; i < MAX_ASCII; i++) {
        if (node->children[i] != NULL) {
            buffer[depth] = (char)i;
            printTrieHelper(node->children[i], buffer, depth + 1);
        }
    }
}

// Print the entire trie starting from the root
void printTrie(TrieNode* root) {
    char buffer[256];  // Buffer to hold paths
    printTrieHelper(root, buffer, 0);
}

// Free the entire trie recursively
void freeTrie(TrieNode* root) {
    for (int i = 0; i < MAX_ASCII; i++) {
        if (root->children[i] != NULL) {
            freeTrie(root->children[i]);
        }
    }
    free(root);
}

// Helper function to delete a specific path from the trie
int deleteTrieHelper(TrieNode* node, const char* path, int depth) {
    if (node == NULL) {
        return 0;
    }

    // Base case: if we've reached the end of the path
    if (path[depth] == '\0') {
        if (node->server_index != -1) {
            node->server_index = -1;  // Mark this node as no longer in use
            // Check if this node has any children
            for (int i = 0; i < MAX_ASCII; i++) {
                if (node->children[i] != NULL) {
                    return 0;  // Do not delete if there are children
                }
            }
            return 1;  // Indicate this node can be deleted
        }
        return 0;  // Path was not found
    }

    int index = (int)path[depth];
    if (deleteTrieHelper(node->children[index], path, depth + 1)) {
        free(node->children[index]);
        node->children[index] = NULL;

        // Check if this node can now be deleted
        if (node->server_index == -1) {
            for (int i = 0; i < MAX_ASCII; i++) {
                if (node->children[i] != NULL) {
                    return 0;  // Do not delete if there are other children
                }
            }
            return 1;  // Indicate this node can be deleted
        }
    }
    return 0;
}

// Function to delete a specific path from the trie
void deleteTrie(TrieNode* root, const char* path) {
    deleteTrieHelper(root, path, 0);
}
