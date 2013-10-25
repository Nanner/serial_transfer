#include "linkLayerProtocol.h"

static unsigned int framesSent = 0;
static unsigned int framesResent = 0;
static unsigned int framesReceived = 0;
static unsigned int rejNumber = 0;
static unsigned int timeouts = 0;

int setBaudrate(char * baudrateString){
    speed_t rates[] = {B0, B110, B115200, B1200, B134, B150, B1800, B19200, B200, B230400, B2400, B300, B38400, B4800, B50, B57600, B600, B75, B9600};
    char * rateStrings[]= {"B0", "B110", "B115200", "B1200", "B134", "B150", "B1800", "B19200", "B200", "B230400", "B2400", "B300", "B38400", "B4800", "B50", "B57600", "B600", "B75", "B9600"};
    int ratesSize = 19;
    
    int i;
    for (i = 0; i < ratesSize; i++) {
        if (strcmp(baudrateString, rateStrings[i]) == 0 ) {
            linkLayerConf.baudRate = rates[i];
            return 0;
        }
    }
    
    return 0;
}

int setDataSize(char * dataSizeString){
    int size = atoi(dataSizeString);
    
    if ( size < MINIMUM_DATA_SIZE )
        return -1;
    
    applicationLayerConf.maxPacketSize = size;
    applicationLayerConf.maxDataFieldSize = applicationLayerConf.maxPacketSize - BASE_DATA_PACKET_SIZE;
    linkLayerConf.maxInformationSize = applicationLayerConf.maxPacketSize * 2 + 4;
    linkLayerConf.frameSize = linkLayerConf.maxInformationSize + BASE_FRAME_SIZE;
    
    linkLayerConf.frameBCC2Index = FBCC2(linkLayerConf.maxInformationSize);
    linkLayerConf.frameTrailerIndex = FTRAILERFLAG(linkLayerConf.maxInformationSize);
    
    return 0;
}

int setRetry(char * retryNumberString){
    int numberOfRetries = atoi(retryNumberString);
                                //first transmission + retries
    linkLayerConf.numTransmissions = 1 + numberOfRetries;
    return 0;
}

int setTimeout(char * secondsString){
    int seconds = atoi(secondsString);
    linkLayerConf.sendTimeout = seconds; //seconds until timeout
    return 0;
}

int llopen(int port, int role){

    struct termios newtio;
    /*
     Open serial port device for reading and writing and not as controlling tty
     because we don't want to get killed if linenoise sends CTRL-C.
     */

    if ( port == COM1 ) {
    	applicationLayerConf.fileDescriptor = open(COM1_PORT, O_RDWR | O_NOCTTY );
    } else if ( port == COM2 ) {
    	applicationLayerConf.fileDescriptor = open(COM2_PORT, O_RDWR | O_NOCTTY );
    } else applicationLayerConf.fileDescriptor = -1;

    if (applicationLayerConf.fileDescriptor < 0) {
        perror("port error"); exit(-1); 
    }
    
    if ( tcgetattr(applicationLayerConf.fileDescriptor,&linkLayerConf.oldtio) == -1) { /* save current port settings */
        perror("tcgetattr");
        return -1;
    }
    
    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = linkLayerConf.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    
    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;
    
    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 1;   /* blocking read until 5 chars received */
    
    /*
     VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a
     leitura do(s) próximo(s) caracter(es)
     */
    
    tcflush(applicationLayerConf.fileDescriptor, TCIOFLUSH);
    
    if ( tcsetattr(applicationLayerConf.fileDescriptor,TCSANOW,&newtio) == -1) {
        perror("tcsetattr");
        return -1;
    }

    applicationLayerConf.status = role;
    
    if (role == TRANSMITTER) {
        initializeLog("transmitter");
        int result = setLink();
        
        switch(result) {
            case 0:
                printf("Handshake sucess! Link set.\n");
                break;
            case -1:
                printf("Failed to set link, connection timed out.\n");
                return -1;
                break;
        }
    }
    
    if (role == RECEIVER)
    {
        initializeLog("receiver");
        printf("Waiting for connection...\n");
        int result = waitForLink();
        
        switch(result) {
            case 0:
                printf("Handshake sent, link should be set.\n");
                break;
            case -1:
                printf("Failed to set link, connection timed out.\n");
                return -1;
                break;
        }
    }
    
    linkLayerConf.sequenceNumber = 0;
    
    return applicationLayerConf.fileDescriptor;
}

int llclose(int fd) {

    if ( tcsetattr(applicationLayerConf.fileDescriptor,TCSANOW,&linkLayerConf.oldtio) == -1) {
        perror("tcsetattr");
        exit(-1);
    }
    
    return 0;
}

int setLink() {
    return(sendCommand(SET, UA, linkLayerConf.sendTimeout, linkLayerConf.numTransmissions, RECEIVER_ADDRESS));
}

int waitForLink() {
    unsigned char command;
    int res = receiveCommand(&command, linkLayerConf.receiveTimeout);
    printf("Command: %d\nRes: %d\n", command, res);
    if(command == SET && res != -1) {
        return(sendResponse(UA, RECEIVER_ADDRESS));
    }

    return -1;
}

int closeLink() {
    if(sendCommand(DISC, DISC, linkLayerConf.sendTimeout, linkLayerConf.numTransmissions, RECEIVER_ADDRESS) == 0)
        sendResponse(UA, SENDER_ADDRESS);

    printf("Closing link.\n");
    return(llclose(applicationLayerConf.fileDescriptor));
}

int waitCloseLink() {
    unsigned char command;
    int result = receiveCommand(&command, linkLayerConf.receiveTimeout);
    if(command == DISC && result != -1) {
        return(confirmCloseLink());
    }

    return -1;
}

int confirmCloseLink() {
    if(sendCommand(DISC, UA, linkLayerConf.receiveTimeout, 1, SENDER_ADDRESS) == 0) {
        printf("Closing link.\n");
        return(llclose(applicationLayerConf.fileDescriptor));
    }

    return -1;
}

void timeout() {
    retryCounter++;
}

int toPhysical(unsigned char* frame) {
    tcflush(applicationLayerConf.fileDescriptor, TCIOFLUSH);
    
    /*
    char message[MESSAGE_LEN];
    sprintf(message, "\nSending: %X %X %X %X %X\n",
           frame[FHEADERFLAG],
           frame[FADDRESS],
           frame[FCONTROL],
           frame[FBCC1],
           frame[linkLayerConf.frameTrailerIndex]);
    writeToLog(message);
     */
    
    unsigned char* stuffedFrame = malloc(linkLayerConf.frameSize);
    if(!stuffedFrame) {
        printf("Failed to allocate memory for stuffed frame, terminating\n");
        return -1;
    }

    stuffFrame(frame, stuffedFrame, linkLayerConf.frameSize, linkLayerConf.maxInformationSize);
    long result = write(applicationLayerConf.fileDescriptor, stuffedFrame, linkLayerConf.frameSize);
    writeFrameToLog(frame, SENT);
    free(stuffedFrame);
    framesSent++;
    return (int) result;
}

int fromPhysical(unsigned char* frame, int exitOnTimeout) {
    int curchar = 0;
    unsigned char receivedString[linkLayerConf.frameSize];
    unsigned char buf[linkLayerConf.frameSize];
    int STOP=FALSE;
    long result = 0;
    int currentTry = retryCounter;

    while (STOP==FALSE) {
        result = read(applicationLayerConf.fileDescriptor,buf,1);
        if(result == 1){
            //If we are receiving the first byte, we want to make sure it's a frame header or a frame trailer before we start receiving the frame
            if(curchar == 0) {
                if(buf[0] == FRAMEFLAG) {
                    //So, we only accept the byte if it's a frameflag
                    receivedString[curchar] = buf[0];
                    curchar++;
                }
            //If we are on the second byte but receive yet another frameflag, it means the previous byte we received was the end of some
            //frame, and not the start of the one we want to receive. So, we just "push" our received string one byte down and start receiving
            //the rest of the frame. Since we use byte stuffing, we won't receive a frameflag in the middle of a valid frame, so we can use this.
            } else if(curchar == 1 && buf[0] == FRAMEFLAG) {
                receivedString[0] = buf[0];
            }
            else {
                receivedString[curchar] = buf[0];
                curchar++;
            }
        }
        if (receivedString[curchar-1]==FRAMEFLAG && curchar-1 > 0 && curchar == linkLayerConf.frameSize)
            STOP=TRUE;
        else if (receivedString[curchar-1] == FRAMEFLAG) {
            //Found a frame flag, so we will reset our buffer and the char count
            receivedString[0] = FRAMEFLAG;
            curchar = 1;
        }

        if(exitOnTimeout && (currentTry < retryCounter))
            return -1;
    }

    destuffFrame(receivedString, frame, linkLayerConf.frameSize, linkLayerConf.maxInformationSize);
    //printf("\nReceived:\n%X %X %X %X %X\n", frame[FHEADERFLAG], frame[FADDRESS], frame[FCONTROL], frame[FBCC1], frame[linkLayerConf.frameTrailerIndex]);
    writeFrameToLog(frame, RECEIVED);
    framesReceived++;
    return curchar;
}

int receiveCommand(unsigned char* command, int tryTimeout) {
    // install timeout handler
    struct sigaction sa;
    sa.sa_handler = timeout;
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);

    unsigned char* receivedFrame = malloc(linkLayerConf.frameSize);
    if(!receivedFrame) {
        printf("Failed to allocate memory for received frame, terminating\n");
        return -1;
    }

    alarm(tryTimeout);
    while (retryCounter < 1) {
        if(fromPhysical(receivedFrame, 1) != -1) {

            printf("\nReceived:\n%X %X %X %X %X\n", receivedFrame[FHEADERFLAG], receivedFrame[FADDRESS], receivedFrame[FCONTROL], receivedFrame[FBCC1], receivedFrame[linkLayerConf.frameTrailerIndex]);
            int errorCheckResult = checkForErrors(receivedFrame, linkLayerConf.maxInformationSize, applicationLayerConf.status);

            if(errorCheckResult == 0) {
                *command = receivedFrame[FCONTROL];
                free(receivedFrame);
                return 0;
            }
        }
    }

    free(receivedFrame);
    return -1;
}

int sendCommand(unsigned char command, unsigned char expectedResponse, int tryTimeout, int retries, unsigned char address) {

    // install timeout handler
    struct sigaction sa;
    sa.sa_handler = timeout;
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);


    unsigned char* frame = createSupervisionFrame(address, command, linkLayerConf.maxInformationSize);
    int responseResult;

    retryCounter = 0;

    while (retryCounter < retries) {

        if (retryCounter > 0) {
            char message[MESSAGE_LEN];
            sprintf(message, "Retry #%d, resending frame\n", retryCounter);
            writeToLog(message);
            timeouts++;
        }

        printf("Sending: %X %X %X %X %X\n", frame[FHEADERFLAG], frame[FADDRESS], frame[FCONTROL], frame[FBCC1], frame[linkLayerConf.frameTrailerIndex]);
        int res = toPhysical(frame);
        printf("%d bytes sent\n", res);

        alarm(tryTimeout);
        int currentTry = retryCounter;

        responseResult = receiveResponse(expectedResponse, currentTry);
        if(responseResult == 0)
            break;
    }

    if (responseResult == 0) {
        free(frame);
        return 0;
    }
    else {
        free(frame);
        return -1;
    }

}

int receiveResponse(unsigned char response, int currentTry) {
    int res = 0;
    unsigned char* receivedFrame = malloc(linkLayerConf.frameSize);
    if(!receivedFrame) {
        printf("Failed to allocate memory for received frame, terminating\n");
        return -1;
    }
    while (retryCounter == currentTry) {
        printf("Waiting for response\n");
        res = fromPhysical(receivedFrame, 1);
        if(res != -1) {
            int errorCheckResult = checkForErrors(receivedFrame, linkLayerConf.maxInformationSize, applicationLayerConf.status);
            if (errorCheckResult == 0) {
                if (receivedFrame[FCONTROL] == response) {
                    alarm(0);
                    return 0;
                } else {
                    printf("Didn't receive expected response!\n");
                }
            }
            else {
                printf("Error in received frame!\n");
            }
        }
    }

    return -1;
}

int sendResponse(unsigned char response, unsigned char address) {
    unsigned char* confirmationFrame = createSupervisionFrame(address, response, linkLayerConf.maxInformationSize);
    int res = toPhysical(confirmationFrame);
    printf("Sending %d bytes: %X %X %X %X %X\n", res, confirmationFrame[FHEADERFLAG], confirmationFrame[FADDRESS], confirmationFrame[FCONTROL], confirmationFrame[FBCC1], confirmationFrame[linkLayerConf.frameTrailerIndex]);
    free(confirmationFrame);
    return 0;
}

int receiveData(unsigned char* packet, size_t packetLength) {
    //TODO maybe accept receiving a DISC here?
    unsigned char* receivedFrame = malloc(linkLayerConf.frameSize);
    if(!receivedFrame) {
        printf("Failed to allocate memory for received frame, terminating\n");
        return -1;
    }

    alarm(RECEIVE_INFO_TIMEOUT);
    int receivedNewPacket = 0;
    int res;
    while(!receivedNewPacket && retryCounter < 1) {
        printf("Trying to receive new packet...\n");
        res = fromPhysical(receivedFrame, 1);
        if(res != -1) {
            int errorCheckResult = checkForErrors(receivedFrame, linkLayerConf.maxInformationSize, applicationLayerConf.status);

            if(errorCheckResult == 0) {
                printf("No errors found in this frame...\n");

                //If we received a DISC, end reception here
                if(receivedFrame[FCONTROL] == DISC) {
                    return -2;
                }

                //If we received the expected frame
                if(receivedFrame[FCONTROL] == linkLayerConf.sequenceNumber) {
                    printf("It's a new packet!\n");
                    linkLayerConf.sequenceNumber ^= INFO_1;
                    getInfo(receivedFrame, packet, packetLength);
                    receivedNewPacket = 1;
                }

                unsigned char rr;
                if(linkLayerConf.sequenceNumber == INFO_0)
                    rr = RR_0;
                else
                    rr = RR_1;
                unsigned char* rrFrame = createSupervisionFrame(RECEIVER_ADDRESS, rr, linkLayerConf.maxInformationSize);
                toPhysical(rrFrame);
                //free(rrFrame);
            }
            else if(errorCheckResult == FRAME_INFO_ERROR) {
                printf("Found info error in this frame...\nFrame BCC2: %X\n", receivedFrame[FBCC2(linkLayerConf.maxInformationSize)]);
                //If this was the expected frame, we want to reject it so that the sender can resend the frame earlier
                if(receivedFrame[FCONTROL] == linkLayerConf.sequenceNumber) {
                    unsigned char rej;
                    if(linkLayerConf.sequenceNumber == INFO_0)
                        rej = REJ_0;
                    else
                        rej = REJ_1;
                    unsigned char* rejFrame = createSupervisionFrame(RECEIVER_ADDRESS, rej, linkLayerConf.maxInformationSize);
                    toPhysical(rejFrame);
                    free(rejFrame);
                    rejNumber++;
                }
                //If this was a repeated frame, we want to send an rr so that the sender resends the frame earlier
                else {
                    unsigned char rr;
                    if(linkLayerConf.sequenceNumber == INFO_0)
                        rr = RR_0;
                    else
                        rr = RR_1;
                    unsigned char* rrFrame = createSupervisionFrame(RECEIVER_ADDRESS, rr, linkLayerConf.maxInformationSize);
                    toPhysical(rrFrame);
                    free(rrFrame);
                }
            }
            else
                printf("Frame messed up\n");
        }
        else
            printf("Timed out\n");

    }

    //free(receivedFrame);

    if(receivedNewPacket) {
        printf("Received: %s\n", packet);
        return res;
    }
    else
        return -1;
}

int sendData(unsigned char* packet, size_t packetLength) {
    if(packetLength > linkLayerConf.maxInformationSize) {
        printf("Packet is too big for defined information field size!\n");

    }
    unsigned char* frame = createInfoFrame(RECEIVER_ADDRESS, linkLayerConf.sequenceNumber, packet, packetLength, linkLayerConf.maxInformationSize);

    int STOP=FALSE;
    unsigned char* receivedFrame = malloc(linkLayerConf.frameSize);
    if(!receivedFrame) {
        printf("Failed to allocate memory for received frame, terminating\n");
        return -1;
    }
    int res;
    retryCounter = 0;

    int sendNextRR, resendRR, expectedREJ;
    if(linkLayerConf.sequenceNumber == INFO_0) {
        sendNextRR = RR_1;
        resendRR = RR_0;
        expectedREJ = REJ_0;
    }
    else {
        sendNextRR = RR_0;
        resendRR = RR_1;
        expectedREJ = REJ_1;
    }

    while (retryCounter < linkLayerConf.numTransmissions) {

        if (retryCounter > 0) {
            char message[MESSAGE_LEN];
            sprintf(message, "Retry #%d, resending frame\n", retryCounter);
            writeToLog(message);
            timeouts++;
        }
        
        // TODO turn this on and get the party started
        flipbit(&frame[FDATA], 2);
        
        res = toPhysical(frame);
        printf("%d bytes sent\n", res);

        alarm(linkLayerConf.sendTimeout);
        int receivedResend = 0;
        int currentTry = retryCounter;

        while (STOP == FALSE && retryCounter == currentTry && !receivedResend) {
            printf("Waiting for acknowledgement...\n");
            res = fromPhysical(receivedFrame, 1);
            if(res != -1) {
                int errorCheckResult = checkForErrors(receivedFrame, linkLayerConf.maxInformationSize, applicationLayerConf.status);
                if (errorCheckResult == 0){
                    if (receivedFrame[FCONTROL] == sendNextRR) {
                        alarm(0);
                        printf("Received positive acknowledgement, send next frame\n");
                        linkLayerConf.sequenceNumber ^= INFO_1;
                        STOP = TRUE;
                    } else if (receivedFrame[FCONTROL] == resendRR || receivedFrame[FCONTROL] == expectedREJ) { 
                        printf("Asked for resend!\n");
                        alarm(0);
                        retryCounter = 0;
                        receivedResend = 1;
                        framesResent++;
                        if(receivedFrame[FCONTROL] == expectedREJ)
                            rejNumber++;
                    }
                }
                else {
                    printf("Error in received frame!\n");
                }
            }
        }
        if(STOP == TRUE)
            break;
    }

    if (STOP == TRUE) {
        printf("Sent frame and received acknowledgement\n");
        return res;
        free(frame);
        free(receivedFrame);
        return 0;
    }
    else {
        printf("Failed to receive acknowledgement\n");
        free(frame);
        free(receivedFrame);
        return -1;
    }

}

void stuffFrame(unsigned char* destuffedFrame, unsigned char* stuffedFrame, size_t frameSize, size_t maxInformationSize) {
    unsigned int currentDestuffedByte = 1;
    unsigned int currentStuffedByte = 1;

    printf("Destuffed frame: %X %X %X %X %X %X\n", destuffedFrame[FHEADERFLAG], destuffedFrame[FADDRESS], destuffedFrame[FCONTROL], (unsigned char) destuffedFrame[FBCC1], (unsigned char) destuffedFrame[FBCC2(maxInformationSize)], destuffedFrame[linkLayerConf.frameTrailerIndex]);

    stuffedFrame[0] = FRAMEFLAG;
    stuffedFrame[frameSize - 1] = FRAMEFLAG;

    for(; currentStuffedByte < frameSize - 2; currentDestuffedByte++, currentStuffedByte++) {
        if(destuffedFrame[currentDestuffedByte] == FRAMEFLAG) {
            stuffedFrame[currentStuffedByte] = ESCAPE_BYTE;
            currentStuffedByte++;
            stuffedFrame[currentStuffedByte] = ESCAPED_FLAG;
        }
        else if(destuffedFrame[currentDestuffedByte] == ESCAPE_BYTE) {
            stuffedFrame[currentStuffedByte] = ESCAPE_BYTE;
            currentStuffedByte++;
            stuffedFrame[currentStuffedByte] = ESCAPED_ESCAPE;
        }
        else {
            stuffedFrame[currentStuffedByte] = destuffedFrame[currentDestuffedByte];
        }
    }

    unsigned long bcc2Position = FBCC2(maxInformationSize);
    if(destuffedFrame[bcc2Position] == FRAMEFLAG) {
        stuffedFrame[bcc2Position - 1] = ESCAPE_BYTE;
        stuffedFrame[bcc2Position] = ESCAPED_FLAG;
    }
    else if(destuffedFrame[bcc2Position] == ESCAPE_BYTE) {
        stuffedFrame[bcc2Position - 1] = ESCAPE_BYTE;
        stuffedFrame[bcc2Position] = ESCAPED_ESCAPE;
    }
    else {
        stuffedFrame[bcc2Position] = destuffedFrame[bcc2Position];
    }

    printf("Stuffed frame: %X %X %X %X %X %X\n", stuffedFrame[FHEADERFLAG], stuffedFrame[FADDRESS], stuffedFrame[FCONTROL], (unsigned char) stuffedFrame[FBCC1], (unsigned char) stuffedFrame[FBCC2(maxInformationSize)], stuffedFrame[linkLayerConf.frameTrailerIndex]);
}

void destuffFrame(unsigned char* stuffedFrame, unsigned char* destuffedFrame, size_t frameSize, size_t maxInformationSize) {
    unsigned int currentDestuffedByte = 1;
    unsigned int currentStuffedByte = 1;

    printf("Stuffed frame: %X %X %X %X %X %X\n", stuffedFrame[FHEADERFLAG], stuffedFrame[FADDRESS], stuffedFrame[FCONTROL], (unsigned char) stuffedFrame[FBCC1], (unsigned char) stuffedFrame[FBCC2(linkLayerConf.maxInformationSize)], stuffedFrame[linkLayerConf.frameTrailerIndex]);

    destuffedFrame[0] = FRAMEFLAG;
    destuffedFrame[frameSize - 1] = FRAMEFLAG;

    for(; currentStuffedByte < frameSize - 3; currentStuffedByte++, currentDestuffedByte++) {
        if(stuffedFrame[currentStuffedByte] == ESCAPE_BYTE) {
            currentStuffedByte++;
            destuffedFrame[currentDestuffedByte] = stuffedFrame[currentStuffedByte] ^ XOR_BYTE;
        }
        else
            destuffedFrame[currentDestuffedByte] = stuffedFrame[currentStuffedByte];
    }

    unsigned long bcc2Position = FBCC2(maxInformationSize);
    if(stuffedFrame[bcc2Position - 1] == ESCAPE_BYTE) {
        destuffedFrame[bcc2Position] = stuffedFrame[bcc2Position] ^ XOR_BYTE;
    }
    else {
        destuffedFrame[bcc2Position] = stuffedFrame[bcc2Position];
    }

    printf("Destuffed frame: %X %X %X %X %X %X\n", destuffedFrame[FHEADERFLAG], destuffedFrame[FADDRESS], destuffedFrame[FCONTROL], (unsigned char) destuffedFrame[FBCC1], (unsigned char) destuffedFrame[FBCC2(linkLayerConf.maxInformationSize)], destuffedFrame[linkLayerConf.frameTrailerIndex]);
}

void initializeLog(char * logname){
    char filename[LOGNAME_MAX_LEN];
    strcpy(filename, logname);
    strcat(filename, LOG_FILE);
    strcpy(linkLayerConf.logname, filename);
    int file = open(linkLayerConf.logname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (file == -1) {
  		perror(linkLayerConf.logname);
    }
    
    char buffer[LOGNAME_MAX_LEN];
    sprintf(buffer, "%s", "Log start\n");
    
    int len = (int)strlen(buffer);
    
    if ( write(file, buffer, len ) == -1){
        printf("Error writing to %s: %s\n", linkLayerConf.logname, strerror(errno));
    }
    
    close(file);
}

void writeToLog(char * string){
    int file = open(linkLayerConf.logname, O_WRONLY | O_APPEND, 0600);
    if (file == -1) {
  		perror(linkLayerConf.logname);
    }

    char message[MESSAGE_LEN];

    strcpy(message, string);
    
    unsigned long len = strlen(message);
    if ( write(file, message, len) == -1){
        printf("Error writing to %s: %s\n", linkLayerConf.logname, strerror(errno));
    }
    
    close(file);
}

// utilities for frame printing
char controlSymbols[] = {SET, DISC, UA, RR_0, RR_1, REJ_0, REJ_1, INFO_0, INFO_1};
char *controlSymbolStrings[] = {"SET   ", "DISC  ", "UA    ", "RR_0  ", "RR_1  ", "REJ_0 ", "REJ_1 ", "INFO_0", "INFO_1"};
int controlSymbolsSize = 9;

void writeFrameToLog(unsigned char * frame, int direction) {
    int file = open(linkLayerConf.logname, O_WRONLY | O_APPEND, 0600);
    if (file == -1) {
  		perror(linkLayerConf.logname);
    }

    char message[MESSAGE_LEN] = "-------------------------------------------------------------\n";
    char counterInfo[100];
    sprintf(counterInfo, "Frames sent: %u, of which %u were re-sends.\nREJ Frames: %u\nFrames received: %u\nTimed out %u times.\n", framesSent, framesResent, rejNumber, framesReceived, timeouts);
    strcat(message, counterInfo);
    
    if (direction == SENT)
        strcat(message, "Sent: ");
    else
        strcat(message, "Received: ");
    
    int i;
    for (i = 0; i < controlSymbolsSize; i++) {
        if ( frame[FCONTROL] == controlSymbols[i] )
            strcat(message, controlSymbolStrings[i]);
    }
    
    char bccInfo[20];
    sprintf(bccInfo, " BCC1: %-2X", frame[FBCC1]);
    strcat(message, bccInfo);
    
    sprintf(bccInfo, " BCC2: %-2X", frame[FBCC2(linkLayerConf.maxInformationSize)]);
    strcat(message, bccInfo);
    
    strcat(message, "\n");

    unsigned long len = strlen(message);
    if ( write(file, message, len) == -1){
        printf("Error writing to %s: %s\n", linkLayerConf.logname, strerror(errno));
    }
    
    close(file);
}