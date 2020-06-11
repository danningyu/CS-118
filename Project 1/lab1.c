#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NORMAL_EXIT 0
#define SYS_CALL_FAILURE 1
#define OTHER_FAILURE 2

#define MAX_CONNECTIONS 1

#define PORT_NUMBER 5000
#define BUFFER_SIZE 2048

//debug switch
#define DEBUG 0

int sockfd, clisockfd;
socklen_t clilen;
struct sockaddr_in serv_addr, cli_addr;

void createServerConnection(const int portNumber){
    //adapted from my CS 111 Lab 1B assignment and CS 118 TA code
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0){
        fprintf(stderr, "lab 1: error creating socket: %s\n", strerror(errno));
        exit(SYS_CALL_FAILURE);
    }
    bzero((char*) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portNumber);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    //in 1B: did not use htonl

    if(bind(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr))<0){
        fprintf(stderr, "lab 1: error binding to socket: %s\n", strerror(errno));
        exit(SYS_CALL_FAILURE);
    }
    listen(sockfd, MAX_CONNECTIONS);
}

char* createHTMLResponse(char* date, char* mtime, int contentLength, 
    char* contentType, char* filePath, int* responseLength){
    //most of this is probably not needed...
    const char* responseStart = "HTTP/1.1 200 OK \r\n";
    const char* responseServer = "Server: Ubuntu/16.04 (Unix) \r\n";
    const char* responseConnection = "Connection: Closed\r\n\r\n";
    const char* responseDate = "Date: ";
    const char* responseContentLength = "Content-Length: ";
    const char* responseLastModified = "Last-Modified: ";
    const char* responseContentType = "Content-Type: ";

    char contentLengthStr[32] = {0};
    sprintf(contentLengthStr, "%d", contentLength);
    int totalLength = strlen(responseStart) + strlen(responseDate) + strlen(date)
                        + 15 + strlen(responseServer) + strlen(responseLastModified)
                        + strlen(mtime) + strlen(responseContentLength)
                        + strlen(contentLengthStr) + strlen(responseContentType)
                        + strlen(contentType) + strlen(responseConnection)
                        + contentLength + 1; //15 for CRLF, 1 for null-byte

    if(DEBUG){
       fprintf(stderr, "Requesting file: %s\n", filePath); 
    }
    
    int fd = open(filePath+1, O_RDONLY);
    if(fd<0){
        fprintf(stderr, "lab 1: error opening file: %s\n", strerror(errno));
        return NULL;
    }
    
    char* response = calloc(totalLength+1, 1); //the crafted return message
    if(response == NULL){
        fprintf(stderr, "lab 1: Failed to allocate memory\n");
        exit(OTHER_FAILURE);
    }

    strcat(response, responseStart); // "HTTP/1.1 200 OK \r\n";
    strcat(response, responseDate); // "Date: "
    strcat(response, date); // "Wed, 22 Jul 2009 19:15:56 GMT"
    strcat(response, " \r\n"); // " \r\n"
    strcat(response, responseServer); // "Server: Ubuntu/16.04 (Unix) \r\n"
    strcat(response, responseLastModified);
    strcat(response, mtime);
    strcat(response, " \r\n");
    strcat(response, responseContentLength); // "Content-Length: "
    strcat(response, contentLengthStr); // "88"
    strcat(response, " \r\n"); // " \r\n"
    strcat(response, responseContentType);
    strcat(response, contentType);
    strcat(response, " \r\n");
    strcat(response, responseConnection);

    if(DEBUG){
            fprintf(stderr, "RESPONSE: so far:\n%s\n", response);
    }
    
    int amtRead = read(fd, response+strlen(response), contentLength);
    if(amtRead<0){
        fprintf(stderr, "lab 1: failed to read from file: %s\n", strerror(errno));
        exit(SYS_CALL_FAILURE);
    }
    if(DEBUG){
        fprintf(stderr, "Amount read: %d; content length: %d\n", amtRead, contentLength);
    }
    *responseLength = totalLength; //pass by "reference"
    return response;
}

void closeFds(){
    close(clisockfd);
    close(sockfd);
}

int main(int argc, char** argv){
    int portNumber = PORT_NUMBER;
    if(argc == 2){
        portNumber = atoi(argv[1]);
    }

    if(DEBUG){
        fprintf(stderr, "Port num: %d\n", portNumber);
    }
      
    createServerConnection(portNumber);  
    atexit(closeFds);

    char inBuffer[BUFFER_SIZE] = {0};
    char* response = NULL;
    clilen = sizeof(cli_addr);

    while(1){
        close(clisockfd);   
        clisockfd = accept(sockfd, (struct sockaddr*) &cli_addr, &clilen);
        if(clisockfd < 0){
            fprintf(stderr, "lab 1: error accepting connection: %s\n", strerror(errno));
            exit(SYS_CALL_FAILURE);
        }
        int sockInAmt = read(clisockfd, inBuffer, BUFFER_SIZE*sizeof(char));
        if(sockInAmt < 0){
            fprintf(stderr, "lab 1: error reading from socket: %s\n", strerror(errno));
            exit(SYS_CALL_FAILURE);
        }
        if(DEBUG){
            write(STDERR_FILENO, inBuffer, sockInAmt);
        }
        char* contentType = NULL;
        char* firstSpace = strchr(inBuffer, ' ');
        if(firstSpace == NULL){
            continue;
        }
        char* secondSpace = strchr(firstSpace+1, ' ');
        if(DEBUG){
          fprintf(stderr, "First space: %ld, second space: %ld\n", 
            firstSpace-inBuffer, secondSpace-inBuffer);  
        }     
        char filePath[BUFFER_SIZE] = {0};
        char* fileToServe = NULL;
        strncpy(filePath, inBuffer+(firstSpace-inBuffer+1), secondSpace - firstSpace -1);
        fileToServe = filePath;
        if(DEBUG){
            fprintf(stderr, "Requested file: |%s|\n", filePath);
        }
    


        if(strstr(filePath, "favicon.ico") != NULL){
            char* response = "HTTP/1.1 200 OK \r\n\r\n";
            write(clisockfd, response, strlen(response));
        }
        else{ //ignore favicon.ico
                //get current time
            char mTimeBuff[BUFFER_SIZE] = {0};
            char timeBuffer[BUFFER_SIZE] = {0};
            time_t currTime = time(0);
            if(currTime <0){
                fprintf(stderr, "lab 1: error getting current time: %s\n", strerror(errno));
                exit(SYS_CALL_FAILURE);
            }
            struct tm* currTmPtr = gmtime(&currTime);
            if(currTmPtr == NULL){
                fprintf(stderr, "lab 1: Failed to convert time to standard format: %s\n", strerror(errno));
                exit(OTHER_FAILURE);
            }
            strftime(timeBuffer, sizeof(timeBuffer), "%a, %d %b %Y %H:%M:%S %Z", currTmPtr);
            if(DEBUG){
                fprintf(stderr, "The current time is %s\n", timeBuffer);
            }
            if(strcmp(filePath, "/") == 0){
                fileToServe = "/index.html";
            }
            if(DEBUG){
                fprintf(stderr, "File to serve: %s\n", fileToServe);
            }
            
            //figure out file extension if it exists
            char* lastDot = strrchr(filePath, '.');
            char fileExtension[BUFFER_SIZE] = {0};
            if(lastDot != NULL){ //a dot exists, so there's a file extension             
                strncpy(fileExtension, filePath+(lastDot-filePath), strlen(filePath) - (lastDot-filePath));
                if(DEBUG){
                    fprintf(stderr, "Last dot at %ld, extension is %s\n", lastDot-filePath, fileExtension);
                }
                if(strstr(fileExtension, ".html")){
                    contentType = "text/html; charset=utf-8";
                }
                else if(strstr(fileExtension, ".txt")){
                    contentType = "text/plain; charset=utf-8";
                }
                else if(strstr(fileExtension, ".png")){
                    contentType = "image/png";
                }
                else if(strstr(fileExtension, ".jpg") || strstr(fileExtension, ".jpeg")){
                    contentType = "image/jpeg";
                }
                else{ //invalid file extension
                    contentType = "application/octet/stream";
                }
            }
            else if(strcmp(filePath, "/") == 0){ 
                //special case for the root page: redirect to index.html
                contentType = "text/html; charset=utf-8";
            }         
            else{ //no file extension detected
                contentType = "application/octet/stream";
            }

            if(contentType != NULL){
                int fileSize = 0;              
                struct stat fileProp;
                if(stat(fileToServe+1, &fileProp)<0){
                    fprintf(stderr, "lab 1: error reading requested file: %s\n", strerror(errno));
                    continue;
                }
                fileSize = fileProp.st_size;
                struct tm* mTimePtr = gmtime(&(fileProp.st_mtim.tv_sec));
                if(mTimePtr == NULL){
                    fprintf(stderr, "lab 1: Failed to convert time to standard format: %s\n", strerror(errno));
                    exit(OTHER_FAILURE);
                }
                strftime(mTimeBuff, sizeof(mTimeBuff), "%a, %d %b %Y %H:%M:%S %Z", mTimePtr);
                if(DEBUG){
                    fprintf(stderr, "Last modified time: %s\n", mTimeBuff);
                }

                int* responseLength = (int*)malloc(sizeof(int));
                if(responseLength == NULL){
                    fprintf(stderr, "lab 1: Failed to allocate memory\n");
                    exit(OTHER_FAILURE);
                }

                response = createHTMLResponse(timeBuffer, mTimeBuff, fileSize, 
                                            contentType, fileToServe, responseLength);
                if(response == NULL){
                    free(response);
                    free(responseLength);
                    continue;
                }

                //write the HTTP response out to the socket
                if(write(clisockfd, response, *responseLength)<0){
                    fprintf(stderr, "lab 1: Failed to write to socket: %s\n", strerror(errno));
                    exit(SYS_CALL_FAILURE);
                }
                if(DEBUG){
                    fprintf(stderr, "Response length after fcn return: %d\n", *responseLength);
                }
                
                //deallocate memory
                free(response);
                free(responseLength);
            }
        } //end if != favicon.ico
    } //END while loop

    exit(NORMAL_EXIT);
}
