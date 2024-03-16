///////////////////////////////////////////////
//
// **************************
//
// Project/Software name: X-Phone
// Author: "Van Gegel" <gegelcopy@ukr.net>
//
// THIS IS A FREE SOFTWARE  AND FOR TEST ONLY!!!
// Please do not use it in the case of life and death
// This software is released under GNU LGPL:
//
// * LGPL 3.0 <http://www.gnu.org/licenses/lgpl.html>
//
// You’re free to copy, distribute and make commercial use
// of this software under the following conditions:
//
// * You have to cite the author (and copyright owner): Van Gegel
// * You have to provide a link to the author’s Homepage: <http://torfone.org>
//
///////////////////////////////////////////////

//This is a main procedure of PairPhone testing software
//one-thread implementation as a infinite loop contained procedures for:
//-receiving baseband, demodulating, decrypting, decompressing, playing over earphones (RX)
//-recording voice from mike, compressing, encrypting, modulating, sending baseband into line (TX)
//-scan keyboard, processing. Suspending thread if no job (CTR)
//---------------------------------------------------------------------------
#ifdef _WIN32
#else //linux

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "memory.h"
#include "math.h"

#include <sys/time.h>

#endif

#include "audio/audio.h"  //low-level alsa/wave audio 
#include "crypto/libcrp.h" //cryptography primitives 

#include "crp.h" //key agreement, authentication, encryption/decryption, frame synchronization
#include "ctr.h" //scan keyboard, processing. Suspending thread if no job (CTR)
#include "rx.h"  //receiving baseband, demodulating, decrypting, decompressing, playing over earphones
#include "tx.h"     //recording voice from mike, compressing, encrypting, modulating, sending baseband into line


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_PORT 12345 // The port number of the server
#define SERVER_IP "192.168.2.3" // The IP address of the server

int main(int argc, char *argv[]) {
    int sock;
    struct sockaddr_in server_addr;

    // Create and configure the socket for UDP
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    int i = 0;

    printf("---------------------------------------------------------------\r\n");
    printf("   PairPhone v0.1a  Van Gegel, 2016  MailTo: torfone@ukr.net\r\n");
    printf("     P2P private talk over GSM-FR compressed voice channel\r\n");
    printf("---------------------------------------------------------------\r\n");

    randInit(0, 0); //init SPRNG
    if (audio_init()) return -1;  //init audio
    tty_rawmode(); //init console IO
    HangUp(); //set idle mode

    int iii = 0;
    //main loop
    do {
        i = rx(i);   //receiving
        i = tx(i, sock, server_addr);   //transmitting

        // TODO jack: This is a debug print statement. It should be removed.
        if (iii % 100000 == 0) {
//            if (i != -1) printf("\n\ni is: %d     Count: %d\n\n", i, iii);
        }
        iii++;

        i = ctr(1);  //controlling
    } while (i);

    // Close the socket
    close(sock);

    tty_normode(); //restore console
    audio_fin(); //close audio
    return 0;
}
//---------------------------------------------------------------------------
