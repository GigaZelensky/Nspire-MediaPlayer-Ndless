#ifndef USB_AUDIO_TEST_PLATFORM_H
#define USB_AUDIO_TEST_PLATFORM_H
#include <stdint.h>
#include <stddef.h>
#define IRQ_USB (1U << 9)
extern volatile uint32_t usb_test_registers[64];
extern volatile uint32_t usb_test_irq_enable, usb_test_irq_disable;
extern unsigned usb_test_cx2;
void usb_test_poll(void);
void *usb_test_allocate(size_t);
void usb_test_free(void *);
static inline volatile uint32_t *usb_test_reg(unsigned offset)
{
    usb_test_poll();
    return usb_test_registers + offset / 4;
}
#define REG(off) (*usb_test_reg(off))
#define IRQ_ENABLE usb_test_irq_enable
#define IRQ_DISABLE usb_test_irq_disable
#define is_cx2 usb_test_cx2
static inline unsigned TCT_Local_Control_Interrupts(unsigned mask) { (void)mask; return 0; }
#endif
