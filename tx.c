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

#include "time_utils.h"
#include "wgsm.h"

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
//        printf("%s aaaaaaaaaa in every 100001 l__jit_buf: %d   counter: %d\r\n", getCurrentDateTimeWithMillis(), l__jit_buf, chech_l__jit_buf_count);
    }
    chech_l__jit_buf_count++;
    //periodically try to play 8KHz samples in buffer over Speaker
    int i = 0;
    int job = 0;

    if (l__jit_buf) //we have unplayed samples, try to play
    {
        // FIXME jack, put 45ms delay here, and comment out the _soundplay
//        i = _soundplay(l__jit_buf, (unsigned char *) (p__jit_buf)); //play, returns number of played samples
        usleep(45 * 1000); // delay for 45ms
        if (chech_l__jit_buf_count2 % 101 == 0) {
//            printf("1111111111 in every 101 l__jit_buf: %d and i: %d\r\n", l__jit_buf, i);
        }
        chech_l__jit_buf_count2++;

        // instead of play the sound I want to send the samples to network
        i = sendSamplesToNetwork(_jit_buf, l__jit_buf, sock, server_addr);
        if (i) job += 2; //set job
        if ((i < 0) || (i > l__jit_buf)) i = 0; //must play again if underrun (PTT mode etc.)
        l__jit_buf -= i; //decrease number of unplayed samples
//        printf("%s 2222222222 check if l__jit_buf changed after _soundplay: %d\r\n", getCurrentDateTimeWithMillis(), l__jit_buf);
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

//            printf("%s 7777777777 sample number %d and sample rate: %d\r\n", getCurrentDateTimeWithMillis(), i, j);
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
        // TODO jack, modulate encrypted txbuf to _jit_buf
        l__jit_buf = Modulate(txbuf, _jit_buf); //modulate block
//        printf("%s 3333333333 check if l__jit_buf changed after modulate: %d\r\n", getCurrentDateTimeWithMillis(), l__jit_buf);

        // TODO jack, Add the modified file writing code with flag check here
//        if (!hasWrittenSamplesToFile) {
//            FILE *fp = fopen("modulated_samples.raw", "wb");
//            if (fp != NULL) {
//                fwrite(_jit_buf, sizeof(short), l__jit_buf, fp);
//                fclose(fp);
//                hasWrittenSamplesToFile = 1; // Set the flag to indicate samples have been written
//            } else {
//                fprintf(stderr, "Failed to open file for writing.\n");
//            }
//        }

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



// ...
gsm global_gsm_state_4encode = NULL; // Global gsm object
const int kSamples = 160;
const int kFrameSize = 33;

void shortArrayToByteArray(const short *shortArray, size_t shortArrayLength, uint8_t *byteArray) {
    for (size_t i = 0; i < shortArrayLength; ++i) {
        byteArray[2 * i] = (uint8_t)(shortArray[i] & 0xFF); // LSB
        byteArray[2 * i + 1] = (uint8_t)((shortArray[i] >> 8) & 0xFF); // MSB
    }
}

unsigned int crc32b(unsigned char *message, size_t len) {
    int i, j;
    unsigned int byte, crc, mask;

    i = 0;
    crc = 0xFFFFFFFF;
    while (len--) {
        byte = message[i];            // Get next byte.
        crc = crc ^ byte;
        for (j = 7; j >= 0; j--) {    // Do eight times.
            mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
        i = i + 1;
    }
    return ~crc;
}


int sendSamplesToNetwork(short pcmSampleArrayInt[3240], short bufUsedSizeShort, int sock, struct sockaddr_in server_addr) {

    // Step 1: Calculate the length of pcmSampleArrayInt
    size_t shortArrayLength = 3240;

    // Step 2: Create a byte array
    uint8_t byteArray[shortArrayLength * 2];

    // Step 3: Call the shortArrayToByteArray function
    shortArrayToByteArray(pcmSampleArrayInt, shortArrayLength, byteArray);
    // Calculate the size of each chunk
    size_t chunkSize = shortArrayLength / 3 * 2 / 6;

    // Loop to send the data 6 times
    for (int j = 0; j < 18; j++) {
        // Calculate the start and end indices of the chunk
        size_t start = j * chunkSize;
        size_t end = start + chunkSize;

        // Make sure the end index does not exceed the array length
        if (end > shortArrayLength * 2) {
            end = shortArrayLength * 2;
        }

//        unsigned int hash = crc32b((unsigned char *)(byteArray + start), (end - start));
//        printf("Hash: %08x\r\n", hash); // Print hash in hexadecimal format

        // Send the chunk
        if (sendto(sock, (const char *)(byteArray + start), (end - start),
                   MSG_CONFIRM, (const struct sockaddr *) &server_addr,
                   sizeof(server_addr)) < 0) {
            perror("Send failed");
            exit(EXIT_FAILURE);
        }
    }


    return bufUsedSizeShort;
}

//If you're using the library to encode and decode sound in your project, and the resulting audio is nowhere near
// telephony quality but sort of warbled, the most likely cause is that you're using the same gsm state to both encode
// and decode. Don't do that; allocate two different states instead, one for each direction.
// https://www.quut.com/gsm/
int DecodeFrame(gsm g, gsm_frame frame, gsm_signal dst[kSamples]) {
    gsm obj = g;
    if (!obj) {
        obj = gsm_create();
    }
    if (!obj) {
        printf("gsm_create failed\r\n");
        return 0; // false in C
    }
    printf("%s hhhhhhhhhh frame %d dst %d\r\n", getCurrentDateTimeWithMillis(), sizeof frame, sizeof dst);
    int result = gsm_decode(obj, frame, dst);
    if (result != 0) {
        printf("gsm_decode: %d\r\n", result);
        return 0; // false in C
    }
//    PrintSamples(dst);
//    printf("\r\n");
    if (!g) {
        gsm_destroy(obj);
    }
    return 1; // true in C
}

int EncodeSamples(gsm g, gsm_signal src[kSamples], gsm_frame frame) {
    gsm obj = g;
    if (!obj) {
        obj = gsm_create();
    }
    if (!obj) {
        printf("gsm_create failed\r\n");
        return 0; // false in C
    }
    printf("%s gggggggggg src %d frame %d\r\n", getCurrentDateTimeWithMillis(), sizeof src, sizeof frame);
    gsm_encode(obj, src, frame);
//    PrintFrame(frame);
//    printf("\r\n");
    if (!g) {
        gsm_destroy(obj);
    }
    return 1; // true in C
}

void PrintSamples(gsm_signal samples[kSamples]) {
    printf("eeeeeeeeee print samples: ");
    for (int i = 0; i < kSamples; ++i) {
        printf("%d ", samples[i]);
    }
    printf("\r\n");
}

void PrintFrame(gsm_frame frame) {
    printf("ffffffffff print frame: ");
    for (int i = 0; i < kFrameSize; ++i) {
        printf("%d ", frame[i]);
    }
    printf("\r\n");
}
