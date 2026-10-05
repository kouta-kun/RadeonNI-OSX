/*
 * The accelerator service of the Radeon HD 7570 driver.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <IOKit/IOLib.h>
#include <IOKit/graphics/IOFramebuffer.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>

#include "RadeonNIAccel.h"

#define super IOAccelerator
OSDefineMetaClassAndStructors(RadeonNIAccel, IOAccelerator)

/* The bundle OpenGL loads from /System/Library/Extensions. */
#define GL_BUNDLE_NAME		"RadeonNIGLDriver"

RadeonNIAccel *RadeonNIAccel::withFramebuffer(IOFramebuffer *fb,
					      IOService *provider)
{
	RadeonNIAccel *accel = new RadeonNIAccel;
	char path[512];
	int len = sizeof(path);

	if (!accel)
		return 0;
	if (!accel->init() || !accel->attach(provider)) {
		accel->release();
		return 0;
	}
	accel->fFramebuffer = fb;
	accel->setName("RadeonNIAccel");
	accel->setProperty("IOMatchCategory", "IOAccelerator");
	accel->setProperty("IOGLBundleName", GL_BUNDLE_NAME);
	accel->setProperty(kIOAccelRevisionKey,
			   (UInt64)kCurrentGraphicsInterfaceRevision, 32);

	/* The framebuffer points at its accelerator by registry path. */
	if (accel->getPath(path, &len, gIOServicePlane)) {
		fb->setProperty(kIOAccelTypesKey, path);
		fb->setProperty(kIOAccelIndexKey, (UInt64)0, 32);
		fb->setProperty(kIOAccelRevisionKey,
				(UInt64)kCurrentGraphicsInterfaceRevision, 32);
		IOLog("RadeonNI: accelerator at %s\n", path);
	}
	accel->registerService();
	return accel;
}

void RadeonNIAccel::retire(IOService *provider)
{
	if (fFramebuffer) {
		fFramebuffer->removeProperty(kIOAccelTypesKey);
		fFramebuffer->removeProperty(kIOAccelIndexKey);
		fFramebuffer->removeProperty(kIOAccelRevisionKey);
		fFramebuffer = 0;
	}
	terminate(kIOServiceRequired | kIOServiceSynchronous);
	detach(provider);
}

IOReturn RadeonNIAccel::newUserClient(task_t owningTask, void *securityID,
				      UInt32 type, IOUserClient **handler)
{
	IOLog("RadeonNI: accelerator user client type %lu requested, none exist yet\n",
	      (unsigned long)type);
	return kIOReturnUnsupported;
}
