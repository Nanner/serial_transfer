/*Non-Canonical Input Processing*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "supervisionFrame.h"

#define BAUDRATE B9600
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1

volatile int STOP=FALSE;

int main(int argc, char** argv)
{
    int fd, res;
    struct termios oldtio,newtio;
    char buf[255];
    
    if ( (argc < 2) ||
        ((strcmp("/dev/ttyS0", argv[1])!=0) &&
         (strcmp("/dev/ttyS1", argv[1])!=0) )) {
            printf("Usage:\tnserial SerialPort\n\tex: nserial /dev/ttyS1\n");
            exit(1);
        }
    
    
    /*
     Open serial port device for reading and writing and not as controlling tty
     because we don't want to get killed if linenoise sends CTRL-C.
     */
    
    
    fd = open(argv[1], O_RDWR | O_NOCTTY );
    if (fd <0) {perror(argv[1]); exit(-1); }
    
    if ( tcgetattr(fd,&oldtio) == -1) { /* save current port settings */
        perror("tcgetattr");
        exit(-1);
    }
    
    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    
    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;
    
    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 1;   /* blocking read until 1 chars received */
    
    
    
    /*
     VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a
     leitura do(s) pr�ximo(s) caracter(es)
     */
    
    
    
    tcflush(fd, TCIOFLUSH);
    
    if ( tcsetattr(fd,TCSANOW,&newtio) == -1) {
        perror("tcsetattr");
        exit(-1);
    }
    
    printf("New termios structure set\n");
    
    
    int curchar = 0;
    char finalstring[255];
	
    while (STOP==FALSE) {       /* loop for input */
        res = read(fd,buf,1);
        if(res == 1){
            finalstring[curchar] = buf[0];
            curchar++;
        }
        if (finalstring[curchar-1]==FRAMEFLAG && curchar-1 > 0) STOP=TRUE;
    }
    
    SupervisionFrame frame;
    memcpy(&frame, finalstring, sizeof(SupervisionFrame));
    printf("%x %x %x %x %x\n", frame.frameHeader, frame.address, frame.control, frame.bcc, frame.frameTrailer);
    
    SupervisionFrame confirmationFrame = createFrame(SENDER_ADDRESS, UA);
    
    res=write(fd, &confirmationFrame, sizeof(SupervisionFrame));
    printf("\nsent\n");
    
    /*
     O ciclo WHILE deve ser alterado de modo a respeitar o indicado no gui�o
     */
    
    
    
    tcsetattr(fd,TCSANOW,&oldtio);
    close(fd);
    return 0;
}