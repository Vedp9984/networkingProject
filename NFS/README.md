# Network File System Project

## Overview

This project implements a basic Network File System (NFS) using a client-server architecture with a Naming Server, Storage Servers, and Clients. The system facilitates file operations such as reading, writing, deleting, and streaming audio files. The key components work together to manage file storage and ensure that clients can access and modify files across multiple storage servers.

## Components

### 1. **Naming Server (NM)**
   - The central server that maps file paths to Storage Servers and manages file requests from clients.
   - Responsible for directing clients to the correct Storage Server based on file location.
   - Stores metadata related to file locations across multiple storage servers.

### 2. **Storage Servers (SS)**
   - Servers that handle the physical storage and retrieval of files.
   - Manage file operations such as reading, writing, deleting, and streaming audio files.
   - Interact with the Naming Server to register their available paths and file locations.

### 3. **Client**
   - A program that interacts with the Naming Server to find the correct Storage Server for file operations.
   - Requests file operations (read, write, delete) and communicates directly with the Naming Server.

---

## Features

### 1. **File Operations**
   - **Reading Files**: Clients can request to read files stored on a specific Storage Server. The Naming Server directs the client to the correct server, which then provides the file content.
   - **Writing Files**: Clients can send write requests to Storage Servers. This operation can be performed asynchronously for large files, allowing clients to receive immediate acknowledgment while the file is written in the background.
   - **Deleting Files**: Clients can request the deletion of files or directories. Once a request is received, the corresponding Storage Server performs the deletion.
   - **Creating Files and Directories**: Clients can create new files and directories in the network file system. The Naming Server coordinates the action and updates the list of accessible paths.
   - **Listing Files and Folders**: Clients can request to list all files and directories in a given directory across multiple Storage Servers.

### 2. **Client-Naming Server Interaction**
   - **Path Finding**: Clients send requests to the Naming Server with a file path. The Naming Server locates the file across all Storage Servers and returns the relevant server's information (IP address and port).
   - **Error Handling**: The system responds with appropriate error codes for situations like file not found or access issues, ensuring clear communication with the client.

### 3. **Asynchronous and Synchronous Writing**
   - **Asynchronous Write**: For large files, clients can initiate asynchronous write operations. The Storage Server immediately acknowledges the request, while the actual data is written in the background. This avoids client waiting time and improves system responsiveness.
   - **Synchronous Write**: Clients can opt for synchronous writes by using a flag. This prioritizes write operations and waits for the server to finish writing before acknowledging the request.

### 4. **Concurrent Client Access**
   - **Multiple Clients**: The system supports concurrent access from multiple clients. The Naming Server handles requests from multiple clients simultaneously by providing initial acknowledgment and processing them asynchronously.
   - **Concurrent File Reading**: Multiple clients can read the same file at the same time. However, if a file is being written to by one client, others will be blocked from reading it until the write operation completes.

### 5. **File Replication and Backup**
   - **Replication**: To ensure fault tolerance, files are replicated across multiple Storage Servers. When a file is written or updated on a Storage Server, it is asynchronously copied to other Storage Servers to ensure availability in case of failure.
   - **Failure Detection**: The Naming Server monitors the health of Storage Servers and can reroute client requests to replicated copies of files if a Storage Server goes down.

### 6. **Efficient Search and Caching**
   - **Efficient Path Lookup**: The Naming Server uses efficient data structures (tries) for quick file location searches, even in systems with large numbers of files.
   - **LRU Caching**: The Naming Server implements Least Recently Used (LRU) caching for recently accessed file paths, improving response times for repeated requests.

### 7. **File Streaming**
   - **Audio File Streaming**: Clients can stream audio files directly from the Storage Server. The Naming Server directs the client to the correct server, and the client receives audio data to be played by a media player.

### 8. **Logging and Bookkeeping**
   - **Logging Operations**: The Naming Server logs every request or acknowledgment received from clients and Storage Servers. This helps track operations and assists in debugging.
   - **Communication Logging**: The logs also include relevant information like IP addresses and ports used in each communication, making it easier to trace issues.

---

## Assumptions

### We are using 0 based indexing for storage servers

### Every path starts with / but do not end with /

### All paths are absolute 

### There is an upper limit for one read which is 1024 which is buffer size

### Whenever a storage server connects or disconnects it notifies to the naming server

### Whenever there is a caching will be notified in a naming server

### Whenever there is a invalid path the client will give that storage server does not exist and will ask the client to enter the input command again

### The deletion does not happen outside parent directory

### In create file or delete file the extension does not matter 
- If x.txt is present then we cannot create x.c 