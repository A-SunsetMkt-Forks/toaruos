#include <kernel/printf.h>
#include <kernel/types.h>
#include <kernel/video.h>
#include <kernel/pci.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>
#include <kernel/args.h>

extern uint32_t lfb_resolution_s;
extern fs_node_t * lfb_device;
static uintptr_t ctrl_regs = 0;

static uint32_t i965_mmio_read(uint32_t reg) {
	return *(volatile uint32_t*)(ctrl_regs + reg);
}

static void i965_mmio_write(uint32_t reg, uint32_t val) {
	*(volatile uint32_t*)(ctrl_regs + reg) = val;
}

static void split(uint32_t val, uint32_t * a, uint32_t * b) {
	*a = (val & 0xFFFF) + 1;
	*b = (val >> 16) + 1;
}

static void setup_framebuffer(uint32_t pcidev) {
	/* Map BAR space for the control registers */
	uint32_t ctrl_space = pci_read_field(pcidev, PCI_BAR0, 4);
	pci_write_field(pcidev, PCI_BAR0, 4, 0xFFFFFFFF);
	uint32_t ctrl_size = pci_read_field(pcidev, PCI_BAR0, 4);
	ctrl_size = ~(ctrl_size & -15) + 1;
	pci_write_field(pcidev, PCI_BAR0, 4, ctrl_space);
	ctrl_space &= 0xFFFFFF00;
	ctrl_regs = (uintptr_t)mmu_map_mmio_region(ctrl_space, ctrl_size);

	/* Disable pipe A while we update source size */
	uint32_t pipe = i965_mmio_read(0x70008);
	i965_mmio_write(0x70008, pipe & ~(1 << 31));
	while (i965_mmio_read(0x70008) & (1 << 30));

	/* Set source size */
	i965_mmio_write(0x6001c, ((1440 - 1) << 16) | (900 - 1));

	/* Re-enable pipe */
	pipe = i965_mmio_read(0x70008);
	i965_mmio_write(0x70008, pipe | (1 << 31));
	while (!(i965_mmio_read(0x70008) & (1 << 30)));

	/* Keep the plane enabled while we update stride value */
	i965_mmio_write(0x70184, 0);
	i965_mmio_write(0x70188, 1440 * 4);
	i965_mmio_write(0x7019c, 0);

	/* Update the values we expose to userspace. */
	lfb_resolution_x = 1440;
	lfb_resolution_y = 900;
	lfb_resolution_b = 32;
	lfb_resolution_s = i965_mmio_read(0x70188);
	lfb_device->length  = lfb_resolution_s * lfb_resolution_y;
}

static void find_intel(uint32_t device, uint16_t v, uint16_t d, void * extra) {
	if (v == 0x8086 && d == 0x0046) {
		setup_framebuffer(device);
	}
}

void i965_initialize(void) {
	if (args_present("noi965")) return;
	pci_scan(find_intel, -1, NULL);
}
