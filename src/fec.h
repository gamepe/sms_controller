
#include "inttypes.h"


/** @brief Maximum allowed number of parity bytes */
#define MAXIMUM_PARITY_BYTES 64
#define MAXIMUM_IMPLEMENTED_PARITY_BYTES 8

/** @brief Return value if fec_decode detects no errors */
#define FEC_NO_ERRORS 0

/** @brief Return value if fec_decode is able to fix all errors */
#define FEC_CORRECTED_ERRORS 1

/** @brief Return value if fec_decode cannot fix all errors */
#define FEC_UNCORRECTABLE_ERRORS 2

/** @brief Initalize the fec subsystem
 * to the specified number of parity bytes.
 * @param parity_bytes Number of parity bytes to be used in FEC
 */
void fec_init (uint8_t parity_bytes);

/** @brief Return the number of parity bytes the FEC subsystem is currently using.
    @return number of parity bytes
 */
uint8_t fec_get_parity_bytes ();

/** @brief Encode the parity bytes for a portion of memory
 * @param src Place in memory to start computing FEC parity bytes for
 * @param len how much memory (in bytes) to compute the FEC parity bytes for
 * @param parity_ptr Location in memory to store the computed parity bytes.
 */
void fec_encode (uint8_t *src, uint8_t len, uint8_t *parity_ptr);


/** @brief Use FEC to correct errors if possible
 *
 * Note: this function actually changes the passed in buffer
 * if it is possible to fix errors. Also note that for every
 * 2 parity bytes, one byte error can be fixed.
 * @param src location to start Forward Error Correction
 * @param len how much memory (in bytes) to preform FEC on
 * @param fec_ptr location of parity bytes.
 * @return one of FEC_NO_ERRORS, FEC_CORRECTED_ERRORS, FEC_UNCORRECTABLE_ERRORS
 */
uint8_t fec_decode (uint8_t *src, uint8_t len, uint8_t *fec_ptr);
