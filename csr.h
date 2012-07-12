#include <stdint.h>

#define CSR_VARID_RAND				0x282a	/* uint16 */
#define CSR_VARID_COLD_RESET                    0x4001  /* valueless */
#define CSR_VARID_WARM_RESET                    0x4002  /* valueless */
#define CSR_VARID_COLD_HALT                     0x4003  /* valueless */
#define CSR_VARID_WARM_HALT                     0x4004  /* valueless */

int csr_open_hci(char *device);
int csr_read_hci(uint16_t varid, uint8_t *value, uint16_t length);
int csr_write_hci(uint16_t varid, uint8_t *value, uint16_t length);
void csr_close_hci(void);
