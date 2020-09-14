#include <stdio.h>
#include <string.h>
#include "melpe/melpe.h"

// decode
int main(int argc, char *argv[])
{
    FILE *fin;
    FILE *fout;

    unsigned char txbuf[11]; //buffer for encoded melpe frame or silency descryptor
    unsigned char temp;
    short spbuf[540]; //buffer for accumulate resampled voice up to melpe frame

    if (argc < 3)
    {
        fprintf(stderr, "MELPe decoder with VAD.\n");
        fprintf(stderr, "Usage: %s melpe_bitstream pcm_samples\n", argv[0]);
        return -1;
    }

    fin = fopen(argv[1], "r");
    fout = fopen(argv[2], "w");

    if (!fin || !fout)
    {
        fprintf(stderr, "File could not be opened.\n");
        return -1;
    }

    melpe_i();

    while ( fread(txbuf, 1, 1, fin) == 1 )
    {
        if (txbuf[0] & 2) // VAD flag
        {
            memset(spbuf, 0, 540 * 2);
        }
        else
        {
            temp = txbuf[0];
            fread(txbuf+1, 1, 10, fin);
            txbuf[0] = txbuf[10];
            txbuf[10] = temp;
            melpe_s(spbuf, txbuf);
        }

        fwrite(spbuf, 2, 540, fout);
    }

    fclose(fin);
    fclose(fout);

    return 0;
}
