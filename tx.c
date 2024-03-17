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

//This file contains transmitting procedures for PairPhone:
//We records some 8KHz audio samples (voice) from Mike, resample and collects in the buffer,
//upon sufficient melpe frame (540 samples), check frame for voice/silency,
//melpe encode or compose control silency descriptor, modulate to 3240 48KHz samples
//and play the baseband signal into Line output.




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
#include "vad/vad2.h"   //voice active detector

#include "crp.h"          //data processing
#include "tx.h"           //this

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "gsm.h"

#include <time.h>
#include <sys/time.h>

const char* getCurrentDateTimeWithMillis() {
    static char datetime[80]; // Increased buffer size for milliseconds
    struct timeval tv;
    struct tm* tm_now;

    // Get current time with microseconds precision
    gettimeofday(&tv, NULL);
    tm_now = localtime(&tv.tv_sec); // Convert seconds part to tm struct

    // Format the date and time, include milliseconds
    int millis = tv.tv_usec / 1000; // Convert microseconds to milliseconds
    strftime(datetime, sizeof(datetime) - 10, "%Y-%m-%d %H:%M:%S", tm_now); // Save space for milliseconds
    sprintf(datetime + strlen(datetime), ".%03d", millis); // Append milliseconds

    return datetime;
}



int hasWrittenSamplesToFile = 0; // Flag to check if samples have been written to file
int hasGSMEncodedSent = 1; // Flag to check if GSM encoded data has been sent
int chech_l__jit_buf_count = 1;
int chech_l__jit_buf_count2 = 1;


vadState2 vad; //Voice Active Detector state

//recording 8 KHz speach from Mike
static short spraw[180]; //buffer for raw grabbed 8 KHz speach samples
static short spbuf[748]; //buffer for accumulate resampled voice up to melpe frame
static int spcnt = 0; //number of accumulated samples
static unsigned char txbuf[12]; //buffer for encoded melpe frame or silency descryptor

//resampling
static float _up_pos = 1.0; //resamplers values
static short _left_sample = 0;


//playing 48 KHz baseband signal into Line
static short _jit_buf[3240]; //PCM 48KHz buffer for samples ready for playing into Line
static short *p__jit_buf = _jit_buf; //pointer to unplayed samples in the buffer
static short l__jit_buf = 0; //number of unplayed samples in the buffer

//synchronizing grabbing and playing loops
static float _fdelay = 24000; //average recording delay
static int tgrab = 0; //difference between pointers of recording and playing streams


int sendSamplesToNetwork(short pInt[3240], short bufUsedSizeShort, int sock, struct sockaddr_in server_addr);

//*****************************************************************************
//----------------Streaming resampler--------------------------------------------
static int _resample(short *src, short *dest, int srclen, int rate) {
    //resampled srclen 8KHz samples to specified rate
    //input: pointers to source and resulting short samples, number of source samples, resulting rate
    //output: samples in dest resumpled from 8KHz to specified samplerate
    //returns: number of resulting samples in dest

    int i, diff = 0;
    short *sptr = src; //source
    short *dptr = dest; //destination
    float fstep = 8000.0 / rate; //ratio between specified and default rates

    //process 540 samples
    for (i = 0; i < srclen; i++) //process samples
    {
        diff = *sptr - _left_sample; //computes difference beetwen current and basic samples
        while (_up_pos <= 1.0) //while position not crosses a boundary
        {
            *dptr++ = _left_sample + ((float) diff * _up_pos); //set destination by basic, difference and position
            _up_pos += fstep; //move position forward to fractional step
        }
        _left_sample = *sptr++; //set current sample as a  basic
        _up_pos = _up_pos - 1.0; //move position back to one outputted sample
    }
    return dptr - dest;  //number of outputted samples
}


//*****************************************************************************
//--Playing over Speaker----------------------------------
static int _playjit(int sock, struct sockaddr_in server_addr) {
    if (chech_l__jit_buf_count % 100001 == 0) {
        printf("%s oooooooooo in every 100001 l__jit_buf: %d   counter: %d\r\n", getCurrentDateTimeWithMillis(), l__jit_buf, chech_l__jit_buf_count);
    }
    chech_l__jit_buf_count++;
    //periodically try to play 8KHz samples in buffer over Speaker
    int i = 0;
    int job = 0;

    if (l__jit_buf) //we have unplayed samples, try to play
    {
        if (chech_l__jit_buf_count2 % 100001 == 0) {
            printf("1111111111 in every 100001 l__jit_buf: %d\r\n", l__jit_buf);
        }
        chech_l__jit_buf_count2++;
        i = _soundplay(l__jit_buf, (unsigned char *) (p__jit_buf)); //play, returns number of played samples
        // instead of play the sound I want to send the samples to network
//        i = sendSamplesToNetwork(_jit_buf, l__jit_buf, sock, server_addr);
        if (i) job += 2; //set job
        if ((i < 0) || (i > l__jit_buf)) i = 0; //must play again if underrun (PTT mode etc.)
        l__jit_buf -= i; //decrease number of unplayed samples
        printf("%s 2222222222 check if l__jit_buf changed after _soundplay: %d\r\n", getCurrentDateTimeWithMillis(), l__jit_buf);
        p__jit_buf += i; //move pointer to unplayed samples
        if (l__jit_buf <= 0) //all samples played
        {
            l__jit_buf = 0; //correction
            p__jit_buf = _jit_buf; //move pointer to the start of empty buffer
        }
    }
    return job; //job flag
}

//*****************************************************************************
//transmition loop: grab 8KHz speech samples from Mike,
//resample, collect frame (540 in 67.5 mS), encode
//encrypt, modulate, play 48KHz baseband signal into Line
int tx(int job, int sock, struct sockaddr_in server_addr) {
    int i, j;

    //loop 1: try to play unplayed samples
    job += _playjit(sock, server_addr); //the first try to play a tail of samples in buffer


    //loop 2: try to grab next 180 samples
    //check for number of grabbed samples
    if (spcnt < 540) //we haven't enought samples for melpe encoder
    {
        i = soundgrab((char *) spraw, 180); //grab up to 180 samples
        if ((i > 0) && (i <= 180)) //if some samles was grabbed
        {
            //Since we are using different audio devices
            //on headset and line sides, the sampling rates of grabbing
            // and playing devices can slightly differ then 48/8 depends HW
            //so we must adjusts one of rates for synchronizing grabbing and playing processes
            //The line side is more sensitive (requirements for baseband is more hard)
            //That why we resamples grabbed stream (slave) for matching rate with playing stream as a master
            //The adjusting process doing approximation in iterative way
            //and requires several seconds for adaptation during possible loss of some speech 67.5mS frames

            //computes estimated rate depends recording delay obtained in moment of last block was modulated
            j = 8000 - (_fdelay - 27000) / 50; //computes samplerate using optimal delay and adjusting sensitivity
            if (j > 9000) j = 9000; //restrict resulting samplerate
            if (j < 7000) j = 7000;

            //change rate of grabbed samples for synchronizing grabbing and playing loops
            i = _resample(spraw, spbuf + spcnt, i, j); //resample and collect speech samples
            spcnt += i; //the number of samples in buffer for processing
            tgrab += i; //the total difference between grabbed speech and played baseband samples
            //this is actually recording delay and must be near 270 sample in average
            //for jitter protecting (due PC multi threading etc.)

            job += 32; //set job
        }
    }
    //check for we have enough grabbed samples for processing
    if (spcnt >= 540) //we have enough samples for melpe encoder
    {
        if (Mute(0) > 0) {
            i = vad2(spbuf + 10, &vad);  //check frame is speech (by VAD)
            i += vad2(spbuf + 100, &vad);
            i += vad2(spbuf + 190, &vad);
            i += vad2(spbuf + 280, &vad);
            i += vad2(spbuf + 370, &vad);
            i += vad2(spbuf + 460, &vad);
        } else i = 0;

        txbuf[11] = 0xFF;   //set defaults flag for voiced frame
        if (i) //frame is voices: compress it
        {
            melpe_a(txbuf, spbuf); //encode the speech frame
            i = State(1); //set VAD flag
        } else //unvoiced frame: sync packet will be send
        {
            txbuf[11] = 0xFE; //or set silence flag for control blocks
            i = State(-1); //clears VAD flag
        }

        spcnt -= 540; //samples rest
        if (spcnt) memcpy((char *) spbuf, (char *) (spbuf + 540), 2 * spcnt);  //move tail to start of buffer
        job += 64;
    }

    //Loop 3: playing
//get number of unplayed samples in buffer 
    i = _getdelay();
//preventing of freezing audio output after underrun or overrun 
    if (i > 540 * 3 * 6) {
        _soundflush1();
        i = _getdelay();
    }
//check for delay is acceptable for playing next portion of samples 
    if (i < 720 * 6) {
        if (l__jit_buf) return job; //we have some unplayed samples in local buffer, not play now.
        MakePkt(txbuf); //encrypt voice or get actual control packet
        l__jit_buf = Modulate(txbuf, _jit_buf); //modulate block
        printf("%s 3333333333 check if l__jit_buf changed after modulate: %d\r\n", getCurrentDateTimeWithMillis(), l__jit_buf);

        // TODO jack, Add the modified file writing code with flag check here
        if (!hasWrittenSamplesToFile) {
            FILE *fp = fopen("modulated_samples.raw", "wb");
            if (fp != NULL) {
                fwrite(_jit_buf, sizeof(short), l__jit_buf, fp);
                fclose(fp);
                hasWrittenSamplesToFile = 1; // Set the flag to indicate samples have been written
            } else {
                fprintf(stderr, "Failed to open file for writing.\n");
            }
        }

        txbuf[11] = 0; //clear tx buffer (processed)
        _playjit(sock, server_addr);  //immediately play baseband into Line

        //estimate rate changing for grabbed samples for synchronizing grabbing and playing
        _fdelay *= 0.99; //smooth coefficient
        _fdelay += tgrab;   //averages recording delay
        tgrab -= 540;  //decrease counter of grabbed samples

        job += 128;
    }

    return job;
}

#define SERVER_PORT 12345 // The port number of the server
#define SERVER_IP "192.168.2.3" // The IP address of the server

//int sendSamplesToNetwork(short pInt[3240], short buf, int sock, struct sockaddr_in server_addr) {
//
//
//    // Send the samples to the network
//    // Send the byte array via UDP
//    if (sendto(sock, pInt, buf * sizeof(short), 0,
//               (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
//        perror("Send failed");
//        close(sock);
//        exit(EXIT_FAILURE);
//    }
//
//    printf("Sending %d samples to network\n", buf);
//
//    return buf;
//}

gsm g = NULL; // Global variable to hold the GSM state object

const int kSamples = 160;

int sendSamplesToNetwork(short pcmSampleArrayInt[3240], short bufUsedSizeShort, int sock, struct sockaddr_in server_addr) {
    printf("l__jit_buf: %d\n", bufUsedSizeShort);
    gsm_signal src[kSamples];
    gsm_frame frame;

    // Create gsm object if it doesn't exist
    if (!g) {
        g = gsm_create();
        if (!g) {
            printf("gsm_create failed\n");
            return 0; // false in C
        }
    }

    // Loop over pcmSampleArrayInt in chunks of kSamples
    for (int i = 0; i < bufUsedSizeShort; i += kSamples) {
        // Copy the data from pcmSampleArrayInt to src
        for (int j = 0; j < kSamples; j++) {
            src[j] = pcmSampleArrayInt[i + j];
        }

    }

    // Encode the data
    gsm_encode(g, src, frame);

    if (hasGSMEncodedSent % 100001 == 0) {
        // TODO jack: get back this print
//        printf("GSM Encoded data sent every 100001: %d\n", strlen(frame));
    }
    hasGSMEncodedSent++;
    // Send the encoded data
    if (sendto(sock, (const char *)frame, strlen(frame),
               MSG_CONFIRM, (const struct sockaddr *) &server_addr,
               sizeof(server_addr)) < 0) {
        perror("Send failed");
        // close(sock); don't close, the main func will close it
        exit(EXIT_FAILURE);
    }

    return bufUsedSizeShort;
}