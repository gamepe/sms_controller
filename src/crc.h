

#ifndef __CRC_H__
#define __CRC_H__

#include <inttypes.h>

#define CRC_OK 0
#define CRC_ERROR -1

/** @file crc.h
 * @brief Calculate the CRC-16 CCITT of buffer that is size bytes long.
 * @param buf Pointer to data buffer.
 * @param size number of bytes in data buffer
 * @return CRC-16 of buffer[0 .. length]
 */
uint16_t crc_compute(uint8_t *buf, uint16_t size);
#endif

