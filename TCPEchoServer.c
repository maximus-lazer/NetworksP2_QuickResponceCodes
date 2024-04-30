#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h> // for time functions

#define DEFAULT_PORT "2012" // Default port
#define BACKLOG 10          // how many pending connections queue will hold
#define MAXDATASIZE 100     // max number of bytes we can get at once

// OPTIONS/DEFAULTS
char PORT[6];
int rate_limit = 3;
int rate_limit_time = 60;
int max_users = 3;
int timeout = 80;


// Prototypes

void logData(const char *message, const char *client_ip);
void parseQRCode(int new_fd);
FILE *writeFile(int sockfd);
off_t getFileSize(char *file);

// beej starter code

void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}

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
    int sockfd, new_fd, numbytes; // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    char buf[MAXDATASIZE];
    int rv;

    // parse command line arguments for options
    strcpy(PORT, DEFAULT_PORT); // intialize port with defualt val
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "PORT") == 0) {
            strcpy(PORT, argv[i + 1]); // change port to arg value
        }
        else if (strcmp(argv[i], "RATE") == 0) {
            if (i + 2 < argc) {
                rate_limit = atoi(argv[i + 1]);
                rate_limit_time = atoi(argv[i + 2]);
            }
        }
        else if (strcmp(argv[i], "MAX_USERS") == 0) {
            if (i + 1 < argc) {
                max_users = atoi(argv[i + 1]);
            }
        }
        else if (strcmp(argv[i], "TIME_OUT") == 0) {
            if (i + 1 < argc) {
                timeout = atoi(argv[i + 1]);
            }
        }
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // Loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    while (1) { // main accept() loop
        sin_size = sizeof their_addr;

        // New connection established
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
        printf("Connected to: %s\n", s);

        // Log connection
        logData("Connection established", s);

        if (!fork()) { // this is the child process
            close(sockfd); // child doesn't need the listener

            // Send message to client
            if (send(new_fd, "Enter 'close' to disconnect, 'shutdown' to turn off server, or 'QRCode_name.png' for URL: \n", 98, 0) == -1) {
                perror("send");
            }

            if ((numbytes = recv(new_fd, buf, 14, 0)) == -1) {
                perror("recv");
                exit(1);
            }

            buf[numbytes] = '\0';

            printf("Received: %s\n", buf);

            // Initialize timer
            time_t startTime = time(NULL);

            int flag = 0;
            while (!flag) {

                // Timeout
                if (time(NULL) - startTime > timeout) {
                    printf("Timeout:\n");
                    close(new_fd);
                    // Log disconnection
                    logData("Connection timeout", s);
                    flag = 1;
                }

                int input;
                char numBuf[2];
                numBuf[1] = '\0';

                // Set input
                if ((numbytes = recv(new_fd, numBuf, 1, 0)) == -1) {
                    input = -1;
                }
                else {
                    input = atoi(numBuf);
                    printf("Input: %d\n", input);
                }

                // Close
                if (input == 0) {
                    if (send(new_fd, "Connection closed\n", 32, 0) == -1) {
                        perror("send");
                    }

                    close(new_fd);
                    logData("Connection closed", s);
                    printf("Server closed connection: %s\n", s);
                }

                // Shutdown
                if (input == 1) {
                    printf("Server: shutdown by: %s\n", s);
                    close(new_fd);
                    logData("Server Shutdown", s);
                    exit(1);
                    return 0;
                }

                // if input is a file, find url
                if (input == 2) {
                    logData("Parsing QRCode", s);
                    parseQRCode(new_fd);
                }

                input = -1;
            }
            close(new_fd); // parent doesn't need this
        }

        return 0;
    }
}

/**
 * Writes data to log file
 * @param message reasong of log
 * @param client_ip IP of client
*/
void logData(const char *message, const char *client_ip) {
    time_t current_time;
    struct tm *time_info;
    char time_str[20];
    FILE *log_file;

    // Get current time
    time(&current_time);
    time_info = localtime(&current_time);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", time_info);

    // Open or make new file to write to
    log_file = fopen("log.txt", "a+");
    if (log_file == NULL) {
        perror("Error opening log file");
        return;
    }

    // Write to log
    fprintf(log_file, "..............................\n%s %s %s\n", time_str, message, client_ip);
    fclose(log_file);
}

/**
 * Parses and sends QRCode data
 * @param new_fd connection
*/
void parseQRCode(int new_fd) {
    FILE *qrFile;
    qrFile = writeFile(new_fd);

    if (qrFile == NULL) {
        printf("QR File not found\n");
    }

    else {
        // Uses java library to convert QRCode to .txt file
        system("java -cp javase.jar:core.jar com.google.zxing.client.j2se.CommandLineRunner receivedQRCode.png > QRresult.txt");

        off_t fileSize = getFileSize("QRresult.txt");

        FILE *dataFile;
        dataFile = fopen("QRresult.txt", "r");

        char sendingBuf[fileSize];
        char sizeBuf[sizeof(off_t)];

        int sendingSize;
        bzero(sendingBuf, fileSize);

        if (dataFile == NULL) {
            perror("Opening file:");
            exit(1);
        }

        // Send URL Size
        if (send(new_fd, &fileSize, sizeof(off_t), 0) == -1) {
            perror("URL Size:");
        }

        printf("URL Size: %ld\n", fileSize);

        // Send all data
        while ((sendingSize = fread(sendingBuf, 1, fileSize, dataFile)) > 0) {
            if (send(new_fd, sendingBuf, sendingSize, 0) == -1) {
                perror("Sending File:");
            }
            printf("Sent URL bytes: %d\n", sendingSize);
            bzero(sendingBuf, fileSize);
        }
    }
}

/**
 * Receives client data to write to a file
 * @param sockfd client connection
 * @return New file from client
*/
FILE *writeFile(int sockfd) {
    off_t length;
    if (recv(sockfd, &length, sizeof(off_t), 0) == -1) {
        perror("recv length");
        exit(EXIT_FAILURE);
    }

    char buffer[MAXDATASIZE];

    // Open a file to write the received data
    FILE *file = fopen("receivedQRCode.png", "wb");
    if (file == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    u_int32_t bytesReceivedSoFar = 0;
    while (bytesReceivedSoFar < length) {
        // Receive data from the client
        int bytesActuallyReceived = recv(sockfd, buffer, MAXDATASIZE, 0);
        if (bytesActuallyReceived == -1) {
            perror("recv data");
            exit(EXIT_FAILURE);
        }

        fwrite(buffer, 1, bytesActuallyReceived, file);
        bytesReceivedSoFar += bytesActuallyReceived;
    }
    printf("Received file bytes: %d\n", bytesReceivedSoFar);
    fclose(file);

    return file;
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