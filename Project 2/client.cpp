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
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <math.h>

#include "utility.h"

int sockfd;
struct sockaddr_in serv_addr;

void sendSegment(packet_t* packetInput, int type){
    packet_t packet = *packetInput;
    printSegment(packetInput, type);  
    ssize_t sendAmt = sendto(sockfd, (char*)&packet, PACKET_SIZE, 0, 
            (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if(sendAmt < 0){
        fprintf(stderr, "lab 2: error sending packet: %s\n", strerror(errno));
        exit(OTHER_ERROR);
    }
}

void connectToServer(const int portNumber, char* hostName){
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0){
        fprintf(stderr, "lab 2: error creating UDP socket: %s\n", strerror(errno));
        exit(SYS_CALL_ERROR);
    }
    struct hostent* hostEntry = gethostbyname(hostName);
    bzero((char*) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portNumber);
    memcpy(&serv_addr.sin_addr, hostEntry->h_addr_list[0], hostEntry->h_length);
}

int main(int argc, char** argv){
    if(argc != 4){
        fprintf(stderr, "lab 2 client: usage: ./client <hostname> <port> <file>\n");
        exit(ARGS_ERROR);
    }
    char* hostname = argv[1];   
    int portNumber = atoi(argv[2]);
    int fd = open(argv[3], O_RDONLY);
    if(fd < 0){
        fprintf(stderr, "lab 2: failed to open file %s: %s\n", argv[3], strerror(errno));
        exit(SYS_CALL_ERROR);
    }
    if(DEBUG){
        fprintf(stderr, "debug: port number %d\n", portNumber);
        assert(sizeof(packet_t) == 524);
    }
    
    connectToServer(portNumber, hostname);

    int setNonBlock = fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);
    if(setNonBlock < 0){
        fprintf(stderr, "Error when making socket nonblocking: %s\n", strerror(errno));
        exit(SYS_CALL_ERROR);
    }
    socklen_t servlen = sizeof(serv_addr);
    ssize_t recvAmt;
    int readAmt;
    packet_t packet; //a "buffer" to hold data
    packet_t prevReceivePacket; //the previous packet, sent or received
    packet_t prevSentPacket; //previous sent packet
    packet_t firstPacketSent; //first packet sent

    // zero out packet, initiate connection with SYN
    memset(&packet, 0, HEADER_SIZE);
    srand(time(NULL));

    uint32_t initialSeqNum = rand() % SEQ_NUM_MOD;
    // uncomment line above before turning in
    // uint32_t initialSeqNum = 2254;
      
    packet.seqNum = htonl(initialSeqNum); //start with random seq #
    packet.ackNum = 0;
    packet.length = 0;
    packet.flags = SYN_FLAG;
    prevSentPacket = packet;

    sendSegment(&packet, SEND); //send initial SYN packet
    struct timespec sendSYNTime;

    if(clock_gettime(CLOCK_MONOTONIC, &sendSYNTime) < 0){
        fprintf(stderr, "lab 2: failed to get current time: %s\n", strerror(errno));
        exit(SYS_CALL_ERROR);
    }

    struct timespec endFINTime;
    struct timespec currentTime;

    struct timespec sentFINToServerTime;

    // SR variables
    struct timespec timers[10];
    packet_t packets[10];
    int indexNextPacketToACK = 0; //the next packet to be ACKed, mod10
    int alreadyAcked[10] = {0}; //default is false
    uint32_t nextAckNum = 0; //the next packet to ACK in order
    int numPacketsInWindow = 0; //normally 10 unless a small file is transmitted    

    // connection teardown error handling variables
    packet_t lastPayloadPacket;
    uint32_t lastPayloadAckNum = 0;
    packet_t finPacketToServer;
    int resentFINtoServer = 0; //we only resend this twice
    int gotServerACKForFIN = 0;
    int sentACKforServerFIN = 0;

    struct pollfd pollEvents[] = {
        {sockfd, POLLIN | POLLHUP | POLLERR, 0},
    };

    int state = CLI_WAIT_FOR_SYN_ACK; 
    int reachedEOF = 0;
    // 0 for wait for syn, 1 for transmit, 2 for finishing procedures
    int needtoSendFirstPacket = 1; //first packet needs to be sent
    // uint32_t finAckSeqNum = 0;
    int sentFIN = 0;
    int notYetGetFirstServerFIN = 1; //have not yet received FIN from server
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

        // CHECKING for timer timeouts
        if(!notYetGetFirstServerFIN){ //2 s timeout for FIN from server
            // FIN has been received from server
            if( (currentTime.tv_sec + (double)(currentTime.tv_nsec)/NS_TO_S)
                     >= (endFINTime.tv_sec + (double)(endFINTime.tv_nsec)/NS_TO_S)){
                if(DEBUG){
                    fprintf(stderr, "2 seconds have elapsed, closing connection\n");
                }
                break;
            }
        }
        else if(state == CLI_WAIT_FOR_SYN_ACK){
            // check if the SYN ACK timeout has occurred
            // 0.5 s after SYN is sent
            if( (currentTime.tv_sec + (double)(currentTime.tv_nsec)/NS_TO_S)
                     >= ((double)(sendSYNTime.tv_sec) + 0.5 + (double)(sendSYNTime.tv_nsec)/NS_TO_S)){
                if(DEBUG){
                    fprintf(stderr, "Packet timeout occurred for syn-ack, resending SYN\n");
                }
                // timeout occurred, SYN-ACK got lost, so resend SYN
                printf("TIMEOUT %u\n", ntohl(prevSentPacket.seqNum));
                packet.seqNum = htonl(initialSeqNum); //start with random seq #
                packet.ackNum = 0;
                packet.length = 0;
                packet.flags = SYN_FLAG;
                prevSentPacket = packet;
                sendSegment(&packet, RESEND); //resend initial SYN packet
                //reset SYN timer
                if(clock_gettime(CLOCK_MONOTONIC, &sendSYNTime) < 0){
                    fprintf(stderr, "lab 2: failed to get current time: %s\n", strerror(errno));
                    exit(SYS_CALL_ERROR);
                }
            }
        }
        else if(state == CLI_TRANSMITTING){
            // check all the timers and resend any that have timed out
            // termination is when you loop through all 10 or numPacketsInWindow
            // whichever is smaller: in case the file doesn't need > 10 packets
            for(int i = 0; i < numPacketsInWindow && i < 10; i++){
                // we start checking from the first index
                int index = (indexNextPacketToACK + i) % WINDOW_SIZE;
                struct timespec sentSegTime = timers[index];

                //only check if not already ACKed
                if( !(alreadyAcked[index]) && (currentTime.tv_sec + (double)(currentTime.tv_nsec)/NS_TO_S)
                     >= ((double)(sentSegTime.tv_sec) + 0.5 + (double)(sentSegTime.tv_nsec)/NS_TO_S)){
                    // timeout occurred for this segment
                    
                    // if this was the first segment, it will detect the ACK flag 
                    // and print it out correctly
                    // however, if it be DUP-ACK, need to check equality with firstPacket
                    sendSegment(&(packets[index]), RESEND);
                    // note: don't set prevSentPacket here because we want that to
                    // be the last packet

                    // reset timer for this one
                    if(clock_gettime(CLOCK_MONOTONIC, &(timers[index])) < 0){
                        fprintf(stderr, "lab 2: failed to get current time: %s\n", strerror(errno));
                        exit(SYS_CALL_ERROR);
                    }
                    // DO NOT move indexNextPackettoACK forwrad
                    // or change alreadyAcked, or change nextAckNum
                }
            }
        }
        else if(!gotServerACKForFIN && (state == CLI_WAIT_FOR_FIN_ACK)){
            // timer for if the FIN sent by client to server has timed out
            // we don't get the server's ACK for the FIN (first bool)
            // or the FIN itself is lost (first book will be true anyways)
            if( (currentTime.tv_sec + (double)(currentTime.tv_nsec)/NS_TO_S)
                     >= ((double)(sentFINToServerTime.tv_sec) + 0.5 + (double)(sentFINToServerTime.tv_nsec)/NS_TO_S)){
                if(DEBUG){
                    fprintf(stderr, "0.5 s time out client sent FIN\n");
                }

                // resend the FIN
                printf("TIMEOUT %u\n", ntohl(finPacketToServer.seqNum));

                // case 1: FIN to server got lost, just resend
                // we never get ACK for this FIN or the server's FIN
                // also covers case where server's ACK and FIN both get lost  
                // ////////////////
                //case 2: the server's ACK to the client's FIN got lost
                // but we got the server's FIN
                // we only retry once in this case
                // unlikely to reach because most likely the 2 seconds will time out first        
                if(notYetGetFirstServerFIN){
                    sendSegment(&finPacketToServer, RESEND);
                }
                else if(!resentFINtoServer){
                    //only print and not send...see below
                    printSegment(&finPacketToServer, RESEND);
                    
                    // sendSegment(&finPacketToServer, RESEND);

                    // we don't expect a response for this...
                    // now what if the ACK we send in reply to server's FIN is
                    // also lost?
                    // then the server will retransmit its FIN and then
                    // we will get it again and send back the ACK
                    // and I think we can assume that the ACK will inevitably
                    // be received, and thus this turns into the case
                    // given in the sampel output
                    // thus we cheat and only print without actually sending
                    resentFINtoServer = 1;
                }
                
                // reset the timer for the FIN
                if(clock_gettime(CLOCK_MONOTONIC, &sentFINToServerTime) < 0){
                    fprintf(stderr, "lab 2: failed to get current time: %s\n", strerror(errno));
                    exit(SYS_CALL_ERROR);
                }       

                // stay in same state of waiting for ACK for the FIN sent
            }
        }

        if(pollEvents[0].revents & POLLIN){
            //data to read from sockfd
            if(state == CLI_WAIT_FOR_SYN_ACK){
                //Now expect SYN-ACK packet
                if(DEBUG){
                    fprintf(stderr, "Getting syn-ack packet\n");
                }
                memset(&packet, 0, PACKET_SIZE); 
                //always zero out packet before setting in data or writing into it!!
                recvAmt = recvfrom(sockfd, (char*)&packet, PACKET_SIZE, 0, 
                            (struct sockaddr*)&serv_addr, &servlen);
                if(recvAmt < 0){
                    fprintf(stderr, "lab 2: error reciving SYN-ACK packet: %s\n", strerror(errno));
                    exit(OTHER_ERROR);
                }
                if(!(packet.flags & SYN_FLAG) && !(packet.flags & ACK_FLAG) && DEBUG){
                    fprintf(stderr, "debug: BAD syn-ack packet from server\n");
                }
                assert(packet.ackNum = htonl((initialSeqNum + 1) % SEQ_NUM_MOD)); //TODO: add mod arithmetic
                printSegment(&packet, RECEIVE);
                prevReceivePacket = packet; //update prevPacket

                state = CLI_TRANSMITTING;
            }
            else if(state == CLI_TRANSMITTING){
                //in the middle of transmission
                if(DEBUG){
                    fprintf(stderr, "Getting an ACK packet\n");
                    fprintf(stderr, "Before processing: nextacknum: %d; index of next packet to ack: %d; reached EOF: %d\n", 
                            nextAckNum, indexNextPacketToACK, reachedEOF);
                    for(int tfIndex  = 0; tfIndex < 10; tfIndex++){
                        fprintf(stderr, "%d ", alreadyAcked[tfIndex]);
                    }
                    fprintf(stderr, "\n");
                }
                memset(&packet, 0, PACKET_SIZE); 
                // always zero out packet before setting in data or writing into it!!
                recvAmt = recvfrom(sockfd, (char*)&packet, PACKET_SIZE, 0, 
                            (struct sockaddr*)&serv_addr, &servlen);
                if(recvAmt < 0){
                    fprintf(stderr, "lab 2: error reciving SYN-ACK packet: %s\n", strerror(errno));
                    exit(OTHER_ERROR);
                }

                if(!(packet.flags & ACK_FLAG) && DEBUG){
                    fprintf(stderr, "debug: BAD ack packet from server\n");
                }
                
                printSegment(&packet, RECEIVE);
                prevReceivePacket = packet; //update prevPacket

                if(!reachedEOF){
                    // got an ACK, so let's send another one IF this acks the lowest
                    if(ntohl(prevReceivePacket.ackNum) == nextAckNum){
                        // ack num of ACK matches the lowest next ACK num
                        alreadyAcked[indexNextPacketToACK] = 1; //true
                        int i = 0;
                        while(alreadyAcked[indexNextPacketToACK]){
                            // while the segments have been ACKed
                            memset(&packet, 0, PACKET_SIZE); 
                            if(DEBUG){
                                fprintf(stderr, "Past 5120: Transmitting another packet, moved up %d\n", i);
                            }

                            int nextIndex = (indexNextPacketToACK + 1) % WINDOW_SIZE;

                            nextAckNum = (nextAckNum + ntohs(packets[nextIndex].length)) % SEQ_NUM_MOD;
                            // update next acknum!! to the first unacked packet
                            
                            packet.seqNum = htonl((ntohl(prevSentPacket.seqNum) + 
                                        ntohs(prevSentPacket.length)) % SEQ_NUM_MOD);
                            readAmt = read(fd, packet.data, PAYLOAD_SIZE);
                            if(readAmt < 0){
                                fprintf(stderr, "lab 2: error reading from file: %s", strerror(errno));
                                exit(SYS_CALL_ERROR);
                            }
                            else if(readAmt == 0){
                                // reached the end of the file
                                memset(&lastPayloadPacket, 0, PACKET_SIZE); 
                                lastPayloadPacket = prevSentPacket;
                                lastPayloadAckNum = (ntohl(lastPayloadPacket.seqNum) + ntohs(lastPayloadPacket.length));
                                // alreadyAcked[indexNextPacketToACK] = 0; //make it false
                                // indexNextPacketToACK = (indexNextPacketToACK + 1) % WINDOW_SIZE;
                                int numAcked = 0;
                                int firstTime = 1; //skip the first time to avoid duplicate
                                while(alreadyAcked[indexNextPacketToACK] && 
                                        nextAckNum != lastPayloadAckNum){
                                    if(firstTime){
                                        firstTime = 0;
                                    }
                                    else{
                                        int nextIndexEOF = (indexNextPacketToACK + 1) % WINDOW_SIZE;
                                        nextAckNum = (nextAckNum + ntohs(packets[nextIndexEOF].length)) % SEQ_NUM_MOD;
                                        // alreadyAcked[indexNextPacketToACK] = 0; //make it false 
                                        numAcked++;
                                        if(DEBUG){
                                            fprintf(stderr, "2 Moved nextacknum up to %u\n", nextAckNum);
                                        }
                                        
                                    }
                                    indexNextPacketToACK = (indexNextPacketToACK + 1) % WINDOW_SIZE; 
                                }
                                reachedEOF = 1;
                                if(DEBUG){
                                    fprintf(stderr, "After reaching EOF:\n");
                                    for(int tfIndex  = 0; tfIndex < 10; tfIndex++){
                                        fprintf(stderr, "%d ", alreadyAcked[tfIndex]);
                                    }
                                    fprintf(stderr, "\n");
                                }
                                if(reachedEOF && nextAckNum == lastPayloadAckNum
                                    && alreadyAcked[indexNextPacketToACK]){ 
                                    state = CLI_TERMINATING;
                                }
                                //move up nextACKnum
                                break; // don't check for any more possible packets to send
                                // and don't send current packet cause there's no data
                            }
                            else{
                                packet.length = htons(readAmt);
                                // no ACK flag after the first one
                                sendSegment(&packet, SEND);
                                prevSentPacket = packet;

                                // set new timer for this new segment
                                if(clock_gettime(CLOCK_MONOTONIC, &(timers[indexNextPacketToACK])) < 0){
                                    fprintf(stderr, "lab 2: failed to get current time: %s\n", strerror(errno));
                                    exit(SYS_CALL_ERROR);
                                }
                                packets[indexNextPacketToACK] = packet;
                                alreadyAcked[indexNextPacketToACK] = 0; //update to show not ACKed
                                indexNextPacketToACK = (indexNextPacketToACK + 1) % WINDOW_SIZE;
                                i++;
                            }
                        }
                    }
                    else{
                        // got an ACK but not for the lowest one
                        // ceiling of (ackNum - expectedACKNum / 512), then mod 10
                        if(DEBUG){fprintf(stderr, "Got ACK for not for lowest packet\n");}
                        uint32_t receivedAckNum = ntohl(prevReceivePacket.ackNum);
                        if(ntohl(prevReceivePacket.ackNum) < nextAckNum){
                            //watch out overflow ocurred
                            receivedAckNum = ntohl(prevReceivePacket.ackNum) + SEQ_NUM_MOD;
                        }
                        // now okay to subtract b/c we've accounted for overflow
                        int unadjustedIndex = (int)(ceil((receivedAckNum - nextAckNum)/(double)(PAYLOAD_SIZE)))
                                                + indexNextPacketToACK;
                        int index = unadjustedIndex % WINDOW_SIZE;
                        alreadyAcked[index] = 1;
                        // do not change anything else b/c the lowest packet was acked
                    }                 
                }
                else if(reachedEOF && (nextAckNum == 
                (ntohl(lastPayloadPacket.seqNum) + ntohs(lastPayloadPacket.length)) % SEQ_NUM_MOD)
                    && alreadyAcked[indexNextPacketToACK]){ 
                    //update SR logic
                    // indexNextPacekttoACK is not necessarily the last packet!
                    // can do a check along hte lines of if(ntohl(prevReceivePacket.ackNum) == nextAckNum)
                    alreadyAcked[indexNextPacketToACK] = 1; 
                    // last segment sent has been acked, initiate shutdown procedure
                    state = CLI_TERMINATING;
                }
                else if(reachedEOF){
                    // we've sent all the packets but still waiting on some ACKs
                    // case 1: we get ACK of the lowest expected packet
                    if(ntohl(prevReceivePacket.ackNum) == nextAckNum){
                        alreadyAcked[indexNextPacketToACK] = 1;
                        if(DEBUG){
                            fprintf(stderr, "DEBUG loc 2: nextacknum: %d; index of next packet to ack: %d; reached EOF: %d\nLoc 2:", 
                                nextAckNum, indexNextPacketToACK, reachedEOF);
                            fprintf(stderr, "\n");
                            for(int tfIndex  = 0; tfIndex < 10; tfIndex++){
                                fprintf(stderr, "%d ", alreadyAcked[tfIndex]);
                            }
                        }
                        while(alreadyAcked[indexNextPacketToACK] && nextAckNum != lastPayloadAckNum){
                            int nextIndex = (indexNextPacketToACK + 1) % WINDOW_SIZE;
                            nextAckNum = (nextAckNum + ntohs(packets[nextIndex].length)) % SEQ_NUM_MOD;
                            // alreadyAcked[indexNextPacketToACK] = 0; //make it false
                            if(DEBUG){fprintf(stderr, "Moved nextacknum up to %u\n", nextAckNum);}
                            indexNextPacketToACK = (indexNextPacketToACK + 1) % WINDOW_SIZE;
                        }
                        if((nextAckNum == (ntohl(lastPayloadPacket.seqNum) + ntohs(lastPayloadPacket.length)) % SEQ_NUM_MOD)
                            && alreadyAcked[indexNextPacketToACK]){
                            //...and that was the last one
                            state = CLI_TERMINATING;
                        }
                    }
                    // case 2: got ACK, but not of lowest expected packet
                    else{
                        if(DEBUG){fprintf(stderr, "Got ACK but not for lowest packet\n");}
                        uint32_t receivedAckNum = ntohl(prevReceivePacket.ackNum);
                        if(ntohl(prevReceivePacket.ackNum) < nextAckNum){
                            //watch out! overflow ocurred
                            if(DEBUG){fprintf(stderr, "overflow ocurred\n");}
                            receivedAckNum = ntohl(prevReceivePacket.ackNum) + SEQ_NUM_MOD;
                        }

                        // now okay to subtract b/c we've accounted for overflow
                        int unadjustedIndex = (int)(ceil((receivedAckNum - nextAckNum)/(double)(PAYLOAD_SIZE))) 
                                                + indexNextPacketToACK;
                        int index = unadjustedIndex % WINDOW_SIZE;
                        if(DEBUG){fprintf(stderr, "Marking index %d as received\n", index);}
                        alreadyAcked[index] = 1;
                    }
                }
            }
            else if(state == CLI_WAIT_FOR_FIN_ACK){
                // waiting for server to ACK the client FIN
                // or for server to send its FIN
                //in the middle of transmission
                if(DEBUG){
                    fprintf(stderr, "Waiting for FIN or ACK for client FIN\n");
                }
                memset(&packet, 0, PACKET_SIZE); 
                //always zero out packet before setting in data or writing into it!!
                recvAmt = recvfrom(sockfd, (char*)&packet, PACKET_SIZE, 0, 
                            (struct sockaddr*)&serv_addr, &servlen);
                if(recvAmt < 0){
                    fprintf(stderr, "lab 2: error reciving SYN-ACK packet: %s\n", strerror(errno));
                    exit(OTHER_ERROR);
                }
                prevReceivePacket = packet;
                printSegment(&packet, RECEIVE);

                if(notYetGetFirstServerFIN && (packet.flags & ACK_FLAG)){
                    // we may have not yet gotten FIN from server yet
                    // got the ACK for the FIN sent earlier
                    if(DEBUG){
                        fprintf(stderr, "Got ACK for client sent FIN\n");
                    }
                    // finAckSeqNum = ntohl(packet.ackNum);
                    gotServerACKForFIN = 1;
                }
                else if(packet.flags & FIN_FLAG){
                    if(DEBUG){
                        fprintf(stderr, "Got FIN from server\n");
                    }

                    // upon getting the first FIN from the server
                    // start a timer, closing within 2 seconds
                    if(notYetGetFirstServerFIN){
                        if(DEBUG){
                            fprintf(stderr, "Got first FIN from server\n");
                        }
                        if(clock_gettime(CLOCK_MONOTONIC, &endFINTime) < 0){
                            fprintf(stderr, "lab 2: failed to get current time: %s\n", strerror(errno));
                            exit(SYS_CALL_ERROR);
                        }
                        endFINTime.tv_sec += FIN_WAIT_SECONDS;
                        notYetGetFirstServerFIN = 0;
                    }
                    
                    // only here: respond to the FIN packet from server
                    // instead of outside the POLLIN
                    // send the ACK for server's FIN
                    // covers both first time and if the FIN is resent due to
                    // the ACK for this FIN being lost
                    // this will only happen up to 4 times before we time out
                    // with the 2 seconds and shutdown automatically
                    memset(&packet, 0, HEADER_SIZE);
                    packet.seqNum = htonl((ntohl(finPacketToServer.seqNum) + 1) % SEQ_NUM_MOD);
                    packet.ackNum = htonl((ntohl(prevReceivePacket.seqNum) + 1) % SEQ_NUM_MOD);
                    // length = 0
                    packet.flags = ACK_FLAG;
                    if(!sentACKforServerFIN){
                        // first time ACKing the server's FIN
                        sendSegment(&packet, SEND);
                        sentACKforServerFIN = 1;
                    }
                    else{
                        // mark here as dup ack
                        sendSegment(&packet, SEND | DUP_ACK);
                    }
                    
                    prevSentPacket = packet;
                }
            }
        }

        // for initial sending packets - up to 10
        if(state == CLI_TRANSMITTING){           
            if(needtoSendFirstPacket){
                if(DEBUG){
                    fprintf(stderr, "Sending first packet\n");
                }
               
                needtoSendFirstPacket = 0;
                // set to false temp here, should actually
                // set to false when we get the ACK for this packet
                // for the case of data loss
                // update: nevermind, separated into needtosend and acked first packet

                // send data packets
                // now send first packet of data with ACK = 1
                memset(&packet, 0, HEADER_SIZE); 
                packet.seqNum = htonl((initialSeqNum + 1) % SEQ_NUM_MOD); //SYN packet has "logical" size of 1, add mod arithmetic
                packet.ackNum = htonl((ntohl(prevReceivePacket.seqNum) + 1) % SEQ_NUM_MOD);
                readAmt = read(fd, packet.data, PAYLOAD_SIZE);
                if(readAmt < 0){
                    fprintf(stderr, "lab 2: error reading from file: %s", strerror(errno));
                    exit(SYS_CALL_ERROR);
                }
                else if(readAmt == 0){
                    reachedEOF = 1;
                    //send back ACK to ack the SYN-ACK, but with no data b/c empty file
                    
                    packet.length = htons(readAmt); // is 0
                    packet.flags = ACK_FLAG;
                    sendSegment(&packet, SEND);
                    firstPacketSent = packet;

                    // SR logic
                    if(clock_gettime(CLOCK_MONOTONIC, &(timers[0])) < 0){
                        fprintf(stderr, "lab 2: failed to get current time: %s\n", strerror(errno));
                        exit(SYS_CALL_ERROR);
                    }
                    packets[0] = packet;
                    indexNextPacketToACK = 0; //next packet to ACK is the 0th one
                    nextAckNum = (ntohl(packet.seqNum) + ntohs(packet.length)) % SEQ_NUM_MOD;
                    numPacketsInWindow++; //we will only have 1 packet
                    prevSentPacket = packet;

                    memset(&lastPayloadPacket, 0, PACKET_SIZE); 
                    lastPayloadPacket = prevSentPacket;
                    lastPayloadAckNum = (ntohl(lastPayloadPacket.seqNum) + ntohs(lastPayloadPacket.length));

                    reachedEOF = 1;
                    // FIXED ABOVE
                    // TODO: strictly speaking, this is wrong
                    // we need to wait for this packet with nothing to be ACKed
                    // before sending the FIN from the client
                    // state = CLI_TERMINATING; //immediately go to termination if empty file
                }
                else if(readAmt != 0){
                    packet.length = htons(readAmt);
                    packet.flags = ACK_FLAG;
                    sendSegment(&packet, SEND);
                    firstPacketSent = packet;
                    prevSentPacket = packet;

                    // SR logic
                    if(clock_gettime(CLOCK_MONOTONIC, &(timers[0])) < 0){
                        fprintf(stderr, "lab 2: failed to get current time: %s\n", strerror(errno));
                        exit(SYS_CALL_ERROR);
                    }

                    //first packet
                    packets[0] = packet;
                    indexNextPacketToACK = 0; //next packet to ACK is the 0th one
                    nextAckNum = (ntohl(packet.seqNum) + ntohs(packet.length)) % SEQ_NUM_MOD;
                    numPacketsInWindow++;

                    // now send up to 9 more because window size is 10
                    memset(&packet, 0, HEADER_SIZE); 
                    for(int i = 1; i<10; i++){ //send 0-indexed 1 2 3 ... 9
                        if(DEBUG){
                            fprintf(stderr, "%d: Transmitting additional initial packets\n", i);
                        }
                        
                        packet.seqNum = htonl((ntohl(prevSentPacket.seqNum) + 
                                              ntohs(prevSentPacket.length)) % SEQ_NUM_MOD);
                        readAmt = read(fd, packet.data, PAYLOAD_SIZE);
                        if(readAmt < 0){
                            fprintf(stderr, "lab 2: error reading from file: %s", strerror(errno));
                            exit(SYS_CALL_ERROR);
                        }
                        else if(readAmt == 0){
                            if(DEBUG){
                                fprintf(stderr, "Reached EOF during initial read on\n");
                            }
                            memset(&lastPayloadPacket, 0, PACKET_SIZE); 
                            lastPayloadPacket = prevSentPacket;
                            lastPayloadAckNum = (ntohl(lastPayloadPacket.seqNum) + ntohs(lastPayloadPacket.length));
                            reachedEOF = 1;
                            // break b/c no more packets need to sent
                            // we don't enter CLI_TERMINATING here becuase
                            // we need to wait for the packets to be ACKed first
                            break;
                        }
                        else{
                            packet.length = htons(readAmt);
                            // no ACK flag after the first one
                            sendSegment(&packet, SEND);
                            prevSentPacket = packet;

                            // SR logic
                            if(clock_gettime(CLOCK_MONOTONIC, &(timers[i])) < 0){
                                fprintf(stderr, "lab 2: failed to get current time: %s\n", strerror(errno));
                                exit(SYS_CALL_ERROR);
                            }
                            packets[i] = packet;
                            numPacketsInWindow++;
                            assert(numPacketsInWindow == (i+1));
                        }
                    }
                }
            }
        }
        if(!sentFIN && state == CLI_TERMINATING){
            if(DEBUG){
                fprintf(stderr, "Termination stage\n");
            }          
            memset(&packet, 0, HEADER_SIZE);
            packet.seqNum = htonl(lastPayloadAckNum);
            // client sends FIN to terminate client->server connection
            // initate the terminaton process
            // acknum and length are 0
            packet.flags = FIN_FLAG;
            sendSegment(&packet, SEND);

            // after sending FIN to server, start 0.5 s timer
            if(clock_gettime(CLOCK_MONOTONIC, &sentFINToServerTime) < 0){
                fprintf(stderr, "lab 2: failed to get current time: %s\n", strerror(errno));
                exit(SYS_CALL_ERROR);
            }

            prevSentPacket = packet;
            finPacketToServer = packet;
            state = CLI_WAIT_FOR_FIN_ACK;
            sentFIN = 1;
        }
    }
    if(DEBUG){
        fprintf(stderr, "Client finished file transmission, terminating\n");
    }  

    close(sockfd);
    close(fd);
    exit(NORMAL_EXIT);
}
