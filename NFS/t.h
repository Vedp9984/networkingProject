#ifndef TRIE_H
#define TRIE_H

#define MAX_ASCII 128

typedef struct TrieNode
{
    struct TrieNode *children[MAX_ASCII];
    int server_index;
} TrieNode;

TrieNode *createTrieNode();

void insertTrie(TrieNode *root, const char *path, int server_index);

int searchTrie(TrieNode *root, const char *path);

// Helper function to print all paths and their associated server indices in the Trie
void printTrie(TrieNode *root);

// Recursive helper to release allocated memory in the Trie
void freeTrie(TrieNode *root);

void deleteTrie(TrieNode *root, const char *path);

#endif
