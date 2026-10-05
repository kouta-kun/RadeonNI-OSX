/*
 * The accelerator service of the Radeon HD 7570 driver.
 *
 * For now it only announces itself: it names the OpenGL driver bundle and
 * links the framebuffer to itself the way OpenGL and the window server
 * expect, and it logs which user clients they ask for. It creates none.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RADEONNIACCEL_H
#define RADEONNIACCEL_H

#include <IOKit/graphics/IOAccelerator.h>

class IOFramebuffer;

class RadeonNIAccel : public IOAccelerator
{
	OSDeclareDefaultStructors(RadeonNIAccel)

public:
	/* Create, attach under the PCI device and publish. */
	static RadeonNIAccel *withFramebuffer(IOFramebuffer *fb,
					      IOService *provider);
	void retire(IOService *provider);

	virtual IOReturn newUserClient(task_t owningTask, void *securityID,
				       UInt32 type, IOUserClient **handler);

private:
	IOFramebuffer *fFramebuffer;
};

#endif /* RADEONNIACCEL_H */
