/*
 * Reads the Radeon HD 7570's expansion ROM on a running Mac, without
 * touching the driver: an investigation tool, loaded by hand with kextload
 * and unloaded again (docs/JOURNAL.md, 2026-10-06).
 *
 * It turns on the ROM's address decoding (a configuration bit), reads the
 * first word with ml_probe_read(), which comes back with "false" instead of
 * a machine check when nothing answers, then reads the whole BAR through a
 * mapping and once more word by word, and publishes what it read as the
 * property "ROM" of its registry entry. Nothing is written to the ROM.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/pci/IOPCIDevice.h>

extern "C" {
/* osfmk/ppc/machine_routines.h; exported, but not in Kernel.framework. */
boolean_t ml_probe_read(vm_offset_t paddr, unsigned int *val);
}

class RadeonNIRomProbe : public IOService
{
	OSDeclareDefaultStructors(RadeonNIRomProbe)

public:
	virtual bool start(IOService *provider);

private:
	IOPCIDevice *findCard();
	void probe(IOPCIDevice *dev);
};

#define super IOService
OSDefineMetaClassAndStructors(RadeonNIRomProbe, IOService)

IOPCIDevice *RadeonNIRomProbe::findCard()
{
	OSDictionary *match = serviceMatching("IOPCIDevice");
	OSIterator *iter = match ? getMatchingServices(match) : 0;
	IOPCIDevice *found = 0;
	OSObject *obj;

	if (match)
		match->release();
	if (!iter)
		return 0;
	while (!found && (obj = iter->getNextObject())) {
		IOPCIDevice *dev = OSDynamicCast(IOPCIDevice, obj);

		if (dev && dev->configRead16(kIOPCIConfigVendorID) == 0x1002 &&
		    dev->configRead16(kIOPCIConfigDeviceID) == 0x675d) {
			dev->retain();
			found = dev;
		}
	}
	iter->release();
	return found;
}

void RadeonNIRomProbe::probe(IOPCIDevice *dev)
{
	IODeviceMemory *mem;
	IOMemoryMap *map;
	volatile const UInt8 *rom;
	UInt8 *copy;
	UInt32 bar, phys, length, image, i, differ = 0;
	unsigned int word = 0;
	UInt8 sum = 0;
	boolean_t ok;

	bar = dev->configRead32(kIOPCIConfigExpansionROMBase);
	IOLog("RomProbe: command %04x, ROM BAR %08lx\n",
	      dev->configRead16(kIOPCIConfigCommand), (unsigned long)bar);
	setProperty("BAR", bar, 32);
	mem = dev->getDeviceMemoryWithRegister(kIOPCIConfigExpansionROMBase);
	if (!(bar & 0xfffff800) || !mem) {
		IOLog("RomProbe: no address assigned to the ROM\n");
		return;
	}
	phys = mem->getPhysicalAddress();
	length = mem->getLength();
	IOLog("RomProbe: ROM at physical %08lx, %lu bytes\n",
	      (unsigned long)phys, (unsigned long)length);

	dev->configWrite32(kIOPCIConfigExpansionROMBase, bar | 1);
	IODelay(1000);
	ok = ml_probe_read(phys, &word);
	IOLog("RomProbe: first word: %s, %08x\n",
	      ok ? "answered" : "machine check", word);
	setProperty("Answered", ok ? kOSBooleanTrue : kOSBooleanFalse);
	if (!ok)
		goto out;

	map = dev->mapDeviceMemoryWithRegister(kIOPCIConfigExpansionROMBase);
	copy = (UInt8 *)IOMalloc(length);
	if (!map || !copy) {
		IOLog("RomProbe: cannot map or allocate\n");
		if (map)
			map->release();
		if (copy)
			IOFree(copy, length);
		goto out;
	}
	rom = (volatile const UInt8 *)map->getVirtualAddress();
	for (i = 0; i < length; i++)
		copy[i] = rom[i];
	map->release();

	image = (UInt32)copy[2] * 512;
	if (image > length)
		image = length;
	for (i = 0; i < image; i++)
		sum += copy[i];
	IOLog("RomProbe: starts %02x %02x %02x, image of %lu bytes, checksum %02x (0 is right)\n",
	      copy[0], copy[1], copy[2], (unsigned long)image, sum);

	/* The same bytes word by word through ml_probe_read(). */
	for (i = 0; i + 4 <= length; i += 4) {
		if (!ml_probe_read(phys + i, &word)) {
			IOLog("RomProbe: machine check at offset %lx\n",
			      (unsigned long)i);
			break;
		}
		if (copy[i] != (UInt8)(word >> 24) ||
		    copy[i + 1] != (UInt8)(word >> 16) ||
		    copy[i + 2] != (UInt8)(word >> 8) ||
		    copy[i + 3] != (UInt8)word)
			differ++;
	}
	IOLog("RomProbe: %lu bytes read word by word, %lu words differ from the mapped read\n",
	      (unsigned long)i, (unsigned long)differ);

	setProperty("ROM", copy, length);
	IOFree(copy, length);
out:
	dev->configWrite32(kIOPCIConfigExpansionROMBase, bar);
	IOLog("RomProbe: ROM BAR back to %08lx\n",
	      (unsigned long)dev->configRead32(kIOPCIConfigExpansionROMBase));
}

bool RadeonNIRomProbe::start(IOService *provider)
{
	IOPCIDevice *dev;

	if (!super::start(provider))
		return false;
	dev = findCard();
	if (!dev) {
		IOLog("RomProbe: no device 1002:675d\n");
	} else {
		probe(dev);
		dev->release();
	}
	registerService();
	return true;
}
