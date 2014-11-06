
#include "crc.h"

/** @brief crc-ccitt truncated polynomial. */
#define POLY 0x1021          
/** @brief crc-ccitt initial value. */
#define INITIAL_VALUE 0xFFFF

uint16_t crc_compute(uint8_t *buf, uint16_t size)
{
   uint16_t index, crc;
   uint8_t v, xor_flag, byte, bit;
   
   crc = INITIAL_VALUE;

   for(index = 0; index < size; index++) {
      byte = buf[index];
      /*
	Align test bit with leftmost bit of the message byte.
      */
      v = 0x80;

      for(bit = 0; bit < 8; bit++) {
	 if(crc & 0x8000)
            xor_flag= 1;
	 else
            xor_flag= 0;

	 crc = crc << 1;
            
	 /*  Append next bit of message to end of CRC if it is not zero.
	     The zero bit placed there by the shift above need not be
	     changed if the next bit of the message is zero. */
	 if(byte & v)
	    crc= crc + 1;

	 if(xor_flag)
	    crc = crc ^ POLY;

	 /* Align test bit with next bit of the message byte. */
	 v = v >> 1;
      }
   }

   /* We have to augment the crc in order to comply with the ccitt spec. */
    for(bit = 0; bit < 16; bit++) {
        if(crc & 0x8000)
            xor_flag= 1;
        else
            xor_flag= 0;

	crc = crc << 1;

        if(xor_flag)
            crc = crc ^ POLY;
    }

    return crc;
}

