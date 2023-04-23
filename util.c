#include "util.h"
#include "string.h"
#include "process_packet.h"
#include "config.h"

#define SREG    *((volatile char*) 0x5F)

#define C_SREG 0

unsigned char calc_checksum(unsigned char *data, unsigned int dsize)
{
    //Storing checksum result in unsigned byte
    unsigned char checksum_result;

    for(int i=0;i<dsize;i++)
    {
        checksum_result += data[i];
        if(SREG & (1<<C_SREG))
        {
            //carry wraparound
            checksum_result+=1;
        }
    }

    return ~checksum_result;
}

/*

void update_checksum(unsigned char *data, unsigned int dsize)
{
    unsigned char checksum_val = calc_checksum(data, dsize);
    unsigned char last_byte = 0b11111111 + (~checksum_val + 0b00000001);
    data[dsize-1]=last_byte;
}


int is_checksum_valid(unsigned char* data, unsigned int dsize)
{
    unsigned char checksum_val = calc_checksum(data, dsize);

    if(checksum_val==0)
        return 1;
    else
        return 0;
}

*/


int update_tcrit_hi(int value)
{
    if (value > config.hi_warn && value <= 0x3FF)
    {
        //Update value
        config.hi_alarm = value;

        //return with no errors
        return 0;
    }

    return 1;
}

int update_twarn_hi(int value)
{
    if (value > config.lo_warn && value < config.hi_alarm)
    {
        //Update value
        config.hi_warn = value;

        //return with no errors
        return 0;
    }

    return 1;
}

int update_twarn_lo(int value)
{
    if(value > config.lo_alarm && value < config.hi_warn)
    {
        //Update value
        config.lo_warn = value;

        //return with 0 errors
        return 0;
    }

    return 1;
}

int update_tcrit_lo(int value)
{
    if(value < config.lo_warn)
    {
        //Update
        config.lo_alarm = value;

        //return with no errors
        return 0;
    }

    return 1;
}

