/*
 * PCI bus operation test
 *
 * Copyright (C) 2016, Red Hat Inc, Alexander Gordeev <agordeev@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <libcflat.h>
#include <pci.h>

int main(void)
{
	if (!pci_probe())
		report_abort("PCI bus probing failed\n");

	pci_print();

	return report_summary();
}
