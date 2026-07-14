//
// XboxOneGamepadDriver.cpp — DriverKit implementation (SCAFFOLD)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// NOT YET BUILT. This is a structurally complete skeleton showing what a
// HIDDriverKit virtual gamepad needs. The descriptor and device-description
// are real; the report-injection path is outlined with TODOs. Build inside
// an Xcode "Driver Extension" target — see ../README.md.
//

#include <os/log.h>
#include <DriverKit/DriverKit.h>
#include <DriverKit/OSData.h>
#include <DriverKit/OSDictionary.h>
#include <DriverKit/OSNumber.h>
#include <DriverKit/OSString.h>
#include <DriverKit/IOBufferMemoryDescriptor.h>
#include <HIDDriverKit/HIDDriverKit.h>

#include "XboxOneGamepadDriver.h"
#include "../shared/gamepad_report.h"

#define LOG(fmt, ...) os_log(OS_LOG_DEFAULT, "XboxGamepad: " fmt, ##__VA_ARGS__)

// Identify as a Microsoft Xbox controller so that not only IOHIDManager /
// SDL based software but also Apple's GameController.framework recognizes it.
#define VENDOR_ID   0x045E   // Microsoft
#define PRODUCT_ID  0x028E   // Xbox 360 pad (widely recognized profile)
#define VERSION_BCD 0x0114

struct XboxOneGamepadDriver_IVars
{
    // Reserved for future state (e.g. a pending rumble report).
    bool started;
};

bool XboxOneGamepadDriver::init(void)
{
    if (!super::init())
    {
        return false;
    }

    ivars = IONewZero(XboxOneGamepadDriver_IVars, 1);

    return ivars != nullptr;
}

void XboxOneGamepadDriver::free(void)
{
    IOSafeDeleteNULL(ivars, XboxOneGamepadDriver_IVars, 1);
    super::free();
}

kern_return_t
IMPL(XboxOneGamepadDriver, Start)
{
    kern_return_t ret = Start(provider, SUPERDISPATCH);

    if (ret != kIOReturnSuccess)
    {
        LOG("super::Start failed 0x%x", ret);
        return ret;
    }

    ivars->started = true;

    // RegisterService publishes us so the userspace driver can open us.
    RegisterService();

    LOG("started");

    return kIOReturnSuccess;
}

kern_return_t
IMPL(XboxOneGamepadDriver, Stop)
{
    ivars->started = false;
    LOG("stopped");

    return Stop(provider, SUPERDISPATCH);
}

// Provide the HID report descriptor (see ../shared/gamepad_report.h).
OSData *
XboxOneGamepadDriver::newReportDescriptor(void)
{
    return OSData::withBytes(kGamepadReportDescriptor,
                             sizeof(kGamepadReportDescriptor));
}

// Advertise vendor/product/name so the OS treats us as an Xbox controller.
OSDictionary *
XboxOneGamepadDriver::newDeviceDescription(void)
{
    OSDictionary * description = OSDictionary::withCapacity(8);
    if (!description)
    {
        return nullptr;
    }

    OSNumber * vid = OSNumber::withNumber((uint64_t)VENDOR_ID, 32);
    OSNumber * pid = OSNumber::withNumber((uint64_t)PRODUCT_ID, 32);
    OSNumber * ver = OSNumber::withNumber((uint64_t)VERSION_BCD, 32);
    OSString * name = OSString::withCString("Xbox One Wireless Controller");
    OSNumber * primaryUsagePage = OSNumber::withNumber((uint64_t)0x01, 32); // Generic Desktop
    OSNumber * primaryUsage     = OSNumber::withNumber((uint64_t)0x05, 32); // Game Pad

    description->setObject("VendorID", vid);
    description->setObject("ProductID", pid);
    description->setObject("VersionNumber", ver);
    description->setObject("Product", name);
    description->setObject("PrimaryUsagePage", primaryUsagePage);
    description->setObject("PrimaryUsage", primaryUsage);

    OSSafeReleaseNULL(vid);
    OSSafeReleaseNULL(pid);
    OSSafeReleaseNULL(ver);
    OSSafeReleaseNULL(name);
    OSSafeReleaseNULL(primaryUsagePage);
    OSSafeReleaseNULL(primaryUsage);

    return description;
}

// Inject one input report into the HID stack.
kern_return_t
XboxOneGamepadDriver::postInputReport(const void * report, uint64_t length)
{
    if (!report || length == 0 || length > sizeof(GamepadReport))
    {
        return kIOReturnBadArgument;
    }

    IOBufferMemoryDescriptor * buffer = nullptr;
    kern_return_t ret = IOBufferMemoryDescriptor::Create(
        kIOMemoryDirectionInOut, length, 0, &buffer);
    if (ret != kIOReturnSuccess || !buffer)
    {
        return ret;
    }

    uint64_t address = 0, len = 0;
    buffer->Map(0, 0, 0, 0, &address, &len);
    memcpy(reinterpret_cast<void *>(address), report, length);

    // handleReport timestamps and forwards the report to the HID stack.
    uint64_t timestamp = mach_absolute_time();
    ret = handleReport(timestamp, buffer, (uint32_t)length,
                       kIOHIDReportTypeInput, 0);

    OSSafeReleaseNULL(buffer);

    return ret;
}

// TODO: vend a small IOUserClient subclass whose externalMethod for
// selector kGamepadMethodSendReport (=0) calls postInputReport(). See
// ../README.md for the companion XboxOneGamepadUserClient.{iig,cpp}.
kern_return_t
IMPL(XboxOneGamepadDriver, NewUserClient)
{
    // Placeholder — must instantiate XboxOneGamepadUserClient and set its
    // provider to `this`.
    return kIOReturnUnsupported;
}
