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


//This file contains receiving procedures for PairPhone:
//We records some 48KHz audio samples from Line, collects in the input buffer,
//synchronously demodulates while receives full 81 bits block,
//check for data type is voice or control, decrypt/decode or put silence,
//resampled depends actual buffer state and play 8KHz audio over Speaker.

//Estimated overall voice latency

//On the transmission side:
//delay of recording Mike buffer is 22.5mS
//delay of MELPE coder frame is 67.5 mS
//delay of Line playing buffer is 45 mS

//On the transport: channel latency of GSM depends external conditions

//On the receiving side:
//delay of Line receiving buffer is 22.5 mS
//delay of MELPE decoder frame is 67.5 mS
//delay of Speaker playing buffer is 45 mS

//Total latency average 270 mS + GSM latency (typically 180 mS)



#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#else

#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#endif


#include "audio/audio.h"  //low-level audio input/output
#include "modem/modem.h"  //modem
#include "melpe/melpe.h"  //audio codec
#include "crp.h"          //data processing
#include "rx.h"           //this
#include "gsm.h" // Include the GSM library
#include "time_utils.h"
#include "wgsm.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>


void decode_gsm_data(unsigned char *udpPacket, int packetSize);


int hasReceiveError = 1;
int hasReceiveUDP = 1;

// Global variable for the UDP socket
int udp_sock = -1;

// Function to initialize the UDP socket
int init_udp_socket(int port) {
    struct sockaddr_in server_addr;

    // Create socket
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("socket creation failed");
        return -1;
    }

    // Bind the socket with the server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(udp_sock, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        return -1;
    }


    // Assume sockfd is your socket descriptor
    int flags = fcntl(udp_sock, F_GETFL, 0);
    if (flags == -1) {
        // handle error
    }

    flags |= O_NONBLOCK;
    if (fcntl(udp_sock, F_SETFL, flags) == -1) {
        // handle error
    }

    return 0;
}




//Global variables

//baseband processing
static short speech[360 * 6];    //PCM 48KHz input buffer for samples received from the line
static short *samples = speech;  //pointer to samples ready for processing
static int cnt = 0;              //the number of unprocessed samples
static unsigned char buf[12];  //demodulators data output
short sp[544]; //ouputted speech frame

//resampling
float qff = 1.0;  //resampling ratio
static float up_pos = 1.0; //resampler fractional position
static short left_sample = 0; //base sample

//playing
static short jit_buf[800]; //PCM 8KHz buffer for samples ready for playing over Speaker
//static short* p_jit_buf=jit_buf; //pointer to unplayed samples in the buffer
static short l_jit_buf = 0; //number of unplayed samples in the buffer
static int q_jit_buf = 0; //pointer to unplayed samples in the buffer
static float fdelay = 7200; //averages playing delay
//accumulators
static float fber = 0; //Average bit error rate
static float fau = 0;  //Average authentication level

//internal procedures 
static int resample(short *src, short *dest, float fstep); //resumpling before playing
static int playjit(void); //playing buffered samples



//*****************************************************************************
//----------------Streaming resampler--------------------------------------------
static int resample(short *src, short *dest, float fstep) {
    //resampled MELPE frame (540 short 8KHz samples) to specified rate
    //input: pointer to source and resulting short samples, resulting ratio
    //output: samples in dest resumpled from 8KHz to specified sample rate
    //returns: resulting length in samples

    int i, diff = 0;
    short *sptr = src; //source
    short *dptr = dest; //destination

    //process 540 samples
    for (i = 0; i < 540; i++) //process MELPE frame
    {
        diff = *sptr - left_sample; //computes difference between current and basic samples
        while (up_pos <= 1.0) //while position not crosses a boundary
        {
            *dptr++ = left_sample + ((float) diff * up_pos); //set destination by basic, difference and position
            up_pos += fstep; //move position forward to fractional step
        }
        left_sample = *sptr++; //set current sample as a  basic
        up_pos = up_pos - 1.0; //move position back to one outputted sample
    }
    return dptr - dest;  //number of outputted samples
}

//*****************************************************************************
//*****************************************************************************
//--Playing over Speaker----------------------------------
static int playjit(void) {
    //play 8KHz samples in buffer over Speaker
    int i = 0;
    int job = 0;

    if (l_jit_buf > 0) //we have unplayed samples, try to play
    {
        i = soundplay(l_jit_buf, (unsigned char *) (jit_buf + q_jit_buf)); //play, returns number of played samples
        if (i) job += 2; //set job
        if ((i < 0) || (i > l_jit_buf)) i = 0; //must play again if underrun (PTT mode etc.)
        l_jit_buf -= i; //decrease number of unplayed samples
        if (l_jit_buf < 0) l_jit_buf = 0;
        q_jit_buf += i; //move pointer to unplayed samples
        if ((l_jit_buf < 180) && (q_jit_buf > 0)) //all samples played
        {
            //memcpy((char*)jit_buf, (char*)(jit_buf+q_jit_buf), 2*l_jit_buf);
            for (i = 0; i < l_jit_buf; i++) jit_buf[i] = jit_buf[i + q_jit_buf]; //move tail
            q_jit_buf = 0; //move pointer to the start of empty buffer
        }
    }
    return job; //job flag
}

//*****************************************************************************
//receiving loop: grab 48KHz baseband samples from Line,
//demodulate, decrypt, decode, play 8KHz voice over Speaker
int rx(int typing) {
    // Check if the UDP socket is initialized
    if (udp_sock < 0) {
        if (init_udp_socket(12345) < 0) {  // Replace 12345 with your actual port number
            printf("Failed to initialize UDP socket\n");
            return -1;
        }
    }

    // Receive UDP packet
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    unsigned char udpPacket[33 * 21]; //GSM frame is 33, but looks like some size at 40, 200 should be enough



    //input: -1 for no typing chars, 1 - exist some chars in input buffer
    //output: 0 - no any jobs doing, 1 - some jobs were doing
    int i;
    float f;
    int job = 0; //flag of any job were doing
    char lag_flag = 0; //block lag is locked (modems synchronization complete)
    //char lock_flag=0; //phase of carrier (1333Hz, 6 samples per period) is locked
    //char sync_flag=0; //the difference of frequency transmitter-to-receiver sampling rate is locked
    //char current_lag=0;  //block lag (0-90, aligned to last bit position, the 6 samples for bit)

    char info[8] = {0}; //call info

    //regularly play Speaker's buffer
    job = playjit(); //the first try to play a tail of samples in buffer
    //check for we have enough samples for demodulation
    if (cnt < 180 * 6) //check we haven't enough of unprocessed samples
    {
        //move tail to start of receiving buffer
        if (samples > speech) //check for tail
        {
            for (i = 0; i < cnt; i++) speech[i] = samples[i]; //move tail to start of buffer
            samples = speech; //set pointer to start of buffer
        }

        // TODO jack, add 22.5ms delay
        usleep(22.5 * 1000); // delay for 45ms
        //record
//        i = _soundgrab((char *) (samples + cnt), 180 * 6);  //try to grab new 48KHZ samples from Line

//        i = recvfrom(udp_sock, (char *)udpPacket, sizeof(udpPacket), MSG_WAITALL, (struct sockaddr *)&client_addr, &addr_len);
        i = recvSamplesFromNetwork(samples + cnt, udp_sock, client_addr);
//        int i = recvfrom(udp_sock, (char *)udpPacket, sizeof(udpPacket), MSG_WAITALL, (struct sockaddr *)&client_addr, &addr_len);
        if (i < 0) {
            // handle error
        } else if (i > 0) {

//            DecodeFrames(global_gsm_state_4decode, (gsm_frame *)udpPacket, dst);
//            printf("%s zzzzzzzzzzzzzzzzzzzzz : %d\r\n", getCurrentDateTimeWithMillis(), i);
        }

        if (i < 0) {
            hasReceiveError++;
            if (hasReceiveError % 100001 == 0) {
                // TODO jack: bring back
//                printf("%s 999999999 recvfrom nothing in non-blocking as -1 in every 100001th iteration as %d\r\n", getCurrentDateTimeWithMillis(), hasReceiveError);
            }
            // good with not recv anything
        }

        if ( i != -1) {
//            printf("%s 5555555555 check received UDP packet size: %d\r\n", getCurrentDateTimeWithMillis(), i);
        }
        if ((i > 0) && (i <= (180 * 6))) //some samples grabbed
        {
            if (hasReceiveUDP % 101 == 0) {
//                printf("%s ^^^^^^^^^^ check received UDP packet size: %d\r\n", getCurrentDateTimeWithMillis(), i);
            }
            hasReceiveUDP++;
            cnt += i;  //add grabbed  samples to account
            job += 4;  //set job
        }
    } else //we have enough samples for processing
    {
        i = Demodulate(samples, buf); //process samples: 36*6 (35-37)*6 samples
//        printf("%s 6666666666 demodulate samples: %d\r\n", getCurrentDateTimeWithMillis(), i);
        samples += i; //move pointer to next samples (with frequency adjusting)
        cnt -= i; //decrease the number of unprocessed samples
        if (0x80 & buf[11]) //checks flag for output data block is ready
        {
            //check for synck and averages BER
            lag_flag = !(!(buf[11] & 0x40)); //block lag is locked (synchronization compleet)
            //lock_flag=!(!(buf[11]&0x20)); //phaze of carrier (1333Hz, 6 samples per period) is locked
            //sync_flag=!(!(buf[11]&0x10)); //the differency of frequency transmitter-to-receiver sampling rate is locked
            //current_lag=buf[10]>>1;  //block lag (0-90, aligned to last bit position, the 6 samples for bit)
            if (lag_flag) //check modem sync
            {
                //averages BER
                i = (0x0F & buf[11]); //count symbols errors (only 1 error per 9-bit symbol can be detected)
                fber *= 0.99; //fber in range 0-900
                fber += i;  //in range 0-9 errored bits per 90 bits treceived
            }
            //output statistics
            if (typing < 0) //output call's info if no characters were typed by user
            {
                f = Mute(0);   //get packets counter value
                i = State(0); //get current connection step * vad flag

                //notification of state and voice output
                if (!i) strcpy(info, (char *) "IDLE");
                else if (abs(i) < 8) strcpy(info, (char *) "CALL");
                else if (f <= 0) strcpy(info, (char *) "MUTE");
                else if (i < 0) strcpy(info, (char *) "PAUS");
                else
                    strcpy(info, (char *) "TALK");

                if (f < 0) f = -f; //absolute value
                i = f * 0.0675; //computes total time of the call in sec: each packet 67,5 ms

                f = fau / 4 - 100; //computes authentification level in %
                if (f < 0) f = 0; //only positive results have reason
                //current state notification
                if (lag_flag) printf("%s %dmin %dsec BER:%0.02f AU:%d%%\r", info, i / 60, i % 60, fber / 90, (int) f);
                else printf("%s %dmin %dsec BER:---- AU:%d%%\r", info, i / 60, i % 60, (int) f); //lost of sync in modem
            }

            //process received packet detects voice/silence type
            buf[11] = 0xFE; //set flag default as for silence descriptor
            if (lag_flag) //check modem sync
            {
                i = ProcessPkt(buf);  //decode received packet
                if (i >= 0) //received packet is a control type
                {
                    fau *= 0.99;  //fau in range 0-800 (400 for random data)
                    fau += i; //averages authentication level
                } else if (i == -3) {
                    buf[11] = 0xFF; //set flag for voice data received
                }
            } //end of sync ok, packets processing
        } //end of data block received
    } //end of a portion of sampless processing

    //check we have received data and output buffer is empty for decoding
    if ((0x0E & buf[11]) && (l_jit_buf <= 180)) {
        //decode voice data or set silency
        job += 16; //set job
        if (1 & buf[11]) //this is a voice frame, decode it
        {
            melpe_s(sp, buf); //decode 81 bits in 11 bytes to 540 8KHz samples
        } else
            memset(sp, 0, 1080); //or output 67.5 mS of silence
        buf[11] = 0; //clears flag: data buffer is processed

        //computes average playing delay
        i = getdelay() + l_jit_buf; //total number of unplayed samples in buffers
        fdelay *= 0.9; //averages
        fdelay += i;
        //computes optimal resapling ratio for the optimum delay
        f = fabs(fdelay / 10 - 720) / 10000000; //correction rate due inconsistency
        if (i < 360) qff -= f;  //adjust current ratio
        else if (i > 1080) qff += f;
        if (qff < 0.888) qff = 0.888; //restrictions
        else if (qff > 1.142) qff = 1.142;

        //resample and play to Headset
        if (l_jit_buf > 180) l_jit_buf = 0; //prevent overflow
        l_jit_buf += resample(sp, jit_buf + l_jit_buf, qff); //resample buffer for playing
        playjit(); //immediately try to play buffer
    }
    return job;
}

//----------------------Setup----------------------------------
//*****************************************************************************
//initialize audio devices
int audio_init(void) {
    if (!_soundinit()) //init audio8 play/rec Headset and audio48 play/rec Line
    {
        printf("Error of 'Line' audio device initialization, application terminated\r\n");
        return 1;  //init Headset side 8KHz audio device
    }

    if (!soundinit()) {
        printf("Error of 'Headset' audio device initialization, application terminated\r\n");
        return 2; //init Line side 48KHz audio device
    }
    printf("\r\n");

    _soundrec(1); //start records from Line
    soundrec(1); //start recrds from Mike

    melpe_i(); //init MELPE1200 codec engine
    return 0; //success
}

//*****************************************************************************
//finalize audio devices
void audio_fin(void) {
    _soundterm();
    soundterm();
}


// Global variable to hold the GSM state object

gsm global_gsm_state_4decode = NULL; // Global gsm object


int DecodeFrames(gsm g, gsm_frame frames[21], gsm_signal dst[21][kSamples]) {


    int i;
    for (i = 0; i < 20; ++i) { // Decode the first 20 frames
        if (!DecodeFrame(g, frames[i], dst[i])) {
            return 0; // false in C
        }


        // Add the decoded data to the samples buffer
        for (int j = 0; j < kSamples; j++) {
            samples[cnt + j] = (short) dst[j];
        }
        cnt += kSamples;
    }

    // Handle the fractional frame
    gsm_signal fractional_dst[(int)(0.25 * kSamples)];
    if (!DecodeFrame(g, frames[20], fractional_dst)) {
        return 0; // false in C
    }

    // Add the fractional decoded data to the samples buffer
    for (int j = 0; j < (int)(0.25 * kSamples); j++) {
        samples[cnt + j] = (short) fractional_dst[j];
    }
    cnt += (int)(0.25 * kSamples);

    return 1; // true in C
}

void byteArrayToShortArray(const uint8_t *byteArray, size_t byteArrayLength, short *shortArray) {
    for (size_t i = 0; i < byteArrayLength / 2; ++i) {
        shortArray[i] = (short)((byteArray[2 * i + 1] << 8) | byteArray[2 * i]);
    }
}


int recvSamplesFromNetwork(short pcmSampleArrayInt[3240/3], int sock, struct sockaddr_in server_addr) {
    // Step 1: Calculate the length of pcmSampleArrayInt
    size_t shortArrayLength = 3240/3;

    // Step 2: Create a byte array
    uint8_t byteArray[shortArrayLength * 2];

    // Calculate the size of each chunk
    size_t chunkSize = shortArrayLength * 2 / 6;

    // Set the socket to non-blocking mode
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    // Loop to receive the data 6 times
    for (int j = 0; j < 6; j++) {
        // Calculate the start and end indices of the chunk
        size_t start = j * chunkSize;
        size_t end = start + chunkSize;

        // Make sure the end index does not exceed the array length
        if (end > shortArrayLength * 2) {
            end = shortArrayLength * 2;
        }

        // Receive the chunk
        socklen_t addr_len = sizeof(server_addr);
        ssize_t recvLen = recvfrom(sock, (char *)(byteArray + start), (end - start),
                                   MSG_WAITALL, (struct sockaddr *) &server_addr,
                                   &addr_len);
        if (recvLen < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // The operation would block, so skip this iteration
                continue;
            } else {
                perror("Receive failed");
                return -1;
            }
        }


//        unsigned int hash = crc32b((unsigned char *)(byteArray + start), (end - start));
//        printf("Hash: %08x\r\n", hash); // Print hash in hexadecimal format
    }

    // Step 3: Call the byteArrayToShortArray function
    byteArrayToShortArray(byteArray, shortArrayLength * 2, pcmSampleArrayInt);

    return shortArrayLength;
}