/***********************************************************************************************
** Program Name: ftserver
** Programmer Name: Jordan Hamilton
** Course Name: CS 372 - Intro to Computer Networks
** Last Modified: 3/9/2020
** Description: This application creates a server that begins listening for connections on the
** port specified on the command line, then forks a new process each time a client creates a
** new connection. The server receives data initially to interpret the command being requested
** by the client (either -g FILENAME to get a file in the current working directory on the
** server, or -l to list the contents of the current directory, then the server responds to
** acknowledge which command was sent by the client. The client then sends details for the
** server to establish a data connection to the client, where the requested data is sent by
** server.
** EXTRA CREDIT: Forking child processes allows this server to create connections to multiple
** clients at once.
***********************************************************************************************/

#define MAX_CLIENT_ARGS 2
#define MAX_STRING_SIZE 100
#define BUFFER_SIZE 1048576
#define MESSAGE_FRAGMENT_SIZE 1024

#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void createDataConnection(struct sockaddr_in*, int*, const char*, const char*);
void defineHost(struct sockaddr_in*, const char*, const char*);
void error(const char*);
void fileToBuffer(const int*, char[], const int*);
void parseClientArgs(char[], char*[], size_t*);
void readCwdFilesToBuffer(char[]);
void receiveStringFromSocket(const int*, char[], char[], const char[]);
void sendStringToSocket(const int*, const char[]);
void setupServer(struct sockaddr_in*, const char*, int*);

int main(int argc, char* argv[]) {
  int listenSocketFD, establishedConnectionFD, dataConnectionFD;
  int plainTextFD, plainTextLength;
  char buffer[BUFFER_SIZE], messageFragment[MESSAGE_FRAGMENT_SIZE];
  size_t argsCount = 0;
  char* clientArgs[MAX_CLIENT_ARGS];
  const char getCmd[] = "-g", listCmd[] = "-l", endOfMessage[] = "||";
  struct sockaddr_in serverAddress, clientControlAddress, clientDataAddress;
  socklen_t sizeOfClientInfo;
  int exitMethod = -5;
  pid_t spawnPid = -5;

  // Check usage to ensure the hostname and port are passed as command line arguments.
  if (argc != 2) {
    fprintf(stderr, "Correct command format: %s PORT\n", argv[0]);
    exit(2);
  }

  // Ensure the elements of the array we'll use to store arguments sent by the client are initialized to NULL
  for (size_t i = 0; i < MAX_CLIENT_ARGS; i++) {
    clientArgs[i] = NULL;
  }

  // Create a struct containing this server's information.
  setupServer(&serverAddress, argv[1], &listenSocketFD);

  // Create an infinite loop for the server
  while (1) {
    // Get the size of the address for the client that will connect
    sizeOfClientInfo = sizeof(clientControlAddress);
    // Accept a connection, blocking if one is not available, until one connects
    establishedConnectionFD = accept(listenSocketFD, (struct sockaddr*) &clientControlAddress, &sizeOfClientInfo);

    // Fork a new process for the accepted connection if we didn't detect an error
    spawnPid = fork();
    switch (spawnPid) {
      case -1:
        error("An error occurred creating a process to handle a new connection");
      case 0:
        if (establishedConnectionFD < 0)
          error("An error occurred accepting a connection");

        // Clear the buffer to receive a message from the client
        memset(buffer, '\0', BUFFER_SIZE * sizeof(char));

        // Read the client's command message from the socket
        receiveStringFromSocket(&establishedConnectionFD, buffer, messageFragment, endOfMessage);

        if (strcmp(buffer, listCmd) == 0) {
          // Acknowledge the "-l" command sent to the server by sending the same command back to the client.
          sendStringToSocket(&establishedConnectionFD, listCmd);

          // Clear the buffer again to receive and parse the hostname and port of the client,
          // then create a data connection.
          memset(buffer, '\0', BUFFER_SIZE * sizeof(char));
          receiveStringFromSocket(&establishedConnectionFD, buffer, messageFragment, endOfMessage);
          parseClientArgs(buffer, clientArgs, &argsCount);
          printf("Connection from %s.\nList directory requested on port %s\n", clientArgs[0], clientArgs[1]);

          createDataConnection(&clientDataAddress, &dataConnectionFD, clientArgs[0], clientArgs[1]);

          // Clear the buffer, read the directory contents into the buffer, then append the end of message indicator.
          memset(buffer, '\0', BUFFER_SIZE * sizeof(char));
          readCwdFilesToBuffer(buffer);
          strcat(buffer, endOfMessage);

          // Send the directory contents to the client.
          sendStringToSocket(&dataConnectionFD, buffer);
          printf("Sending directory contents to %s:%s.\n", clientArgs[0], clientArgs[1]);

          // Close the data connection once the directory contents have been sent
          close(dataConnectionFD);

        } else if (strstr(buffer, getCmd) != NULL) {
          // Parse and store the file name to find in the current working directory on the server
          char fileName[MAX_STRING_SIZE];
          parseClientArgs(buffer, clientArgs, &argsCount);
          memset(fileName, '\0', MAX_STRING_SIZE * sizeof(char));
          strcpy(fileName, clientArgs[1]);

          // Attempt to open the file for reading in the current directory
          plainTextFD = open(fileName, O_RDONLY);
          if (plainTextFD < 0) {
            // Send an error message to the client and close the connecting,
            // also displaying an error message on the server if the file does not exist or can't be opened.
            sendStringToSocket(&establishedConnectionFD, "FILE NOT FOUND");
            close(establishedConnectionFD);
            error("File not found. Sending error message");
          }

          // Acknowledge the "-g" command sent to the server by sending the same command back to the client.
          sendStringToSocket(&establishedConnectionFD, getCmd);

          // Clear the buffer again and receive the hostname and port of the client for a data connection.
          memset(buffer, '\0', BUFFER_SIZE * sizeof(char));
          receiveStringFromSocket(&establishedConnectionFD, buffer, messageFragment, endOfMessage);
          parseClientArgs(buffer, clientArgs, &argsCount);
          printf("Connection from %s.\nFile \"%s\" requested on port %s\n", clientArgs[0], fileName, clientArgs[1]);

          createDataConnection(&clientDataAddress, &dataConnectionFD, clientArgs[0], clientArgs[1]);

          // Clear the buffer, then read the file into the buffer, append the end of message indicator.
          memset(buffer, '\0', BUFFER_SIZE * sizeof(char));
          fileToBuffer(&plainTextFD, buffer, &plainTextLength);
          strcat(buffer, endOfMessage);

          // Send the file contents to the client.
          printf("Sending \"%s\" to %s:%s.\n", fileName, clientArgs[0], clientArgs[1]);
          sendStringToSocket(&dataConnectionFD, buffer);

          // Close the data connection once the file contents have been sent
          close(dataConnectionFD);

        } else {
          printf("Received an unknown command from the client.\n");
          sendStringToSocket(&establishedConnectionFD, "Unknown command received. Please try again.");
        }

        // Close the existing socket which is connected to the client
        close(establishedConnectionFD);

        // Free allocated memory for arguments from the client after we've closed the connection
        for (size_t i = 0; i < MAX_CLIENT_ARGS; i++) {
          if (clientArgs[i] != NULL) {
            free(clientArgs[i]);
            clientArgs[i] = NULL;
          }
        }

      default:
        // Try to reap any zombie child processes
        spawnPid = waitpid(-1, &exitMethod, WNOHANG);
        // Close the existing socket which is connected to the client
        close(establishedConnectionFD);
    }


  }
  // Close the listening socket
  close(listenSocketFD);
  return(0);
}

/* This function takes a pointer to a sockaddr_in struct and a pointer to a socket file descriptor, then attempts to
 * create a socket and connects to the host via that socket. Credit: CS 344 (Operating Systems 1) lecture video 4.2. */
void createDataConnection(struct sockaddr_in* address, int* socketFD, const char* host, const char* port) {

  // Create a struct containing the information to make a data connection to the client.
  defineHost(address, host, port);

  // Set up the socket
  *socketFD = socket(AF_INET, SOCK_STREAM, 0); // Create the socket
  if (*socketFD < 0)
    error("An error occurred creating a socket");

  // Connect to the host
  if (connect(*socketFD, (struct sockaddr*) address, sizeof(*address)) < 0) // Connect socket to address
    error("An error occurred establishing a data connection");
}

/* This function takes a pointer to a sockaddr_in struct and two strings representing the host address and port, then
 * attempts to declare the necessary properties in the struct to allow a socket to be created, passing that struct
 * in the createDataConnection function. Credit: CS 344 (Operating Systems 1) lecture video 4.2. */
void defineHost(struct sockaddr_in* hostAddress, const char* host, const char* port) {
  int portNumber = atoi(port); // Get the port number, convert to an integer from a string
  struct hostent* hostInfo;

  // Set up the server address struct
  memset((char*) hostAddress, '\0', sizeof(*hostAddress)); // Clear out the address struct
  hostAddress->sin_family = AF_INET; // Create a network-capable socket
  hostAddress->sin_port = htons(portNumber); // Store the port number
  hostInfo = gethostbyname(host); // Convert the machine name into a special form of address

  if (hostInfo == NULL) {
    fprintf(stderr, "An error occurred defining the address to connect to.\n");
    exit(2);
  }

  // Copy in the address
  memcpy((char*) &hostAddress->sin_addr.s_addr, (char*) hostInfo->h_addr, hostInfo->h_length);
}

// Error function used for reporting issues
void error(const char* msg) {
  perror(msg);
  exit(2);
}

/* Takes a file descriptor pointer, a buffer to store the file's contents and a pointer to the length of the file,
 * then uses the read function to store the provided number of bytes from the file into the buffer. */
void fileToBuffer(const int* fileDescriptor, char buffer[], const int* fileLength) {
  int bytesRead = -5;
  lseek(*fileDescriptor, 0, SEEK_SET);
  bytesRead = read(*fileDescriptor, buffer, *fileLength);

  if (bytesRead < 0)
    error("An error occurred trying to read file contents");
}

/* This function takes the string sent from the client, a pointer to a string array and a pointer to a value
 * representing the number of arguments sent by the client that are stored in the array. Memory is freed if
 * needed, then allocated for each space-delimited word that is parsed from the string and stored into the array. */
void parseClientArgs(char command[], char* args[], size_t* argsCount) {
  // Look for every word separated by a space
  const char* delimiter = " ";
  *argsCount = 0;

  // If any of the arguments in the array are not null, free memory and set them as null before reusing the array.
  for (size_t i = 0; i < MAX_CLIENT_ARGS; i++) {
    if (args[i] != NULL) {
      free(args[i]);
      args[i] = NULL;
    }
  }

  char* buffer = strtok(command, delimiter);

  while (buffer != NULL) {
    args[*argsCount] = malloc(MAX_STRING_SIZE * sizeof(char));

    if (args[*argsCount] == NULL)
      perror("Allocating memory to store a command argument failed");

    // Copy the detected argument into the next available space in the array
    memset(args[*argsCount], '\0', MAX_STRING_SIZE * sizeof(char));
    strcpy(args[*argsCount], buffer);

    // Increment the number of arguments we've detected in the current string
    *argsCount += 1;

    // Find the next string separated by a space if one exists, or exit the loop once buffer is null.
    buffer = strtok(NULL, delimiter);
  }
}

/* Opens the current working directory, then uses the same logic from CS 344 Block 2.4 - Manipulating Directories
 * to loop through the files in this directory and print to the passed buffer. Cleans up by closing the directory. */
void readCwdFilesToBuffer(char buffer[]) {

  // Look in the current directory.
  char* dirPath = ".";
  DIR* currentDir;
  struct dirent* file;

  // Attempt to open the directory
  currentDir = opendir(dirPath);
  if (!currentDir)
    error("An error occurred reading the current directory.\n");

  // Loop through each file in the directory, printing a formatted string into the buffer with the file's name and
  // a newline character.
  while ((file = readdir(currentDir))) {
    sprintf(buffer + strlen(buffer), "%s\n", file->d_name);
  }

  closedir(currentDir);
}

/* Takes a socket, a message buffer, a smaller array to hold characters as they're read, the size of the array, and a
 * small string used by client and server to indicate the end of a message. The function loops through until the
 * substring is found, as seen in the Network Clients video for block 4, repeatedly adding the message fragment
 * to the end of the message. The substring that marks the end of the message is then replaced with a null terminator. */
void receiveStringFromSocket(const int* establishedConnectionFD, char message[], char messageFragment[], const char endOfMessage[]) {
  int charsRead = -5;
  long terminalLocation = -5;

  while (strstr(message, endOfMessage) == NULL) {
    memset(messageFragment, '\0', MESSAGE_FRAGMENT_SIZE * sizeof(char));
    charsRead = recv(*establishedConnectionFD, messageFragment, MESSAGE_FRAGMENT_SIZE - 1, 0);

    // Exit the loop if we either don't read any more characters when receiving, or we failed to retrieve any characters
    if (charsRead <= 0)
      break;

    strcat(message, messageFragment);
  }

  /* Find the terminal location using the method in the Network Clients video from Block 4
   * then set a null terminator after the actual message contents end. */
  terminalLocation = strstr(message, endOfMessage) - message;
  message[terminalLocation] = '\0';
}

/* Takes a pointer to a socket, followed by a string to send via that socket,
 * then loops to ensure all the data in the string is sent. */
void sendStringToSocket(const int* socketFD, const char message[]) {
  int charsWritten;
  // Send message to server
  charsWritten = send(*socketFD, message, strlen(message), 0); // Write to the server
  if (charsWritten < 0)
    error("An error occurred writing to the socket");

  while (charsWritten < strlen(message)) {
    int addedChars = 0;
    // Write to the server again, starting from one character after the most recently sent character
    addedChars = send(*socketFD, message + charsWritten, strlen(message) - charsWritten, 0);
    if (addedChars < 0)
      error("An error occurred writing to the socket");

    // Exit the loop if no more characters are being sent to the server.
    if (addedChars == 0)
      break;

    // Add the number of characters written in an iteration to the total number of characters sent in the message
    charsWritten += addedChars;
  }
}

/* This function takes a pointer to a sockaddr_in struct, a string representing the port to listen on and an integer
 * for a socket file descriptor, then attempts to set up and enable the socket to create the server.
 * Credit: CS 344 (Operating Systems 1) lecture video 4.2. */
void setupServer(struct sockaddr_in* serverAddress, const char* port, int* listenSocketFD) {
  int portNumber = atoi(port);

  // Set up the server address struct
  memset((char*) serverAddress, '\0', sizeof(*serverAddress)); // Clear out the address struct
  serverAddress->sin_family = AF_INET; // Create a network-capable socket
  serverAddress->sin_port = htons(portNumber); // Store the port number

  // Set up the socket
  *listenSocketFD = socket(AF_INET, SOCK_STREAM, 0);
  if (listenSocketFD < 0)
    error("An error occurred opening a socket");

  // Enable the socket to begin listening
  if (bind(*listenSocketFD, (struct sockaddr*) serverAddress, sizeof(*serverAddress)) < 0)
    error("An error occurred binding to a socket");

  // Flip the socket on - it can now receive up to 5 connections
  listen(*listenSocketFD, 5);
  printf("Server open on %s\n", port);
}
