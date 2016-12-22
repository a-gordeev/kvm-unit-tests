/*
 * PCI bus operation test
 *
 * Copyright (C) 2016, Red Hat Inc, Alexander Gordeev <agordeev@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <libcflat.h>
#include <smp.h>
#include <pci.h>
#include <x86/vm.h>

int main(void)
{
	smp_init();
	setup_vm();

	pci_print();

	if (pci_find_dev(PCI_VENDOR_ID_REDHAT,
			 PCI_DEVICE_ID_REDHAT_TEST) != PCIDEVADDR_INVALID) {
		int ret = pci_testdev();
		report("PCI test device passed %d tests",
			ret >= PCI_TESTDEV_NUM_BARS * PCI_TESTDEV_NUM_TESTS,
			ret);
	}

	return report_summary();
}
