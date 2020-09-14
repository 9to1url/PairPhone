#include <stdio.h>
#include <string.h>

#include "melpe/melpe.h"  //audio codec
#include "vad/vad2.h"   //voice active detector
#include "crp.h"

// encode
int main(int argc, char *argv[])
{

    FILE *fin;
    FILE *fout;

    unsigned char temp;
    unsigned char txbuf[11]; //buffer for encoded melpe frame or VAD flag
    short spbuf[540]; //buffer for accumulate resampled voice up to melpe frame

    if (argc < 3)
    {
        fprintf(stderr, "MELPe encoder with VAD.\n");
        fprintf(stderr, "Usage: %s input output\n", argv[0]);
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
    int sp_read, i;
    vadState2 vad; //Voice Active Detector state

    // encode first frame
    memset(spbuf, 0, 540 * 2);

//  add last was VAD var...
    do
    {
        sp_read = fread(spbuf, 2, 540, fin);

        //check frame is speech (by VAD)
        i=vad2(spbuf+10, &vad);
        i+=vad2(spbuf+100,&vad);
        i+=vad2(spbuf+190,&vad);
        i+=vad2(spbuf+280,&vad);
        i+=vad2(spbuf+370,&vad);
        i+=vad2(spbuf+460,&vad);

        printf("i = %d\n", i);

        if (i == 0)
        {
            fprintf(stderr, "Silent frame\n");
            txbuf[0] |= 2; // set VAD bit
            fwrite(txbuf, 1, 1, fout);
        }
        else
        {
            fprintf(stderr, "Voiced frame\n");
            melpe_a(txbuf, spbuf);
// invert the last and the first byte
            temp = txbuf[0];
            txbuf[0] = txbuf[10];
            txbuf[10] = temp;

            fwrite(txbuf, 1, 11, fout);
        }
        memset(spbuf, 0, 540 * 2);
    } while ( sp_read == 540 );

    fclose(fin);
    fclose(fout);
}
