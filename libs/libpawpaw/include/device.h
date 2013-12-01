#ifndef __DEVICE_H__
#define __DEVICE_H__

#define DEVSVC_REGISTER         21
#define DEVSVC_LISTEN_CHANGES   20
#define DEVSVC_GET_INFO         25

#define DEVSVC_SERVICE_NAME		"svc_dev"

enum svcdev_device_type {
    DEV_CONSOLE,
    DEV_TIMER,
    DEV_ETHERNET,
    DEV_AUDIO,
    DEV_VIDEO,
};

enum svcdev_device_loc {
    DEV_PLATFORM_DEVICE,
    USB,
    SATA,
};

#endif