/* Debian95 — minimal PCI config access (mechanism 1) + device find. */
#include <debian95/net.h>
#include <debian95/rtl.h>

static u32 cfg_read(u8 bus, u8 slot, u8 func, u8 off) {
    u32 addr = (1u << 31) | ((u32)bus << 16) | ((u32)slot << 11) |
               ((u32)func << 8) | (off & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}
static void cfg_write(u8 bus, u8 slot, u8 func, u8 off, u32 val) {
    u32 addr = (1u << 31) | ((u32)bus << 16) | ((u32)slot << 11) |
               ((u32)func << 8) | (off & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

int pci_find(u16 vendor, u16 device, PciDev *out) {
    for (int bus = 0; bus < 4; bus++)
        for (int slot = 0; slot < 32; slot++) {
            u32 id = cfg_read((u8)bus, (u8)slot, 0, 0x00);
            if ((id & 0xFFFF) != vendor || (id >> 16) != device) continue;
            out->bus = (u8)bus; out->slot = (u8)slot; out->func = 0;
            u32 bar0 = cfg_read((u8)bus, (u8)slot, 0, 0x10);
            out->io_base = (u16)(bar0 & ~0x3u);       /* I/O BAR */
            out->irq = (u8)(cfg_read((u8)bus, (u8)slot, 0, 0x3C) & 0xFF);
            /* enable I/O space + bus mastering */
            u32 cmd = cfg_read((u8)bus, (u8)slot, 0, 0x04);
            cfg_write((u8)bus, (u8)slot, 0, 0x04, cmd | 0x05);
            return 0;
        }
    return -1;
}
