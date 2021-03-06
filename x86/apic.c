#include "libcflat.h"
#include "apic.h"
#include "vm.h"
#include "smp.h"
#include "desc.h"
#include "isr.h"
#include "msr.h"

static void test_lapic_existence(void)
{
    u32 lvr;

    lvr = apic_read(APIC_LVR);
    printf("apic version: %x\n", lvr);
    report("apic existence", (u16)lvr == 0x14);
}

#define TSC_DEADLINE_TIMER_MODE (2 << 17)
#define TSC_DEADLINE_TIMER_VECTOR 0xef
#define MSR_IA32_TSC            0x00000010
#define MSR_IA32_TSCDEADLINE    0x000006e0

static int tdt_count;

static void tsc_deadline_timer_isr(isr_regs_t *regs)
{
    ++tdt_count;
}

static void start_tsc_deadline_timer(void)
{
    handle_irq(TSC_DEADLINE_TIMER_VECTOR, tsc_deadline_timer_isr);
    irq_enable();

    wrmsr(MSR_IA32_TSCDEADLINE, rdmsr(MSR_IA32_TSC));
    asm volatile ("nop");
    report("tsc deadline timer", tdt_count == 1);
    report("tsc deadline timer clearing", rdmsr(MSR_IA32_TSCDEADLINE) == 0);
}

static int enable_tsc_deadline_timer(void)
{
    uint32_t lvtt;

    if (cpuid(1).c & (1 << 24)) {
        lvtt = TSC_DEADLINE_TIMER_MODE | TSC_DEADLINE_TIMER_VECTOR;
        apic_write(APIC_LVTT, lvtt);
        start_tsc_deadline_timer();
        return 1;
    } else {
        return 0;
    }
}

static void test_tsc_deadline_timer(void)
{
    if(enable_tsc_deadline_timer()) {
        printf("tsc deadline timer enabled\n");
    } else {
        printf("tsc deadline timer not detected\n");
    }
}

static void do_write_apicbase(void *data)
{
    wrmsr(MSR_IA32_APICBASE, *(u64 *)data);
}

void test_enable_x2apic(void)
{
    u64 invalid_state = APIC_DEFAULT_PHYS_BASE | APIC_BSP | APIC_EXTD;
    u64 apic_enabled = APIC_DEFAULT_PHYS_BASE | APIC_BSP | APIC_EN;
    u64 x2apic_enabled =
        APIC_DEFAULT_PHYS_BASE | APIC_BSP | APIC_EN | APIC_EXTD;

    if (enable_x2apic()) {
        printf("x2apic enabled\n");

        report("x2apic enabled to invalid state",
               test_for_exception(GP_VECTOR, do_write_apicbase,
                                  &invalid_state));
        report("x2apic enabled to apic enabled",
               test_for_exception(GP_VECTOR, do_write_apicbase,
                                  &apic_enabled));

        wrmsr(MSR_IA32_APICBASE, APIC_DEFAULT_PHYS_BASE | APIC_BSP);
        report("disabled to invalid state",
               test_for_exception(GP_VECTOR, do_write_apicbase,
                                  &invalid_state));
        report("disabled to x2apic enabled",
               test_for_exception(GP_VECTOR, do_write_apicbase,
                                  &x2apic_enabled));

        wrmsr(MSR_IA32_APICBASE, apic_enabled);
        report("apic enabled to invalid state",
               test_for_exception(GP_VECTOR, do_write_apicbase,
                                  &invalid_state));

        wrmsr(MSR_IA32_APICBASE, x2apic_enabled);
        apic_write(APIC_SPIV, 0x1ff);
    } else {
        printf("x2apic not detected\n");

        report("enable unsupported x2apic",
               test_for_exception(GP_VECTOR, do_write_apicbase,
                                  &x2apic_enabled));
    }
}

#define ALTERNATE_APIC_BASE	0x42000000

static void test_apicbase(void)
{
    u64 orig_apicbase = rdmsr(MSR_IA32_APICBASE);
    u32 lvr = apic_read(APIC_LVR);
    u64 value;

    wrmsr(MSR_IA32_APICBASE, orig_apicbase & ~(APIC_EN | APIC_EXTD));
    wrmsr(MSR_IA32_APICBASE, ALTERNATE_APIC_BASE | APIC_BSP | APIC_EN);

    report_prefix_push("apicbase");

    report("relocate apic",
           *(volatile u32 *)(ALTERNATE_APIC_BASE + APIC_LVR) == lvr);

    value = orig_apicbase | (1UL << cpuid_maxphyaddr());
    report("reserved physaddr bits",
           test_for_exception(GP_VECTOR, do_write_apicbase, &value));

    value = orig_apicbase | 1;
    report("reserved low bits",
           test_for_exception(GP_VECTOR, do_write_apicbase, &value));

    wrmsr(MSR_IA32_APICBASE, orig_apicbase);
    apic_write(APIC_SPIV, 0x1ff);

    report_prefix_pop();
}

static int ipi_count;

static void self_ipi_isr(isr_regs_t *regs)
{
    ++ipi_count;
    eoi();
}

static void test_self_ipi(void)
{
    int vec = 0xf1;

    handle_irq(vec, self_ipi_isr);
    irq_enable();
    apic_icr_write(APIC_DEST_SELF | APIC_DEST_PHYSICAL | APIC_DM_FIXED | vec,
                   0);
    asm volatile ("nop");
    report("self ipi", ipi_count == 1);
}

volatile int nmi_counter_private, nmi_counter, nmi_hlt_counter, sti_loop_active;

void sti_nop(char *p)
{
    asm volatile (
		  ".globl post_sti \n\t"
		  "sti \n"
		  /*
		   * vmx won't exit on external interrupt if blocked-by-sti,
		   * so give it a reason to exit by accessing an unmapped page.
		   */
		  "post_sti: testb $0, %0 \n\t"
		  "nop \n\t"
		  "cli"
		  : : "m"(*p)
		  );
    nmi_counter = nmi_counter_private;
}

static void sti_loop(void *ignore)
{
    unsigned k = 0;

    while (sti_loop_active) {
	sti_nop((char *)(ulong)((k++ * 4096) % (128 * 1024 * 1024)));
    }
}

static void nmi_handler(isr_regs_t *regs)
{
    extern void post_sti(void);
    ++nmi_counter_private;
    nmi_hlt_counter += regs->rip == (ulong)post_sti;
}

static void update_cr3(void *cr3)
{
    write_cr3((ulong)cr3);
}

static void test_sti_nmi(void)
{
    unsigned old_counter;

    if (cpu_count() < 2) {
	return;
    }

    handle_irq(2, nmi_handler);
    on_cpu(1, update_cr3, (void *)read_cr3());

    sti_loop_active = 1;
    on_cpu_async(1, sti_loop, 0);
    while (nmi_counter < 30000) {
	old_counter = nmi_counter;
	apic_icr_write(APIC_DEST_PHYSICAL | APIC_DM_NMI | APIC_INT_ASSERT, 1);
	while (nmi_counter == old_counter) {
	    ;
	}
    }
    sti_loop_active = 0;
    report("nmi-after-sti", nmi_hlt_counter == 0);
}

static volatile bool nmi_done, nmi_flushed;
static volatile int nmi_received;
static volatile int cpu0_nmi_ctr1, cpu1_nmi_ctr1;
static volatile int cpu0_nmi_ctr2, cpu1_nmi_ctr2;

static void multiple_nmi_handler(isr_regs_t *regs)
{
    ++nmi_received;
}

static void kick_me_nmi(void *blah)
{
    while (!nmi_done) {
	++cpu1_nmi_ctr1;
	while (cpu1_nmi_ctr1 != cpu0_nmi_ctr1 && !nmi_done) {
	    pause();
	}
	if (nmi_done) {
	    return;
	}
	apic_icr_write(APIC_DEST_PHYSICAL | APIC_DM_NMI | APIC_INT_ASSERT, 0);
	/* make sure the NMI has arrived by sending an IPI after it */
	apic_icr_write(APIC_DEST_PHYSICAL | APIC_DM_FIXED | APIC_INT_ASSERT
		       | 0x44, 0);
	++cpu1_nmi_ctr2;
	while (cpu1_nmi_ctr2 != cpu0_nmi_ctr2 && !nmi_done) {
	    pause();
	}
    }
}

static void flush_nmi(isr_regs_t *regs)
{
    nmi_flushed = true;
    apic_write(APIC_EOI, 0);
}

static void test_multiple_nmi(void)
{
    int i;
    bool ok = true;

    if (cpu_count() < 2) {
	return;
    }

    sti();
    handle_irq(2, multiple_nmi_handler);
    handle_irq(0x44, flush_nmi);
    on_cpu_async(1, kick_me_nmi, 0);
    for (i = 0; i < 1000000; ++i) {
	nmi_flushed = false;
	nmi_received = 0;
	++cpu0_nmi_ctr1;
	while (cpu1_nmi_ctr1 != cpu0_nmi_ctr1) {
	    pause();
	}
	apic_icr_write(APIC_DEST_PHYSICAL | APIC_DM_NMI | APIC_INT_ASSERT, 0);
	while (!nmi_flushed) {
	    pause();
	}
	if (nmi_received != 2) {
	    ok = false;
	    break;
	}
	++cpu0_nmi_ctr2;
	while (cpu1_nmi_ctr2 != cpu0_nmi_ctr2) {
	    pause();
	}
    }
    nmi_done = true;
    report("multiple nmi", ok);
}

int main()
{
    setup_vm();
    smp_init();
    setup_idt();

    test_lapic_existence();

    mask_pic_interrupts();
    test_enable_x2apic();
    test_apicbase();

    test_self_ipi();

    test_sti_nmi();
    test_multiple_nmi();

    test_tsc_deadline_timer();

    return report_summary();
}
