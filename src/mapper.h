#ifndef NESTICLE_MAPPER_H
#define NESTICLE_MAPPER_H
void reset_release_mapper();
void write_release_mapper(unsigned short address,unsigned char value);
void save_mapper_042(unsigned char *state);
void restore_mapper_042(const unsigned char *state);
void mapper9_latch(bool second);
extern int irq_enabled,irq_line,irq_origin;
void set_irq_042(int line);
void enable_irq_042();
void disable_irq_042();
#endif
