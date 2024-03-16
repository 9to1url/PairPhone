#include <stdio.h>
#include <stdlib.h>
#include <gsm.h>

int main() {
    gsm signal = gsm_create();
    if (!signal) {
        fprintf(stderr, "Failed to create GSM signal\n");
        exit(EXIT_FAILURE);
    }

    // Assuming `input_samples` contains 160 13-bit (downsampled to 8kHz) audio samples
    gsm_signal input_samples[160];
    gsm_byte encoded_signal[33]; // GSM frame is 33 bytes

    // Fill your input_samples here...

    // Encode the audio samples
    gsm_encode(signal, input_samples, encoded_signal);

    // At this point, `encoded_signal` contains the encoded GSM data.
    // You can write it to a file, send it over a network, etc.

    // Cleanup
    gsm_destroy(signal);

    return 0;
}

