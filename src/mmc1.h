#ifndef NESTICLE_MMC1_H
#define NESTICLE_MMC1_H

void reset_mmc1();
void write_mmc1(unsigned short address, unsigned char data);
void save_mmc1(unsigned char *state);
void restore_mmc1(const unsigned char *state);

#endif
