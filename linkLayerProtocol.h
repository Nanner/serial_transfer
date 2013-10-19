#ifndef _LINKLAYERPROTOCOL_H
#define _LINKLAYERPROTOCOL_H

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "frame.h"
#include "applicationLayer.h"

#define TRANSMITTER 1
#define RECEIVER 0

#define COM1 1
#define COM2 2
#define COM1_PORT "/dev/ttyS0"
#define COM2_PORT "/dev/ttyS1"
#define BAUDRATE B9600

#define FALSE 0
#define TRUE 1

#define SET_UA_TIMEOUT 10
#define RECEIVE_INFO_TIMEOUT 10

typedef struct {
    char port[20];
    int baudRate;
    unsigned int sequenceNumber;
    unsigned int timeout;
    unsigned int numTransmissions;
    struct termios oldtio;
    size_t maxInformationSize;
    size_t frameSize;
    unsigned int frameBCC2Index;
    unsigned int frameTrailerIndex;
} LinkLayer;

extern LinkLayer linkLayerConf;

extern int retryCounter;

int llopen(int port, int role);

int llclose(int fd);

int receivePacket(char* packet, size_t packetLength);

int sendPacket(char* packet, size_t packetLength);

int setLink();

int waitForLink();

//int llread(int fd, char * buffer); //Might be the same as fromPhysical

//int llwrite(int fd, char * buffer, int length); //Might be the same as toPhysical

int toPhysical(char* frame);

int fromPhysical(char* frame, int exitOnTimeout);

void timeout();

#endif