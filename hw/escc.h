/* escc.c */
#define ESCC_SIZE 4
MemoryRegion *escc_init(target_phys_addr_t base, qemu_irq irqA, qemu_irq irqB,
              CharDriverState *chrA, CharDriverState *chrB,
              int clock, int it_shift, int reg_bit);

void slavio_serial_ms_kbd_init(target_phys_addr_t base, qemu_irq irq,
                               int disabled, int clock, int it_shift);
