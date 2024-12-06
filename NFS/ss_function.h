#ifndef SS_FUNCTION_H
#define SS_FUNCTION_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <pthread.h>
#include <time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>

#define BUFFER_SIZE 1024

struct FileMetadata
{
  char file_path[BUFFER_SIZE];
  long long file_size;
  int access_rights;
  char last_accessed[BUFFER_SIZE];
  char last_modified[BUFFER_SIZE];
  char last_status_change[BUFFER_SIZE];
};

struct proc
{
  pid_t pid;
  struct FileMetadata metadata;
};

void sendack(int socket, const char *message);

void sendErrorCode(int socket, int errorCode);

const char *errorCodeToMessage(int errorCode);

void sendErrorMessage(int socket, int errorCode);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~NM intraction~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

int createFileOrDirectory(int sock, const char *path, const char *name, int kya);
int deleteFileOrDirectory(int sock, const char *path);
int copyDirectory(const char *src_dir, const char *dst_dir, int sock);
int copyPath(const char *src, const char *dst, int sock);
int copyFile(const char *src, const char *dst, int sock);
char *get_ip_address();
void *asyncWrite(void *arg);
int writeFile_with_sync_and_async(const char *path, int socket, const char *data, int syncFlag);
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~client intraction~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

int readFile(const char *path, int socket);
int writeFile(const char *path, int socket, const char *data);
int getFileSize(const char *path, int socket);
int getFilePermissions(const char *path, int socket);
void get_file_metadata(const char *file_path, struct FileMetadata *metadata, int sock);

int streamAudioFile(const char *path, int socket);

#endif
