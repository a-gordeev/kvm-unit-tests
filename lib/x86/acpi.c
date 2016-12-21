#include "libcflat.h"
#include "acpi.h"
#include "asm/io.h"

void* find_acpi_table_addr(u32 sig)
{
    unsigned long addr;
    struct rsdp_descriptor *rsdp;
    struct rsdt_descriptor_rev1 *rsdt;
    void *end;
    int i;

    /* FACS is special... */
    if (sig == FACS_SIGNATURE) {
        struct fadt_descriptor_rev1 *fadt;
        fadt = find_acpi_table_addr(FACP_SIGNATURE);
        if (!fadt) {
            return NULL;
        }
        return ioremap(fadt->firmware_ctrl, PAGE_SIZE);
    }

    for(addr = 0xf0000; addr < 0x100000; addr += 16) {
	rsdp = (void*)addr;
	if (rsdp->signature == 0x2052545020445352LL)
          break;
    }
    if (addr == 0x100000) {
        printf("Can't find RSDP\n");
        return NULL;
    }

    if (sig == RSDP_SIGNATURE) {
        return rsdp;
    }

    if (!rsdp->rsdt_physical_address) {
        return NULL;
    }

    rsdt = ioremap(rsdp->rsdt_physical_address, sizeof(*rsdt));
    if (rsdt->signature != RSDT_SIGNATURE) {
        return NULL;
    }
    rsdt = ioremap(rsdp->rsdt_physical_address, rsdt->length);

    if (sig == RSDT_SIGNATURE) {
        return rsdt;
    }

    end = (void*)rsdt + rsdt->length;
    for (i=0; (void*)&rsdt->table_offset_entry[i] < end; i++) {
        struct acpi_table *table;

        if (!rsdt->table_offset_entry[i]) {
            continue;
        }

        table = ioremap(rsdt->table_offset_entry[i], sizeof(*table));
        if (table->signature == sig) {
            return ioremap(rsdt->table_offset_entry[i], table->length);
        }
    }

    return NULL;
}
