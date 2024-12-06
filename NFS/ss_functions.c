
#include "ss_function.h"
#include <netdb.h>
#define PATH_MAX 4096

// IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII~~error_handling~~IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII

#define SUCCESS 0
#define ERR_CREATING_FILE -1
#define ERR_CREATING_DIR -2
#define ERR_FILE_EXISTS -3
#define ERR_DIR_EXISTS -4
#define ERR_OPENING_FILE -5
#define ERR_WRITING_FILE -6
#define ERR_READING_FILE -7
#define ERR_GETTING_FILE_SIZE -8
#define ERR_GETTING_PERMISSIONS -9
#define ERR_OPENING_DIR -10
#define ERR_STREAMING_FILE -11
#define ERR_MPG123_NOT_INSTALLED -12
#define ERR_UNKNOWN_PATH_TYPE -13
#define ERR_UNKNOWN -14

/********************************************************************************************************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>

void sendack(int socket, const char *message)
{
  send(socket, message, strlen(message), 0);
  printf("Acknowledgment sent: %s\n", message);
}

void sendErrorCode(int socket, int errorCode)
{

  ssize_t bytesSent = send(socket, &errorCode, sizeof(int), 0);
  if (bytesSent == -1)
  {
    perror("Error sending error code");
  }
}

const char *errorCodeToMessage(int errorCode)
{
  switch (errorCode)
  {
  case SUCCESS:
    return "Success";
  case ERR_CREATING_FILE:
    return "Error creating file";
  case ERR_CREATING_DIR:
    return "Error creating directory";
  case ERR_FILE_EXISTS:
    return "File already exists";
  case ERR_DIR_EXISTS:
    return "Directory already exists";
  case ERR_OPENING_FILE:
    return "Error opening file";
  case ERR_WRITING_FILE:
    return "Error writing to file";
  case ERR_READING_FILE:
    return "Error reading file";
  case ERR_GETTING_FILE_SIZE:
    return "Error getting file size";
  case ERR_GETTING_PERMISSIONS:
    return "Error getting permissions";
  case ERR_OPENING_DIR:
    return "Error opening directory";
  case ERR_STREAMING_FILE:
    return "Error streaming file";
  case ERR_MPG123_NOT_INSTALLED:
    return "mpg123 is not installed";
  case ERR_UNKNOWN_PATH_TYPE:
    return "Unknown path type";
  default:
    return "Unknown error";
  }
}

void sendErrorMessage(int socket, int errorCode)
{
  const char *errorMessage = errorCodeToMessage(errorCode);
  ssize_t bytesSent = send(socket, errorMessage, strlen(errorMessage) + 1, 0);
  if (bytesSent == -1)
  {
    perror("Error sending error message");
  }
}

typedef struct
{
  char *path;
  char *data;
  int socket;
  size_t dataLength;
  int priority;
} WriteRequest;

typedef struct
{
  WriteRequest *requests[20];
  int size;
  pthread_mutex_t lock;
  pthread_cond_t cond;
} PriorityQueue;

PriorityQueue *pq;
PriorityQueue *createPriorityQueue()
{
  PriorityQueue *pq = malloc(sizeof(PriorityQueue));
  pq->size = 0;
  pthread_mutex_init(&pq->lock, NULL);
  pthread_cond_init(&pq->cond, NULL);
  return pq;
}

void insertRequest(PriorityQueue *pq, WriteRequest *request)
{
  pthread_mutex_lock(&pq->lock);
  if (pq->size < 20)
  {
    pq->requests[pq->size++] = request;
    for (int i = pq->size - 1; i > 0 && pq->requests[i]->priority < pq->requests[i - 1]->priority; i--)
    {
      WriteRequest *temp = pq->requests[i];
      pq->requests[i] = pq->requests[i - 1];
      pq->requests[i - 1] = temp;
    }
  }
  pthread_cond_signal(&pq->cond);
  pthread_mutex_unlock(&pq->lock);
}

WriteRequest *removeHighestPriorityRequest(PriorityQueue *pq)
{
  pthread_mutex_lock(&pq->lock);
  while (pq->size == 0)
  {
    pthread_cond_wait(&pq->cond, &pq->lock);
  }
  WriteRequest *highestPriorityRequest = pq->requests[0];
  for (int i = 1; i < pq->size; i++)
  {
    pq->requests[i - 1] = pq->requests[i];
  }
  pq->size--;
  pthread_mutex_unlock(&pq->lock);
  return highestPriorityRequest;
}

void *processWriteRequests(void *arg)
{
  while (1)
  {
    WriteRequest *request = removeHighestPriorityRequest(pq);
    if (request)
    {
      FILE *file = fopen(request->path, "a");
      if (!file)
      {
        sendErrorMessage(request->socket, 2);
        free(request->path);
        free(request->data);
        free(request);
        continue;
      }
      fwrite(request->data, sizeof(char), request->dataLength, file);
      fclose(file);
      sendack(request->socket, "Asynchronous write completed successfully.");
      free(request->path);
      free(request->data);
      free(request);
    }
    else
    {
      usleep(100000);
    }
  }
}

void *writeFileThread(void *arg)
{
  WriteRequest *request = (WriteRequest *)arg;
  FILE *file = fopen(request->path, "a");
  if (!file)
  {
    sendErrorMessage(request->socket, 2);
    free(request->path);
    free(request->data);
    free(request);
    return NULL;
  }
  size_t totalDataLength = request->dataLength;
  size_t writtenBytes = 0;
  char buffer[BUFFER_SIZE];
  size_t bufferOffset = 0;
  while (writtenBytes < totalDataLength)
  {
    size_t bytesToWrite = (totalDataLength - writtenBytes > BUFFER_SIZE) ? BUFFER_SIZE : (totalDataLength - writtenBytes);
    memcpy(buffer + bufferOffset, request->data + writtenBytes, bytesToWrite);
    bufferOffset += bytesToWrite;
    writtenBytes += bytesToWrite;
    size_t result = fwrite(buffer, sizeof(char), bufferOffset, file);
    if (result < bufferOffset)
    {
      perror("Partial write error");
      break;
    }
    bufferOffset = 0;
    usleep(100000);
  }
  fclose(file);
  sendack(request->socket, "Write completed successfully");
  free(request->path);
  free(request->data);
  free(request);
  return NULL;
}

int writeFile_with_sync_and_async(const char *path, int socket, const char *data, int syncFlag)
{
  static pthread_mutex_t syncWriteLock = PTHREAD_MUTEX_INITIALIZER;
  static int isInitialized = 0;
  if (!isInitialized)
  {
    pq = createPriorityQueue();
    pthread_t workerThread;
    pthread_create(&workerThread, NULL, processWriteRequests, NULL);
    pthread_detach(workerThread);
    isInitialized = 1;
  }
  if (syncFlag)
  {
    pthread_mutex_lock(&syncWriteLock);
    FILE *file = fopen(path, "a");
    if (file)
    {
      fwrite(data, sizeof(char), strlen(data), file);
      fclose(file);
      sendack(socket, "Synchronous write completed successfully.");
    }
    else
    {
      sendack(socket, "Failed to open file for synchronous writing.");
    }
    pthread_mutex_unlock(&syncWriteLock);
    return 1; // Return appropriate status
  }
  else
  {
    WriteRequest *request = malloc(sizeof(WriteRequest));
    if (!request)
    {
      sendack(socket, "Memory allocation failed.");
      return -1;
    }
    request->path = strdup(path);
    request->data = strdup(data);
    request->socket = socket;
    request->dataLength = strlen(data);
    request->priority = 2;
    if (!request->path || !request->data)
    {
      free(request->path);
      free(request->data);
      free(request);
      sendack(socket, "Memory allocation failed.");
      return -2;
    }
    insertRequest(pq, request);
    sendack(socket, "Asynchronously writing data to file");
  }
  return 0;
}

char *get_ip_address()
{
  static char ip_address[INET_ADDRSTRLEN];
  struct ifaddrs *ifaddr, *ifa;
  int family, s;

  // Get a list of all interfaces
  if (getifaddrs(&ifaddr) == -1)
  {
    perror("getifaddrs");
    exit(EXIT_FAILURE);
  }

  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
  {
    if (ifa->ifa_addr == NULL)
      continue;

    family = ifa->ifa_addr->sa_family;

    // Only look at IPv4 addresses
    if (family == AF_INET)
    {
      s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                      ip_address, sizeof(ip_address),
                      NULL, 0, NI_NUMERICHOST);
      if (s != 0)
      {
        printf("getnameinfo() failed: %s\n", gai_strerror(s));
        exit(EXIT_FAILURE);
      }

      // Avoid loopback address
      if (strcmp(ifa->ifa_name, "lo") != 0)
      {
        break;
      }
    }
  }

  // Free the list of interfaces
  freeifaddrs(ifaddr);

  return ip_address;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII~~~naming_server's intractions~~~IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII

int createFileOrDirectory(int sock, const char *path, const char *name, int kya)
{
  char full_path[1024];
  snprintf(full_path, sizeof(full_path), "%s/%s", path, name);
  if (full_path[strlen(full_path) - 1] == '/')
  {
    full_path[strlen(full_path) - 1] = '\0';
  }

  struct stat path_stat;
  int result = stat(full_path, &path_stat);
  if (result == 0)
  {
    if (S_ISDIR(path_stat.st_mode))
    {
      sendErr
      orMessage(sock, ERR_DIR_EXISTS); // Send error message for directory already exists
      return ERR_DIR_EXISTS;                  // Directory already exists
    }
    else if (S_ISREG(path_stat.st_mode))
    {
      sendErrorMessage(sock, ERR_FILE_EXISTS); // Send error message for file already exists
      return ERR_FILE_EXISTS;                  // File already exists
    }
    else
    {
      sendErrorMessage(sock, ERR_UNKNOWN_PATH_TYPE); // Send error message for unknown path type
      return ERR_UNKNOWN_PATH_TYPE;                  // Unknown path type
    }
  }
  else if (errno != ENOENT)
  {
    perror("Error checking path");
    sendErrorMessage(sock, -errno); // Send error message for error checking the path
    return -errno;                  // Some error occurred while checking the path
  }

  if (kya == 1)
  {
    int fd = open(full_path, O_CREAT | O_EXCL | O_WRONLY, 0666);
    if (fd == -1)
    {
      perror("Error creating file");
      sendErrorMessage(sock, ERR_CREATING_FILE); // Send error message for error creating file
      return ERR_CREATING_FILE;                  // Error while creating the file
    }
    else
    {
      close(fd); // Close the file descriptor
      sendack(sock, "success");
      return SUCCESS; // File created successfully
    }
  }
  else if (kya == 0)
  {
    // Create a directory
    if (mkdir(full_path, 0777) == -1)
    {
      perror("Error creating directory");
      sendErrorMessage(sock, ERR_CREATING_DIR); // Send error message for error creating directory
      return ERR_CREATING_DIR;                  // Error while creating the directory
    }
    else
    {
      sendack(sock, "success");
      return SUCCESS; // Directory created successfully
    }
  }

  sendErrorMessage(sock, ERR_UNKNOWN); // Send error message for unknown 'kya' value
  return ERR_UNKNOWN;                  // Unknown 'kya' value, should never reach here
}

int deleteDirectoryRecursively(int sock, const char *path)
{
  DIR *dir = opendir(path);
  struct dirent *entry;
  char fullPath[BUFFER_SIZE];

  if (dir == NULL)
  {
    sendErrorMessage(sock, ERR_OPENING_DIR); // Send error message for error opening the directory
    return ERR_OPENING_DIR;
  }

  while ((entry = readdir(dir)) != NULL)
  {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
    {
      continue;
    }
    snprintf(fullPath, BUFFER_SIZE, "%s/%s", path, entry->d_name);
    int result = deleteFileOrDirectory(sock, fullPath); // Pass sock to deleteFileOrDirectory
    if (result != SUCCESS)
    {
      closedir(dir);
      return result;
    }
  }

  closedir(dir);

  if (rmdir(path) == -1)
  {
    sendErrorMessage(sock, ERR_CREATING_DIR); // Send error message for error creating directory
    return ERR_CREATING_DIR;
  }
  else
  {
    sendack(sock, "success");
    return SUCCESS;
  }
}

int deleteFileOrDirectory(int sock, const char *path)
{
  struct stat path_stat;
  if (stat(path, &path_stat) == -1)
  {
    sendErrorMessage(sock, -errno); // Send error message for error stat'ing the file or directory
    return -errno;
  }

  if (S_ISDIR(path_stat.st_mode))
  {
    return deleteDirectoryRecursively(sock, path); // Pass sock to deleteDirectoryRecursively
  }
  else if (S_ISREG(path_stat.st_mode))
  {
    if (unlink(path) == -1)
    {
      sendErrorMessage(sock, ERR_CREATING_FILE); // Send error message for error deleting the file
      return ERR_CREATING_FILE;                  // Error deleting the file
    }
    else
    {
      sendack(sock, "success");
      return SUCCESS; // File deleted successfully
    }
  }
  else
  {
    sendErrorMessage(sock, ERR_UNKNOWN_PATH_TYPE); // Send error message for unknown path type
    return ERR_UNKNOWN_PATH_TYPE;                  // Unknown path type
  }
}

int copyFile(const char *src, const char *dst, int socket)
{
  int src_fd = open(src, O_RDONLY);
  if (src_fd == -1)
  {
    perror("Error opening source file");
    sendack(socket, "Unable to open the source file.");
    return -1;
  }
  int dst_fd = open(dst, O_CREAT | O_WRONLY | O_TRUNC, 0666);
  if (dst_fd == -1)
  {
    perror("Error opening destination file");
    sendack(socket, "Unable to open or create the destination file.");
    close(src_fd);
    return -2;
  }

  char buffer[1024];
  ssize_t bytes_read;
  while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0)
  {
    ssize_t bytes_written = write(dst_fd, buffer, bytes_read);
    if (bytes_written != bytes_read)
    {
      perror("Error writing to destination file");
      sendack(socket, "Error occurred while writing to the destination file.");
      close(src_fd);
      close(dst_fd);
      return -3;
    }
  }

  if (bytes_read == -1)
  {
    perror("Error reading source file");
    sendack(socket, "Error occurred while reading the source file.");
  }
  close(src_fd);
  close(dst_fd);
  sendack(socket, "File copied successfully.");
  return 0;
}

int copyDirectory(const char *src, const char *dst, int socket)
{
  DIR *dir = opendir(src);
  if (dir == NULL)
  {
    perror("Error opening source directory");
    sendack(socket, "Unable to open the source directory.");
    return -1;
  }

  // Create destination directory
  if (mkdir(dst, 0777) == -1)
  {
    // If the directory exists, check if it's a directory
    struct stat dst_stat;
    if (stat(dst, &dst_stat) == 0 && S_ISDIR(dst_stat.st_mode))
    {
      // Directory already exists, proceed
    }
    else
    {
      perror("Error creating destination directory");
      sendack(socket, "Unable to create the destination directory.");
      closedir(dir);
      return -2;
    }
  }

  struct dirent *entry;
  char src_path[1024], dst_path[1024];

  // Loop through the source directory
  while ((entry = readdir(dir)) != NULL)
  {
    // Skip . and .. entries
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
    {
      continue;
    }

    // Construct source and destination paths
    snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
    snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);

    struct stat path_stat;
    if (stat(src_path, &path_stat) == -1)
    {
      perror("Error stating source file or directory");
      sendack(socket, "Error occurred while stating the source file or directory.");
      closedir(dir);
      return -3;
    }

    if (S_ISDIR(path_stat.st_mode))
    {
      if (copyDirectory(src_path, dst_path, socket) != 0)
      {
        closedir(dir);
        return -4;
      }
    }
    // If it's a file, copy the file
    else if (S_ISREG(path_stat.st_mode))
    {
      if (copyFile(src_path, dst_path, socket) != 0)
      {
        closedir(dir);
        return -5;
      }
    }
  }

  closedir(dir);
  sendack(socket, "Directory copied successfully.");
  return 0;
}

int copyPath(const char *src, const char *dst, int socket)
{
  struct stat path_stat;

  // Check if the source path exists and get its stats
  if (stat(src, &path_stat) == -1)
  {
    perror("Error stating source path");
    sendack(socket, "Unable to state the source path.");
    return -1; // Error stating source path
  }

  // If it's a directory, copy the directory recursively
  if (S_ISDIR(path_stat.st_mode))
  {
    return copyDirectory(src, dst, socket); // Copy directory
  }
  // If it's a regular file, copy the file
  else if (S_ISREG(path_stat.st_mode))
  {
    return copyFile(src, dst, socket); // Copy file
  }
  else
  {
    fprintf(stderr, "Unknown file type\n");
    sendack(socket, "Unknown file type (not a regular file or directory).");
    return -5; // Unknown file type (not a regular file or directory)
  }
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%~~client's intractions~~%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

int readFile(const char *path, int socket)
{
  printf("inside function for you\n");

  int fd = open(path, O_RDONLY);
  if (fd == -1)
  {
    sendack(socket, "Error opening source file.");
    return -1; // Error code indicating failure to open the file
  }

  char response[BUFFER_SIZE];
  ssize_t bytesRead = read(fd, response, BUFFER_SIZE);
  if (bytesRead == -1)
  {
    sendack(socket, "Error reading from file.");
    close(fd);
    return -2; // Error code indicating failure to read the file
  }

  // Null-terminate the string to make sure it's a valid C string
  response[bytesRead] = '\0';

  // Close the file after reading
  close(fd);

  // Send the file contents to the client
  ssize_t bytesSent = send(socket, response, bytesRead, 0);
  if (bytesSent == -1)
  {
    sendack(socket, "Error streaming file data.");
    return -3; // Error code indicating failure to stream the file data
  }
  sendErrorCode(socket, SUCCESS);
  return 0; // Success code
}
int getFileSize(const char *path, int socket)
{
  printf("Inside function to tell file size");

  struct stat st;

  if (stat(path, &st) == -1)
  {
    sendack(socket, "Error getting file size.");
    return -1;
  }

  int fileSize = st.st_size;
  printf("File size: %d bytes\n", fileSize);

  send(socket, &fileSize, sizeof(fileSize), 0);
  sleep(1);
  sendack(socket, "File size retrieved successfully.");
  return fileSize;
}

int writeFile(const char *path, int socket, const char *data)
{
  printf("Inside writeFile function\n");

  int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0666); // Make sure the file is created if not exists
  if (fd == -1)
  {
    sendack(socket, "Error creating or opening file.");
    return -1;
  }

  size_t dataLen = strlen(data);

  ssize_t bytesWritten = write(fd, data, dataLen);
  if (bytesWritten == -1)
  {
    sendack(socket, "Error writing to file.");
    close(fd); // Close file descriptor before returning
    return -2;
  }

  printf("Successfully wrote %zd bytes to the file\n", bytesWritten);
  close(fd); // Close file descriptor after operation

  sendack(socket, "Data successfully written to file.");
  return 0; // Success
}

int getFilePermissions(const char *path, int socket)
{
  printf("Inside function to get file permissions");

  struct stat st;

  // Get file status using stat system call
  if (stat(path, &st) == -1)
  {
    sendack(socket, "Error getting file permissions.");
    return -1; // Indicating failure to get permissions
  }

  int permissions = st.st_mode & 0777; // Mask out only the permission bits

  printf("Permissions: %o\n", permissions); // Print permissions in octal format

  // Send permissions to the client
  send(socket, &permissions, sizeof(permissions), 0);

  sendack(socket, "Successfully retrieved file permissions.");
  return 0; // Success
}

int streamAudioFile(const char *path, int socket)
{
  // Check if the file exists
  if (access(path, F_OK) == -1)
  {
    sendack(socket, "File does not exist.");
    return -1; // Indicating that the file doesn't exist
  }

  int file_fd = open(path, O_RDONLY);
  if (file_fd < 0)
  {
    sendack(socket, "Error opening audio file for streaming.");
    return -2; // Indicating failure to open the file
  }

  sendErrorCode(socket, SUCCESS);

  char buffer[BUFFER_SIZE];
  ssize_t bytesRead;

  // Stream the file data in chunks
  while ((bytesRead = read(file_fd, buffer, sizeof(buffer))) > 0)
  {
    ssize_t bytesSent = send(socket, buffer, bytesRead, 0);
    if (bytesSent == -1)
    {
      perror("Error sending audio data");

      // Handle specific errors
      if (errno == EPIPE || errno == ECONNRESET)
      {
        printf("Client disconnected during audio streaming.\n");
        break; // Exit the loop on client disconnection
      }
      else
      {
        sendack(socket, "Error sending audio data to client.");
        close(file_fd);
        return -3;
      }
    }
  }
  close(file_fd);

  if (bytesRead == 0)
  {
    sendack(socket, "Audio file streamed successfully.");
  }
  else if (bytesRead < 0)
  {
    perror("Error reading from audio file");
    sendack(socket, "Error reading audio file.");
  }

  return 0;
}

void send_file_metadata(int socket, const struct FileMetadata *metadata)
{
  printf("Sending file metadata...\n");
  ssize_t bytes_sent = send(socket, metadata, sizeof(struct FileMetadata), 0);
  if (bytes_sent == -1)
  {
    perror("send");
    sendErrorCode(socket, -1); // Send error code if send fails
    return;
  }
  else if (bytes_sent != sizeof(struct FileMetadata))
  {
    fprintf(stderr, "Error: Sent %zd bytes instead of %zu bytes.\n", bytes_sent, sizeof(struct FileMetadata));
    sendErrorCode(socket, -1); // Send error code if send fails
    return;
  }
  sendErrorCode(socket, SUCCESS);
}

void get_file_metadata(const char *file_path, struct FileMetadata *metadata, int sock)
{
  struct stat file_stat;

  printf("Getting file metadata for: %s\n", file_path);

  if (stat(file_path, &file_stat) == -1)
  {
    perror("stat");
    sendErrorCode(sock, -1); // Send error code if stat fails
    return;
  }

  // Ensure metadata struct is zeroed out
  memset(metadata, 0, sizeof(struct FileMetadata));

  strncpy(metadata->file_path, file_path, BUFFER_SIZE - 1);
  metadata->file_path[BUFFER_SIZE - 1] = '\0'; // Ensure null-termination
  metadata->file_size = (long long)file_stat.st_size;
  metadata->access_rights = file_stat.st_mode & 0777;

  // Copy and null-terminate time strings
  strncpy(metadata->last_accessed, ctime(&file_stat.st_atime), BUFFER_SIZE - 1);
  metadata->last_accessed[BUFFER_SIZE - 1] = '\0'; // Ensure null-termination
  strncpy(metadata->last_modified, ctime(&file_stat.st_mtime), BUFFER_SIZE - 1);
  metadata->last_modified[BUFFER_SIZE - 1] = '\0';
  strncpy(metadata->last_status_change, ctime(&file_stat.st_ctime), BUFFER_SIZE - 1);
  metadata->last_status_change[BUFFER_SIZE - 1] = '\0';

  printf("Metadata collected:\n");
  printf("  File Path: %s\n", metadata->file_path);
  printf("  File Size: %lld\n", metadata->file_size);
  printf("  Access Rights: %o\n", metadata->access_rights);
  printf("  Last Accessed: %s\n", metadata->last_accessed);
  printf("  Last Modified: %s\n", metadata->last_modified);
  printf("  Last Status Change: %s\n", metadata->last_status_change); 
  send_file_metadata(sock, metadata);
}