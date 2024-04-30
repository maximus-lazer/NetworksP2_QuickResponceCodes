#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sendfile.h>

#include <arpa/inet.h>

#define PORT "2012" // the port client will be connecting to

#define MAXDATASIZE 100 // max number of bytes we can get at once

// Prototypes

off_t getFileSize(char *file);
void sendData(char *name, int sockfd);
void getData(int sockfd);

// beej starter code

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

int main(int argc, char *argv[]) {
    int sockfd, numbytes;
    char buf[MAXDATASIZE];
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];
    int qrFD;
    off_t sendingOffset;
    int sentBytes;
    off_t remainingData;
    int fd;

    if (argc < 2) {
        fprintf(stderr, "Input Error: client_hostname\n");
        exit(1);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(argv[1], PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and connect to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
    printf("client: connecting to %s\n", s);

    freeaddrinfo(servinfo); // all done with this structure

    if ((numbytes = recv(sockfd, buf, MAXDATASIZE - 1, 0)) == -1) {
        perror("recv");
        exit(1);
    }

    buf[numbytes] = '\0';

    printf("client: received '%s'\n", buf);

    if (send(sockfd, "Hello, Server!\n", 14, 0) == -1) {
        perror("send");
    }

    while (1) {

        char input[100];
        if ((scanf("%s", input) < 0)) {
            perror("Input");
            exit(0);
        }

        // Close
        if (strcmp(input, "close") == 0) {
            if (send(sockfd, "0", 1, 0) == -1) {
                perror("send");
            }

            if ((numbytes = recv(sockfd, buf, 32, 0)) == -1) {
                perror("recv");
                exit(1);
            }

            buf[numbytes] = '\0';

            printf("client: received '%s'\n", buf);

            close(sockfd);
            exit(0);
        }

        // Shutdown
        else if (strcmp(input, "shutdown") == 0) {
            if (send(sockfd, "1", 1, 0) == -1) {
                perror("send");
            }
            memset(input, 0, strlen(input));
        }

        // PNG input
        else if (strlen(input) > 0) {
            off_t fileSize;
            fileSize = getFileSize(input);

            if (fileSize == 0) {
                printf("Error: File not Found\n");
            }
            else {
                if (send(sockfd, "2", 1, 0) == -1) {
                    perror("send");
                }
                sendData(input, sockfd);
                memset(input, 0, strlen(input));
                getData(sockfd);
            }
        }
    }

    close(sockfd);

    return 0;
}

/**
 * Gets file size
 * @param file input file
 * @return size of file
*/
off_t getFileSize(char *file) {
    struct stat buf;

    if (stat(file, &buf) == -1) {
        perror("Get file size:");
        exit(EXIT_FAILURE);
    }

    return buf.st_size;
}

/**
 * Gets client data to print
 * @param sockfd client connection
*/
void getData(int sockfd) {
    // Receive the length of the data
    off_t length;
    if (recv(sockfd, &length, sizeof(off_t), 0) == -1) {
        perror("recv length");
        exit(EXIT_FAILURE);
    }

    // Allocate memory for receiving buffer
    char buffer[MAXDATASIZE];

    // Open a file to write the received data
    FILE *file = fopen("received_data.txt", "wb");
    if (file == NULL)
    {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    u_int32_t bytesReceivedSoFar = 0;
    while (bytesReceivedSoFar < length) {
        // Receive data from the server
        int bytesActuallyReceived = recv(sockfd, buffer, MAXDATASIZE, 0);

        if (bytesActuallyReceived == -1) {
            perror("recv data");
            exit(EXIT_FAILURE);
        }

        // Write received data to the file
        fwrite(buffer, 1, bytesActuallyReceived, file);

        bytesReceivedSoFar += bytesActuallyReceived;
    }

    printf("Received server bytes: %d\n", bytesReceivedSoFar);

    // Close the file
    fclose(file);

    FILE *fileRead = fopen("received_data.txt", "rb");
    char string[255];

    if (fileRead == NULL) {
        printf("File error");
    }

    printf("QRCode Result:\n\n");

    while (fgets(string, 255, fileRead) != NULL) {
        printf("%s", string);
    }

    fclose(fileRead);
}

/**
 * Send client data to server
 * 
 * @param sockfd client connection
*/
void sendData(char *name, int sockfd) {
    off_t fileSize;
    FILE *qrFile;

    // File size for file name
    fileSize = getFileSize(name);

    printf("File Size: %ld\n", fileSize);

    qrFile = fopen(name, "r");
    char sendingBuf[fileSize];
    int sendingSize;

    bzero(sendingBuf, fileSize);

    if (qrFile == NULL) {
        perror("File error");
    }

    // Send File size
    if (send(sockfd, &fileSize, sizeof(off_t), 0) == -1) {
        perror("File Size");
    }

    printf("Sent File Size: %ld\n", fileSize);

    // Send all data
    while ((sendingSize = fread(sendingBuf, 1, fileSize, qrFile)) > 0) {
        if (send(sockfd, sendingBuf, sendingSize, 0) == -1) {
            perror("Sending data");
        }

        printf("Sent bytes: %d\n", sendingSize);
        bzero(sendingBuf, fileSize);
    }
}