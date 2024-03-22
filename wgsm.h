#ifndef WGSM_H
#define WGSM_H

#include "gsm.h"

extern const int kSamples;
extern const int kFrameSize;

void PrintSamples(gsm_signal samples[kSamples]);
void PrintFrame(gsm_frame frame);
int DecodeFrame(gsm g, gsm_frame frame, gsm_signal dst[kSamples]);
int EncodeSamples(gsm g, gsm_signal src[kSamples], gsm_frame frame);

int DecodeFrames(gsm g, gsm_frame frames[21], gsm_signal dst[21][kSamples]);
#endif // WGSM_H