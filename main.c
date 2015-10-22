//
//  main.c
//  USBTest
//
//  Created by Charles Hwang on 4/7/15.
//  Copyright (c) 2015 Charles Hwang. All rights reserved.
//

#include <stdio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/USBSpec.h>

//https://delog.wordpress.com/2012/04/27/access-usb-device-on-mac-os-x-using-io-kit/ - Writing a simple USB driver for the Mac
//https://github.com/jedi22/PoorMansXboxOneControllerDriver - Simple Xbox One USB Driver
//http://tattiebogle.net/index.php/ProjectRoot/Xbox360Controller/UsbInfo - Xbox 360 controller documented
//http://xboxforums.create.msdn.com/forums/p/37235/243706.aspx#243706 - Rock Band Stage Kit force feedback explained

//https://developer.apple.com/library/mac/documentation/DeviceDrivers/Conceptual/AccessingHardware/AH_IOKitLib_API/AH_IOKitLib_API.html - IOKitLib API explained
//https://developer.apple.com/library/mac/documentation/IOKit/Reference/IOKitLib_header_reference/#//apple_ref/c/func/IOIteratorNext - IOKItLib.h Reference



/** 
 TODO:
 - Figure out why ReadPipe isn't working
 - Make code more C-like
**/

CFMutableDictionaryRef matchingDictionary = NULL;
kern_return_t kr;
SInt32 idVendor = 0x0e6f;
SInt32 idProduct = 0x0103;
io_iterator_t iterator = 0;
io_service_t usbRef;
SInt32 score;
IOCFPlugInInterface** plugin;
IOUSBDeviceInterface300** usbDevice = NULL;
IOReturn ret;
IOUSBConfigurationDescriptorPtr config;
IOUSBFindInterfaceRequest interfaceRequest;
IOUSBInterfaceInterface300** usbInterface;

char* in;
UInt32 numBytes;

//left specifies which lights
//right specifies which color
//{0x00, 0x08, 0x00, ll, rr, 0x00, 0x00, 0x00}
const char red = 0x80;
const char orange = 0x60;
const char green = 0x40;
const char blue = 0x20;
const char off = 0xff;

const char allDiodes = 0xff;
const char left8 = 0x80;
const char left4 = 0x40;
const char left2 = 0x20;
const char left1 = 0x10;
const char right8 = 0x08;
const char right4 = 0x04;
const char right2 = 0x02;
const char right1 = 0x01;
const char all = 0xff;
char pattern[] = {0x00, 0x08, 0x00, left8, blue, 0x00, 0x00, 0x00};
char patternOff[] = {0x00, 0x08, 0x00, all, off, 0x00, 0x00, 0x00};

const char fogOn = 0x10;
const char fogOff = 0x20;
const char strobeSlow = 0x30;
const char strobeMedium = 0x40;
const char strobeFast = 0x50;
const char strobeFaster = 0x60;
const char strobeOff = 0x70;
char strobeFog[] =   {0x00, 0x08, 0x00, 0x00, fogOn, 0x00, 0x00, 0x00};

const char guideOff = 0x00;
const char allBlink = 0x01;
const char blink1On = 0x02;
const char blink2On = 0x03;
const char blink3On = 0x04;
const char blink4On = 0x05;
const char on1 = 0x06;
const char on2 = 0x07;
const char on3 = 0x08;
const char on4 = 0x09;
const char rotate = 0x0a;
char guideLEDs[] = {0x01, 0x03, blink1On};

/**
 * Finds the USB device using the device's vendor ID and product ID.
 * Returns true if it's found.
 **/
//could change this to return IOUSBDeviceInterface300?
bool findUSBDevice(SInt32 vendor, SInt32 product) {
    matchingDictionary = IOServiceMatching(kIOUSBDeviceClassName);
    CFDictionaryAddValue(matchingDictionary,
                         CFSTR(kUSBVendorID),
                         CFNumberCreate(kCFAllocatorDefault,
                                        kCFNumberSInt32Type, &vendor));
    CFDictionaryAddValue(matchingDictionary,
                         CFSTR(kUSBProductID),
                         CFNumberCreate(kCFAllocatorDefault,
                                        kCFNumberSInt32Type, &product));
    //    IOServiceGetMatchingServices(kIOMasterPortDefault,
    //                                 matchingDictionary, &iterator); //??Un-Delete
    //finds the device matching the dictionary
    kr = IOServiceGetMatchingServices(kIOMasterPortDefault,
                                                    matchingDictionary, &iterator); //helps with getting endpoints later
    
    //Iterator should hold only one device, in theory.
    usbRef = IOIteratorNext(iterator);
    if (usbRef == 0)
    {
        printf("Device not found\n");
        return false;
    }
    IOObjectRelease(iterator);
    IOCreatePlugInInterfaceForService(usbRef, kIOUSBDeviceUserClientTypeID,
                                      kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(usbRef);
    (*plugin)->QueryInterface(plugin,
                              CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID300),
                              (LPVOID)&usbDevice);
    (*plugin)->Release(plugin);
    
    return true;
}

/**
 * Gain access to the device.
 **/
bool activate() {
    //IOUSBLib.h
    /**
     Before the client can issue commands that change the state of the device, it
     must have succeeded in opening the device. This establishes an exclusive link
     between the client's task and the actual device.
     **/
    ret = (*usbDevice)->USBDeviceOpen(usbDevice);
    if (ret == kIOReturnSuccess)
    {
        // set first configuration as active
        ret = (*usbDevice)->GetConfigurationDescriptorPtr(usbDevice, 0, &config);
        if (ret != kIOReturnSuccess)
        {
            printf("Could not set active configuration (error: %x)\n", ret);
            return false;
        }
        (*usbDevice)->SetConfiguration(usbDevice, config->bConfigurationValue);
    }
    else if (ret == kIOReturnExclusiveAccess)
    {
        // this is not a problem as we can still do some things
    }
    else
    {
        printf("Could not open device (error: %x)\n", ret);
        return false;
    }
    return true;
}

/**
 * Access the interface of the USB device.
 **/
bool findOpenInterface() {
    //According to TattieBoogie, this is the proper interface for accessing the input and output of an Xbox 360 controller
    interfaceRequest.bInterfaceClass = 255;
    interfaceRequest.bInterfaceSubClass = 93;
    interfaceRequest.bInterfaceProtocol = 1;
    interfaceRequest.bAlternateSetting = kIOUSBFindInterfaceDontCare;
    (*usbDevice)->CreateInterfaceIterator(usbDevice,
                                          &interfaceRequest, &iterator);
    // We use interface #0
    //    IOIteratorNext(iterator); // skip interface #0
    usbRef = IOIteratorNext(iterator);
    IOObjectRelease(iterator);
    IOCreatePlugInInterfaceForService(usbRef,
                                      kIOUSBInterfaceUserClientTypeID,
                                      kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(usbRef);
    (*plugin)->QueryInterface(plugin,
                              CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300),
                              (LPVOID)&usbInterface);
    (*plugin)->Release(plugin);
    
    //Open interface
    ret = (*usbInterface)->USBInterfaceOpen(usbInterface);
    if (ret != kIOReturnSuccess)
    {
        printf("Could not open interface (error: %x)\n", ret);
        return false;
    }
    return true;
}

/**
 * Useful helper function get the number of endpoints on a USB device.
 **/
//Probably deletable.
void fromPoorMan() {
    CFRunLoopSourceRef source_ref;
    kr = (*usbInterface)->CreateInterfaceAsyncEventSource(usbInterface, &source_ref);
    
    unsigned char eps;
    kr = (*usbInterface)->GetNumEndpoints(usbInterface, &eps);
    
    printf("Endpoints: %d\n", eps);
}

/*** ---- The Good Stuff ---- ***/

/**
 * Sends a signal to the controller and then waits for timer seconds.
 **/
void sendBits(char * signal, int timer) {
    //Use endpoint 2 to output to the Xbox 360 controller
    ret = (*usbInterface)->WritePipe(usbInterface, 2, signal, sizeof(signal));
    if (ret == kIOReturnSuccess)
    {
        //        printf("Wrote %d bytes\n", numBytes);
        //        printf("Wrote: %s", signal);
        printf("Wrote something\n");
    }
    else
    {
        printf("Write failed (error: %x)\n", ret);
    }
    sleep(timer);
}

/**
 * Reads signal sent by the Xbox 360 controller, typically input from controller.
 **/
//Kind of broken...
void readBits() {
    //Use endpoint 1 to receieve input from the Xbox 360 controller
    numBytes = 32;
    in = malloc(numBytes);
    ret = (*usbInterface)->ReadPipe(usbInterface, 1, in, &numBytes);
    if (ret == kIOReturnSuccess)
    {
        printf("Read %d bytes\n", numBytes);
    }
    else
    {
        printf("Read failed (error: %x)\n", ret);
    }
}

/**
 * Shutting everything off before exiting the program.
 **/
void theEnd() {
    sendBits(guideLEDs, 0); // Turn off Guide Button LED's
    sendBits(patternOff, 0); //Turn off Stage Kit LED's
//    sleep(2);
    
    (*usbInterface)->USBInterfaceClose(usbInterface);
    (*usbDevice)->USBDeviceClose(usbDevice);
}

/**
 * Chooses which single LED to turn on. Translates user input from the Mac to a force feedback command that controls the LED's.
 **/
// Could cut back on repeated code. Change to be able to create multi-LED patterns.
void makePattern(char lr, int diode, const char color) {

    char message[] = {0x00, 0x08, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00};

    if (lr == 'L' || lr == 'l') {
        message[4] = color;
        if (diode == 1) {
            message[3] = left1;
        }
        if (diode == 2) {
            message[3] = left2;
        }
        if (diode == 4) {
            message[3] = left4;
        }
        if (diode == 8) {
            message[3] = left8;
        }
    }
    else if (lr == 'R' || lr == 'r') {
        message[4] = color;
        if (diode == 1) {
            message[3] = right1;
        }
        if (diode == 2) {
            message[3] = right2;
        }
        if (diode == 4) {
            message[3] = right4;
        }
        if (diode == 8) {
            message[3] = right8;
        }
    }
    else {
        printf("Bad command.\n");
        printf("LEDs off.\n");
    }
    
    sendBits(message, 1);
}

/**
 * Turns on all LED's.
 **/
void patternAllOn() {
    char message[] = {0x00, 0x08, 0x00, all, 0xff, 0x00, 0x00, 0x00};
    message[4] = blue;
    sendBits(message, 0);
    message[4] = green;
    sendBits(message, 0);
    message[4] = orange;
    sendBits(message, 0);
    message[4] = red;
    sendBits(message, 0);
}

/**
 * Turns on each set of LED colors after 1 second delays.
 **/
void patternAllOnWithDelay() {
    char message[] = {0x00, 0x08, 0x00, all, 0xff, 0x00, 0x00, 0x00};
    message[4] = blue;
    sendBits(message, 1);
    message[4] = green;
    sendBits(message, 1);
    message[4] = orange;
    sendBits(message, 1);
    message[4] = red;
    sendBits(message, 1);
}

/**
 * A test function to turn on an LED after a second delay.
 **/
void bigTest() {
    
    sendBits(guideLEDs, 1);
    for (int i = 1; i <= 8; i = i << 1) {
        makePattern('r', i, red);
//        readBits();
    }
    for (int i = 1; i <= 8; i = i << 1) {
        makePattern('l', i, red);
//        readBits();
    }
    sendBits(patternOff, 1);
    for (int i = 1; i <= 8; i = i << 1) {
        makePattern('l', i, orange);
//        readBits();
    }
    for (int i = 1; i <= 8; i = i << 1) {
        makePattern('r', i, orange);
//        readBits();
    }
    sendBits(patternOff, 1);
    for (int i = 1; i <= 8; i = i << 1) {
        makePattern('r', i, green);
//        readBits();
    }
    for (int i = 1; i <= 8; i = i << 1) {
        makePattern('l', i, green);
//        readBits();
    }
    sendBits(patternOff, 1);
    for (int i = 1; i <= 8; i = i << 1) {
        makePattern('l', i, blue);
//        readBits();
    }
    for (int i = 1; i <= 8; i = i << 1) {
        makePattern('r', i, blue);
//        readBits();
    }
    patternAllOnWithDelay();
    sleep(2);
    sendBits(patternOff, 1);
}

int main(int argc, const char * argv[]) {
    if (!findUSBDevice(idVendor, idProduct)) {
        return -1;
    }
    if (!activate()) {
        return -1;
    }
    if (!findOpenInterface()) {
        return -1;
    }
//    fromPoorMan();

//    bigTest();
//    patternAllOnWithDelay();
    sendBits(guideLEDs, 2);
//    readBits(); //figure out how to use this without freezing the driver
    
//printf("---Close---\n");
    theEnd();
    printf("Done.\n");
    return 0;
}
