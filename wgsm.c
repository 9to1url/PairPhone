#include <stdio.h>
#include <string.h>
#include "gsm.h"

const int kSamples = 160;
const int kFrameSize = 33;

void PrintSamples(gsm_signal samples[kSamples]) {
    for (int i = 0; i < kSamples; ++i) {
        printf("%d ", samples[i]);
    }
    printf("\n");
}

void PrintFrame(gsm_frame frame) {
    for (int i = 0; i < kFrameSize; ++i) {
        printf("%d ", frame[i]);
    }
    printf("\n");
}

int DecodeFrame(gsm g, gsm_frame frame, gsm_signal dst[kSamples]) {
    gsm obj = g;
    if (!obj) {
        obj = gsm_create();
    }
    if (!obj) {
        printf("gsm_create failed\n");
        return 0; // false in C
    }
    int result = gsm_decode(obj, frame, dst);
    if (result != 0) {
        printf("gsm_decode: %d\n", result);
        return 0; // false in C
    }
    PrintSamples(dst);
    printf("\r\n");
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
    gsm_encode(obj, src, frame);
    PrintFrame(frame);
    printf("\r\n");
    if (!g) {
        gsm_destroy(obj);
    }
    return 1; // true in C
}

//int main(int argc, char* argv[]) {
//    gsm_signal src[kSamples], dst[kSamples];
//    gsm_frame frame;
//
//    memset(src, 0, sizeof(src));
//    memset(dst, 0, sizeof(dst));
//
//    EncodeSamples(NULL, src, frame);
//    DecodeFrame(NULL, frame, dst);
//
//    EncodeSamples(NULL, dst, frame);
//    DecodeFrame(NULL, frame, dst);
//
//    EncodeSamples(NULL, dst, frame);
//    DecodeFrame(NULL, frame, dst);
//
//    /* Uncomment and modify according to your needs
//    for (int i = 0; i < kSamples; i++) {
//        src[i] = i * 30;
//    }
//    gsm_encode(g, src, frame);
//    gsm_decode(g, frame, dst);
//
//    for (int i = 0; i < kFrameSize; i++) {
//        frame[i] = i * 7;
//    }
//    DecodeFrame(NULL, frame, dst);
//
//    gsm enc = gsm_create();
//    gsm dec = gsm_create();
//    if (!enc || !dec) {
//        return 1;
//    }
//
//    gsm_destroy(enc);
//    gsm_destroy(dec);
//    */
//    return 0;
//}
//
