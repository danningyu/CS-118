#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <math.h>

#include "utility.h"

#define AHEAD 1
#define BEHIND 2

#define CREATE_PERMISSIONS 0666

struct sockaddr_in serv_addr, cli_addr;
int sockfd;

int openServerConnection(const int portNumber){
    sockfd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if(sockfd < 0){
        fprintf(stderr, "lab 2: error creating UPD socket: %s\n", strerror(errno));
        exit(SYS_CALL_ERROR);
    }
    bzero((char*) &serv_addr, sizeof(serv_addr));
    bzero((char*) &cli_addr, sizeof(cli_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portNumber);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr))<0){
        fprintf(stderr, "lab 2: error binding to socket: %s\n", strerror(errno));
        exit(SYS_CALL_ERROR);
    }
    return sockfd;
}

void sendSegment(packet_t* packetInput, int type){
    packet_t packet = *packetInput;
    printSegment(packetInput, type);  
    ssize_t sendAmt = sendto(sockfd, (char*)&packet, PACKET_SIZE, 0, 
            (struct sockaddr*)&cli_addr, sizeof(cli_addr));
    if(sendAmt < 0){
        fprintf(stderr, "lab 2: error sending packet: %s\n", strerror(errno));
        exit(OTHER_ERROR);
    }
}

int main(int argc, char** argv){
    if(argc != 2){
        fprintf(stderr, "lab 2: usage: ./server port\n");
        exit(ARGS_ERROR);
    }
    int portNumber = atoi(argv[1]);

    if(DEBUG){
        fprintf(stderr, "Debug: port number %d\n", portNumber);
        assert(sizeof(packet_t) == 524);
    }

    openServerConnection(portNumber); 
    int setNonBlock = fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);
    if(setNonBlock < 0){
        fprintf(stderr, "Error when making socket nonblocking: %s\n", strerror(errno));
        exit(SYS_CALL_ERROR);
    }

    int fileNumber = 1;
    int readAmt = 0;
    socklen_t clilen = sizeof(cli_addr);
    
    packet_t packet; //a "buffer" to hold data
    packet_t prevReceivePacket; //the previous packet, sent or received
    packet_t prevSentPacket; //previous sent packet
    uint32_t constantServSeqNum = 0;
    srand(time(NULL));

    // SR stuff - remember to refresh at each new connection!!
    uint32_t nextExpectSeqNum;
    int nextExpectedIndex = 0;
    int received[10] = {0}; // true/false for if received[i] has been gotten
    packet_t packets[10]; //to store up to 10 out of order packets
    int gotFirstDataPacket = 0;
    // END SR stuff

    //teardown loss stuff
    packet_t serverFINPacket;
    struct timespec sentFINTime;
    struct timespec currentTime;

    struct pollfd pollEvents[] = {
        {sockfd, POLLIN | POLLHUP | POLLERR, 0},
    };

    int fd;
    int state = SERV_WAIT_FOR_SYN;
    char fileName[15] = {0};
    uint32_t initialSeqNum = 0;
    uint32_t serverSeqNum = 0; //does not change for most of connection lifetime
    while(1){      
        int pollResult = poll(pollEvents, 1, 0);
        if(pollResult < 0){
            fprintf(stderr, "lab 2: polling error: %s\n", strerror(errno));
            exit(SYS_CALL_ERROR);
        }
        if(clock_gettime(CLOCK_MONOTONIC, &currentTime) < 0){
            fprintf(stderr, "lab 2: failed to get current time: %s\n", strerror(errno));
            exit(SYS_CALL_ERROR);
        }

        // check for FIN timeout here
        if(state == SERV_WAIT_FOR_FIN_CLIENT_ACK){
            // waiting for client to ACK the server's FIN
            // and timeout occurred because the FIN was lost
            // OR, the ACK from the client for the server's FIN was lost
            // and so we resend again
            if( (currentTime.tv_sec + (double)(currentTime.tv_nsec)/NS_TO_S)
                     >= ((double)(sentFINTime.tv_sec) + 0.5 + (double)(sentFINTime.tv_nsec)/NS_TO_S)){
                if(DEBUG){
                    fprintf(stderr, "0.5 s passed, server sent FIN timeout\n");
                }
                printf("TIMEOUT %u\n", ntohl(serverFINPacket.seqNum));
                sendSegment(&serverFINPacket, RESEND);
                // reset the timer for the FIN
                if(clock_gettime(CLOCK_MONOTONIC, &sentFINTime) < 0){
                    fprintf(stderr, "lab 2: failed to get current time: %s\n", strerror(errno));
                    exit(SYS_CALL_ERROR);
                }
            }

            // NOTE: we assume that the ACK from the client for the server
            // FIN is not lost more than 4 times (that is, we assume it eventually
            // makes it to the server. Otherwise, the server will wait forever
            // for client ACK to arrive and will send the FINs to nothingness
            // because after the first FIN arrives at the client, it closes its
            // side after 2 seconds. But we have no way of knowing the socket
            // was closed.
        }

        if(pollEvents[0].revents & POLLIN){
            // we have data to read!
            if(state == SERV_WAIT_FOR_SYN){
                // aka waiting for client to connect
                // read data from socket - look for SYN message
                memset(&packet, 0, PACKET_SIZE);
                readAmt = recvfrom(sockfd, (char*)&packet, PACKET_SIZE, 0, 
                                    (struct sockaddr*)&cli_addr, &clilen);
                if(DEBUG){
                    fprintf(stderr, "Debug: read in first syn packet\n");
                }
                
                if(readAmt < 0){
                    fprintf(stderr, "lab 2: error reading from socket: %s", strerror(errno));
                    exit(OTHER_ERROR);
                }
                if(packet.flags & SYN_FLAG){
                    // ignore any leftover teardown packets from previous
                    // connection
                    printSegment(&packet, RECEIVE);
                    assert(packet.flags & SYN_FLAG);
                    prevReceivePacket = packet;
                    
                    //reply with SYN-ACK
                    memset(&packet, 0, HEADER_SIZE);
                    initialSeqNum = rand() % SEQ_NUM_MOD;

                    // initialSeqNum = 25157;
                    constantServSeqNum = (initialSeqNum + 1) % SEQ_NUM_MOD;

                    packet.seqNum = htonl(initialSeqNum);
                    packet.ackNum = htonl((ntohl(prevReceivePacket.seqNum) + 1) % SEQ_NUM_MOD);
                    packet.length = 0;
                    packet.flags = SYN_FLAG | ACK_FLAG;
                    prevSentPacket = packet;
                    sendSegment(&packet, SEND);

                    // set next expected seq num to this segment's seq # + 1
                    // represents the start of data
                    nextExpectSeqNum = (ntohl(prevReceivePacket.seqNum) + 1) % SEQ_NUM_MOD; //add length to it in the for loop

                    state = SERV_WAIT_FOR_SYN_CLIENT_ACK;
                }      
            }
            else if(state == SERV_WAIT_FOR_SYN_CLIENT_ACK){
                // read data: look for ACK in packet for the SYN-ACK sent earlier
                // alternatively, the SYN|ACK sent by server could have been lost
                // so the SYN gets resent
                memset(&packet, 0, HEADER_SIZE);
                readAmt = recvfrom(sockfd, (char*)&packet, PACKET_SIZE, 0, 
                                    (struct sockaddr*)&cli_addr, &clilen);
                if(readAmt < 0){
                    fprintf(stderr, "lab 2: error reading from socket: %s", strerror(errno));
                    exit(OTHER_ERROR);
                }
                printSegment(&packet, RECEIVE);
                prevReceivePacket = packet;

                if((packet.flags & SYN_FLAG) && !(packet.flags & ACK_FLAG)){
                    // syn ack was lost so client resends the initial SYN
                    // technically in the wrong state here
                    // we want to resend the SYN-ACK as dup ack
                    // but use SEND and not RESEND
                    packet.seqNum = htonl(initialSeqNum); // do not generate new ISN?
                    packet.ackNum = htonl((ntohl(prevReceivePacket.seqNum) + 1) % SEQ_NUM_MOD); //don't forget mod arithmetic
                    packet.length = 0;
                    packet.flags = SYN_FLAG | ACK_FLAG;
                    
                    sendSegment(&packet, SEND | DUP_ACK);
                    prevSentPacket = packet;
                    // and stay in same state
                }
                else{
                    // we got an ACK reply to SYN|ACK
                    // OR  this packet got lost and the ones after arrived instead...
                    if(packet.flags & ACK_FLAG){
                        // we got the first one! this falls under the class of
                        // packet arriving in order - NOT ALWAYS
                        // we CANNOT use packet.seqNum = expectedSeqNum
                        // because expectedSeqNum has not yet been initialized
                        assert(packet.flags & ACK_FLAG);               
                        //we got the packet, so now open a file and start recording data
                        // only if this is not a DUPLICATE
                        if(!gotFirstDataPacket){
                            // first time getting the initial payload - in order case
                            sprintf(fileName, "%d.file", fileNumber);
                            fd = creat(fileName, CREATE_PERMISSIONS);
                            if(fd < 0){
                                fprintf(stderr, "lab 2: error creating file: %s", strerror(errno));
                                exit(SYS_CALL_ERROR);
                            }                          
                            // SR logic
                            packets[nextExpectedIndex] = packet;
                            received[nextExpectedIndex] = 1;
                            int startIndex = nextExpectedIndex;
                            
                            //ONLY FOR FIRST DATA PACKET (the one with an ACK)
                            //we need to initialize nextExpectSeqNum based on the random
                            // ISN sent from the client (+1) to the SYN sent earlier
                            
                            int numPacketsToRead = 0;
                            while(received[nextExpectedIndex]){
                                received[nextExpectedIndex] = 0;
                                nextExpectSeqNum = (nextExpectSeqNum + 
                                                    ntohs(packets[nextExpectedIndex].length)) % SEQ_NUM_MOD;
                                if(DEBUG){
                                    fprintf(stderr, "Next expected seq # is now %u\n", nextExpectSeqNum);
                                }
                                //increment the index
                                nextExpectedIndex = (nextExpectedIndex + 1) % WINDOW_SIZE; 
                                numPacketsToRead++;   
                            }
                            if(DEBUG){
                                fprintf(stderr, "Next expected seq num after getting 3rd ACK: %u\n", nextExpectSeqNum);
                            }
                            // int numPacketsToRead = nextExpectedIndex - startIndex; //underflow may occurr!!
                            for(int i = 0; i<numPacketsToRead; i++){
                                int index = (startIndex + i) % WINDOW_SIZE;
                                if(DEBUG){fprintf(stderr, "Reading packet index %d of %d\n", index, numPacketsToRead);}
                                if(write(fd, packets[index].data, ntohs(packets[index].length)) < 0){
                                    fprintf(stderr, "lab 2: error writing to file: %s\n", strerror(errno));
                                    exit(SYS_CALL_ERROR);
                                }
                            }
                            // END SR logic 

                            if(DEBUG){fprintf(stderr, "Got ack reply to syn-ack\n");}

                            // send ACK reply
                            memset(&packet, 0, HEADER_SIZE);
                            serverSeqNum = ntohl(prevReceivePacket.ackNum);
                            packet.seqNum = htonl(constantServSeqNum);

                            packet.ackNum = htonl((ntohl(prevReceivePacket.seqNum) + 
                                                    ntohs(prevReceivePacket.length)) % SEQ_NUM_MOD);
                            // length 0
                            packet.flags = ACK_FLAG;
                            // no data in ack reply
                            sendSegment(&packet, SEND); //this ACK may be lost!!
                            prevSentPacket = packet;
                            state = SERV_WAIT_FOR_DATA;
                        }
                        else{ //we gotten the first packet already but ACK lost
                            // this is a duplicate: send ACK with DUP-ACK
                            // we discard and simply send back the ACK
                            // this is the "behind" case
                            
                            // this code will never be reached (hopefully) because after getting
                            // the first data with ACK we will change to
                            // SERV_WAIT_FOR_DATA state
                            if(DEBUG){
                                fprintf(stderr, "This should never be printed, got DUPLICATE ack reply to syn-ack\n");
                                exit(PROGRAM_ERROR);
                            }

                            memset(&packet, 0, HEADER_SIZE);
                            serverSeqNum = ntohl(prevReceivePacket.ackNum);
                            packet.seqNum = htonl(constantServSeqNum);

                            packet.ackNum = htonl((ntohl(prevReceivePacket.seqNum) + 
                                                    ntohs(prevReceivePacket.length)) % SEQ_NUM_MOD);
                            // length 0
                            packet.flags = ACK_FLAG;
                            // no data in ack reply
                            sendSegment(&packet, SEND | DUP_ACK);
                            prevSentPacket = packet;
                            state = SERV_WAIT_FOR_DATA;
                        }
                        
                    }
                    else{ //packets 2-10 arrived in initial window and the 1st has not yet arrived
                        // we stay in this state until we get the ACK for 
                        // the SYN-ACK sent earlier
                        // this, this else deals with the up to 9 other segments
                        // that are sent initially

                        // we are guaranteed that these packets come after the ACK packet
                        // but for general transmission, no guarantee                  
                        uint32_t receivedSeqNum = ntohl(prevReceivePacket.seqNum);
                        if(ntohl(prevReceivePacket.seqNum) < nextExpectSeqNum){
                            // wrap around occurred, normally it should be greater
                            receivedSeqNum = ntohl(prevReceivePacket.seqNum) + SEQ_NUM_MOD;
                        }
                        // this subtraction okay because we account for wraparound
                        int indexOffset = (int)(ceil((receivedSeqNum - nextExpectSeqNum) / (double)(PAYLOAD_SIZE)));
                        if(DEBUG){
                            assert(indexOffset >= 0); //make sure it's nonnegative
                        }
                        
                        // how much to offset the index from what's expected
                        int index = (nextExpectedIndex + indexOffset) % WINDOW_SIZE;

                        // set up send ACK for this packet
                        memset(&packet, 0, HEADER_SIZE);
                        // serverSeqNum = ntohl(prevReceivePacket.ackNum);
                        packet.seqNum = htonl(constantServSeqNum);

                        packet.ackNum = htonl((ntohl(prevReceivePacket.seqNum) + 
                                                ntohs(prevReceivePacket.length)) % SEQ_NUM_MOD);
                        // length 0
                        packet.flags = ACK_FLAG;
                        if(DEBUG){
                            fprintf(stderr, "Checking if we got packet at index %d\n", index);
                        fprintf(stderr, "Next expected index: %d; indexOffset: %d\n", nextExpectedIndex, indexOffset);
                        fprintf(stderr, "Next expected seq #: %u; got seq # %u\n", nextExpectSeqNum, receivedSeqNum);

                        }
                        
                        if(!received[index]){
                            // first time getting this segment
                            // resolves issue if the ACK is lost and not the data itself
                            // then client will retransmit but we should just discard it
                            // but we need to resend the ACK so that client can 
                            // mark it as received on its side so it can move on
                            // do we print anything out if discarding the packet?
                            // ANS: yes, see other case below
                            received[index] = 1;
                            packets[index] = prevReceivePacket; //save packet in buffer
                            sendSegment(&packet, SEND);
                            prevSentPacket = packet;
                        }
                        else{ //we got 2-9th packet but the ACK got lost
                            // duplicate: do not copy into buffer, just DUP-ACK it
                            // for the client
                            sendSegment(&packet, SEND | DUP_ACK);
                            prevSentPacket = packet;
                        }
                    }
                }
            }
            else if(state == SERV_WAIT_FOR_DATA){
                // normal reading of data plus maybe FIN packet from client
                memset(&packet, 0, PACKET_SIZE);
                readAmt = recvfrom(sockfd, (char*)&packet, PACKET_SIZE, 0, 
                                    (struct sockaddr*)&cli_addr, &clilen);
                if(readAmt < 0){
                    fprintf(stderr, "lab 2: error reading from socket: %s", strerror(errno));
                    exit(OTHER_ERROR);
                }
                printSegment(&packet, RECEIVE);
                
                // what if ACK for the 1st data packet (reply to the SYN-ACK) is lost?
                // the we would have the ACK flag set
                // but we can just treat it as an out of order packet
                prevReceivePacket = packet;

                // some code here if it gets a FIN packet instead
                if(packet.flags & FIN_FLAG){
                    // got FIN packet, client initiated teardown, reply with ACK
                    if(DEBUG){
                        fprintf(stderr, "Got FIN from client\n");
                    }
                    memset(&packet, 0, HEADER_SIZE);
                    packet.seqNum = htonl(serverSeqNum);
                    packet.ackNum = htonl((ntohl(prevReceivePacket.seqNum) + 1) % SEQ_NUM_MOD);
                    packet.flags = ACK_FLAG;
                    sendSegment(&packet, SEND); //reply to FIN with ACK
                    prevSentPacket = packet;

                    // also send its own FIN packet
                    if(DEBUG){
                        fprintf(stderr, "Server sending its FIN\n");
                    }
                    memset(&packet, 0, HEADER_SIZE);
                    packet.seqNum = htonl(serverSeqNum);
                    // length 0, ack number 0
                    packet.flags = FIN_FLAG;
                    sendSegment(&packet, SEND); //send the server FIN

                    // save time when this FIN was sent
                    if(clock_gettime(CLOCK_MONOTONIC, &sentFINTime) < 0){
                        fprintf(stderr, "lab 2: failed to get current time: %s\n", strerror(errno));
                        exit(SYS_CALL_ERROR);
                    }
                    prevSentPacket = packet;
                    serverFINPacket = packet;
                    state = SERV_WAIT_FOR_FIN_CLIENT_ACK;
                }
                else{
                    // not a FIN packet so just normal data
                    if(DEBUG){
                        fprintf(stderr, "Next expected seq #: %u, but got %u\n", nextExpectSeqNum, ntohl(prevReceivePacket.seqNum));
                    }
                    //2 cases: segment is in order or it's not
                    if(ntohl(prevReceivePacket.seqNum) == nextExpectSeqNum){
                        // we got the segment we expect, MATCHes what we want
                        // first time getting this packet
                        // SR logic
                        if(DEBUG){
                            fprintf(stderr, "Got in order packet\n");
                        }
                        
                        packets[nextExpectedIndex] = packet;
                        received[nextExpectedIndex] = 1;
                        int startIndex = nextExpectedIndex;
                        int numPacketsToRead = 0;
                        while(received[nextExpectedIndex]){
                            // advance we get to a segment we haven't gotten yet
                            received[nextExpectedIndex] = 0;
                            nextExpectSeqNum = (nextExpectSeqNum + 
                                                ntohs(packets[nextExpectedIndex].length)) % SEQ_NUM_MOD;
                            
                            //increment the index
                            nextExpectedIndex = (nextExpectedIndex + 1) % WINDOW_SIZE;
                            numPacketsToRead++;
                        }
                        // int numPacketsToRead = nextExpectedIndex - startIndex;
                        for(int i = 0; i<numPacketsToRead; i++){
                            int index = (startIndex + i) % WINDOW_SIZE;
                            if(write(fd, packets[index].data, ntohs(packets[index].length)) < 0){
                                fprintf(stderr, "lab 2: error writing to file: %s\n", strerror(errno));
                                exit(SYS_CALL_ERROR);
                            }


                        }
                        // END SR logic    

                        if(DEBUG){fprintf(stderr, "Got data in order, wrote %d to file\n", numPacketsToRead);}

                        memset(&packet, 0, HEADER_SIZE);
                        // serverSeqNum = ntohl(prevReceivePacket.ackNum);
                        packet.seqNum = htonl(constantServSeqNum);

                        packet.ackNum = htonl((ntohl(prevReceivePacket.seqNum) + 
                                                ntohs(prevReceivePacket.length)) % SEQ_NUM_MOD);
                        // length 0
                        packet.flags = ACK_FLAG;
                        // no data in ack reply
                        sendSegment(&packet, SEND);
                        prevSentPacket = packet;
                    }
                    else{ //segment is out of order...
                        // two cases: ahead of expectedSeqNum or behind expectedSeqNum
                        int aheadOrBehind = 0;
                        
                        // 2 cases for behind
                        if(DEBUG){
                            fprintf(stderr, "Next expected seq num: %u\n", nextExpectSeqNum);
                        }
                        if(nextExpectSeqNum >= 5120){ //CANNOT underflow, but can overflow
                            if(nextExpectSeqNum - ntohl(prevReceivePacket.seqNum) < (10*PAYLOAD_SIZE + 1)){
                                // what we got is within 5120 of the nextExpectedSeqNum
                                // so it's behind; if it was ahead and overflowed, would be more than that
                                aheadOrBehind = BEHIND;
                                if(DEBUG){
                                    fprintf(stderr, "1 Received packet behind expected, no underflow\n");
                                }
                            }
                            else{
                                aheadOrBehind = AHEAD;
                                // big nextExpectSeqNum, caused next packet to overflow
                                if(DEBUG){
                                    fprintf(stderr, "Received packet ahead of expected, OVERFLOW\n");
                                }
                            }
                            // cannot just do expectSeqNum - 5120 > 0 because it's unsigned
                            // if expectSeqNum = 2, then 2 - 5120 = big number and this evaluates
                            // to true when it shouldn't 
                        }
                        else{
                            uint32_t adjustedNextExpectedSeqNum = nextExpectSeqNum + SEQ_NUM_MOD;                      
                            if(adjustedNextExpectedSeqNum - ntohl(prevReceivePacket.seqNum) < (10*PAYLOAD_SIZE + 1)
                                ){
                                // must be approx < 5120, so let's do 5121
                                // also when unadjusted, it should be bigger
                                aheadOrBehind = BEHIND; //behind with underflow
                                if(DEBUG){
                                    fprintf(stderr, "Received packet behind expected, UNDERFLOW\n");
                                }
                            }
                            else if(nextExpectSeqNum > ntohl(prevReceivePacket.seqNum) && 
                            ((nextExpectSeqNum - ntohl(prevReceivePacket.seqNum)) < (10*PAYLOAD_SIZE + 1))){
                                aheadOrBehind = BEHIND; //behind with underflow
                                if(DEBUG){
                                    fprintf(stderr, "Received packet behind expected, no underflow but next expected < 5120\n");
                                }
                            }
                            else{
                                if(DEBUG){
                                    fprintf(stderr, "Received packet ahead of expected, no overflow\n");
                                }
                                aheadOrBehind = AHEAD;
                            }
                        }

                        //ahead:
                        if(aheadOrBehind == AHEAD){
                            uint32_t receivedSeqNum;
                            if(DEBUG){
                                fprintf(stderr, "Ahead received: ");
                                for(int tfIndex = 0; tfIndex < 10; tfIndex++){
                                    fprintf(stderr, "%d ", received[tfIndex]);
                                }
                                fprintf(stderr, "\n");
                            }
                            
                            receivedSeqNum = ntohl(prevReceivePacket.seqNum);
                            if(ntohl(prevReceivePacket.seqNum) < nextExpectSeqNum){
                                // wrap around occurred (overflow)
                                // remember both interpreted as unsigned!!
                                receivedSeqNum = ntohl(prevReceivePacket.seqNum) + SEQ_NUM_MOD;
                            }
                            int indexOffset = (int)(ceil((receivedSeqNum - nextExpectSeqNum) / (double)(PAYLOAD_SIZE)));
                            // how much to offset the index from what's expected
                            int index = (nextExpectedIndex + indexOffset) % WINDOW_SIZE;
                            // DO NOT change nextExpectedIndex b/c this is out of order
                            // set up send ACK for this packet
                            memset(&packet, 0, HEADER_SIZE);
                            // serverSeqNum = ntohl(prevReceivePacket.ackNum);
                            packet.seqNum = htonl(constantServSeqNum);

                            packet.ackNum = htonl((ntohl(prevReceivePacket.seqNum) + 
                                                    ntohs(prevReceivePacket.length)) % SEQ_NUM_MOD);
                            // length 0
                            packet.flags = ACK_FLAG;
                            if(DEBUG){fprintf(stderr, "Received packet ahead at index %d\n", index);}
                            if(!received[index]){
                                // first time getting this segment: save it and marked as recvd
                                // resolves issue if the ACK is lost and not the data itself
                                // then client will retransmit but we should just discard it
                                // but we need to resend the ACK so that client can 
                                // mark it as received on its side so it can move on
                                // do we print anything out if discarding the packet?
                                // i think so, RECV and SEND with DUP-ACK (see other case below)
                                if(DEBUG){fprintf(stderr, "First time getting ahead of expected segment\n");}
                                received[index] = 1;
                                packets[index] = prevReceivePacket;
                                sendSegment(&packet, SEND);
                                prevSentPacket = packet;
                            }
                            else{ //we got the packet earlier but the ACK got lost
                                // duplicate: do not copy into buffer, just ACK it
                                // for the client
                                if(DEBUG){fprintf(stderr, "Got this segment before, sending DUP-ACK and discarding\n");}
                                sendSegment(&packet, SEND | DUP_ACK);
                                prevSentPacket = packet;
                            }
                        }
                        else if(aheadOrBehind == BEHIND){
                            //behind
                            // we have already saved it and possibly even written it to disk already
                            // to the file
                            // so just send an ACK
                            if(DEBUG){fprintf(stderr, "Got packet that is behind, ACK sent but lost\n");}

                            memset(&packet, 0, HEADER_SIZE);
                            // serverSeqNum = ntohl(prevReceivePacket.ackNum);
                            packet.seqNum = htonl(constantServSeqNum);

                            packet.ackNum = htonl((ntohl(prevReceivePacket.seqNum) + 
                                                    ntohs(prevReceivePacket.length)) % SEQ_NUM_MOD);
                            // length 0
                            packet.flags = ACK_FLAG;
                            // no data in ack reply
                            sendSegment(&packet, SEND | DUP_ACK);
                            prevSentPacket = packet;
                        }
                        else{
                            if(DEBUG){fprintf(stderr, "Ahead/behind: this line should never be reached!\n");}
                        }
                    }
                }
            }
            else if(state == SERV_WAIT_FOR_FIN_CLIENT_ACK){
                // got an ACK from client for the FIN that server sent
                // this completes the connection

                // OR, the ACK to the client's FIN was lost and client
                // resends its FIN

                memset(&packet, 0, PACKET_SIZE);
                readAmt = recvfrom(sockfd, (char*)&packet, PACKET_SIZE, 0, 
                                    (struct sockaddr*)&cli_addr, &clilen);
                if(readAmt < 0){
                    fprintf(stderr, "lab 2: error reading from socket: %s", strerror(errno));
                    exit(OTHER_ERROR);
                }
                prevReceivePacket = packet;
                printSegment(&packet, RECEIVE);

                // got FIN instead - because ACK to client's FIN was lost
                // sample output does not deal with this case, seems to assume
                // that we will get the ACK for server's FIN and that will just
                // automatically close the process
                if((prevReceivePacket.flags & FIN_FLAG)){
                    // got the FIN again, may occur if server ACK for client FIN is lost
                    // I think this code will never execute
                    // we only retry if the ACK didnt arrive for server's FIN also
                    // if ACK for server FIN arrives, we will never reach this code
                    if(DEBUG){
                        fprintf(stderr, "Got retranmitted FIN from client\n");
                    }
                    memset(&packet, 0, HEADER_SIZE);
                    packet.seqNum = htonl(serverSeqNum);
                    packet.ackNum = htonl((ntohl(prevReceivePacket.seqNum) + 1) % SEQ_NUM_MOD);
                    packet.flags = ACK_FLAG;
                    sendSegment(&packet, SEND | DUP_ACK); //reply to FIN with ACK
                    prevSentPacket = packet;

                    //if the FIN was lost, we detect it with timeout and resend it
                    // above before POLLIN

                }
                else if(prevReceivePacket.flags & ACK_FLAG){
                    //once it gets this packet, connection is over
                    // server->client connection closed
                    // "close" connection - not really, just increment
                    // file num and reset to waiting for SYN state
                    if(DEBUG){
                        fprintf(stderr, "----------FINISHED receiving file %d----------\n", fileNumber);
                    }
                    fileNumber++;
                    state = SERV_WAIT_FOR_SYN;
                    
                    //reset SR logic!!
                    nextExpectSeqNum = 0;
                    nextExpectedIndex = 0;
                    for(int i = 0; i<10; i++){
                        received[i] = 0;
                    }
                    gotFirstDataPacket = 0;
                    fflush(stdout);
                    close(fd); //close the currently open file
                }

                //otherwise, we just sit and wait
                // timeout may occur for server sent FIN, if so, resend above
                // (outside this POLLIN)
            }
        }        
    }

    // this line should never be reached
    if(DEBUG){
        fprintf(stderr, "Exited while loop, this line shouldn't be reached\n");
    }
    exit(NORMAL_EXIT);
}
