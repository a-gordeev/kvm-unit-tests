#ifndef _ASM_X86_IO_H_
#define _ASM_X86_IO_H_

#define __force
#define __iomem

#define inb inb
static inline uint8_t inb(unsigned long port)
{
    unsigned char value;
    asm volatile("inb %w1, %0" : "=a" (value) : "Nd" ((unsigned short)port));
    return value;
}

#define inw inw
static inline uint16_t inw(unsigned long port)
{
    unsigned short value;
    asm volatile("inw %w1, %0" : "=a" (value) : "Nd" ((unsigned short)port));
    return value;
}

#define inl inl
static inline uint32_t inl(unsigned long port)
{
    unsigned int value;
    asm volatile("inl %w1, %0" : "=a" (value) : "Nd" ((unsigned short)port));
    return value;
}

#define outb outb
static inline void outb(uint8_t value, unsigned long port)
{
    asm volatile("outb %b0, %w1" : : "a"(value), "Nd"((unsigned short)port));
}

#define outw outw
static inline void outw(uint16_t value, unsigned long port)
{
    asm volatile("outw %w0, %w1" : : "a"(value), "Nd"((unsigned short)port));
}

#define outl outl
static inline void outl(uint32_t value, unsigned long port)
{
    asm volatile("outl %0, %w1" : : "a"(value), "Nd"((unsigned short)port));
}

#define ioremap ioremap
void __iomem *ioremap(phys_addr_t phys_addr, size_t size);

#include <asm-generic/io.h>

#endif
