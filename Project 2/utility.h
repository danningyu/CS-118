#include <stdint.h>
#include <stdlib.h>

#define NORMAL_EXIT 0
#define SYS_CALL_ERROR 1
#define OTHER_ERROR 2
#define ARGS_ERROR 3
#define PROGRAM_ERROR 4

#define PACKET_SIZE 524
#define PAYLOAD_SIZE 512
#define HEADER_SIZE 12
#define SEQ_NUM_MOD 25601
#define FIN_WAIT_SECONDS 2

#define WINDOW_SIZE 10

#define NS_TO_S 1000000000

#define BUFFER_SIZE 1024

#define SYN_FLAG 0x01
#define FIN_FLAG 0x02
#define ACK_FLAG 0x04

#define SEND 0x01
#define RECEIVE 0x02
#define RESEND 0x04
#define TIMEOUT 0x08
#define DUP_ACK 0x10

#define DEBUG 0

#define SERV_WAIT_FOR_SYN 0
#define CLI_WAIT_FOR_SYN_ACK 1
#define CLI_TRANSMITTING 2
#define CLI_TERMINATING 3
#define SERV_WAIT_FOR_SYN_CLIENT_ACK 4
#define SERV_WAIT_FOR_DATA 5
#define CLI_WAIT_FOR_FIN_ACK 6
#define SERV_WAIT_FOR_FIN_CLIENT_ACK 7

struct packet {
    uint32_t seqNum;
    uint32_t ackNum;
    uint16_t length;
    uint8_t flags;
    // & 0x1: SYN
    // & 0x2: FIN
    // & 0x4: ACK
    uint8_t padding;
    uint8_t data[PAYLOAD_SIZE];
};

typedef struct packet packet_t;

void printSegment(packet_t* packetInput, int type){
    packet_t packet = *(packetInput);
    
    if(DEBUG){
        fprintf(stderr, "Debug: type %d, packet with %u,%u,%u,%u\n", 
                type, ntohl(packet.seqNum), ntohl(packet.ackNum), 
                ntohs(packet.length), packet.flags);
    }
    
    if(type & SEND){
        printf("SEND");
    }
    else if(type & RECEIVE){
        printf("RECV");
    }
    else if(type & RESEND){
        printf("RESEND");
    }

    // timeout printing occurs in the client/server file

    if(!(type & TIMEOUT)){
        printf(" %u %u", ntohl(packet.seqNum), ntohl(packet.ackNum));
        if(packet.flags & SYN_FLAG){
            printf(" SYN");
        }
        if(packet.flags & FIN_FLAG){
            printf(" FIN");
        }
        if(packet.flags & ACK_FLAG){
            // ACK flag
            if(type & DUP_ACK){
                printf(" DUP-ACK");
            }
            else{
                printf(" ACK");
            }
        }
        printf("\n");
    }
}
