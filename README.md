Papaya
======

Multi-server operating system, with some limited POSIX support. Uses the seL4 microkernel.

Originally written for the COMP9242: Advanced Operating Systems course at University of NSW in 2013.

Requirements
------------

This code was tested with a Freescale i.MX6 development board (specifically, the Sabre). It should work on any other ARM system that seL4 supports, in theory.

x86 support should also be possible to add, but you'll need to make changes to the virtual memory code, and the ELF loading code. This should be relatively minor.

You will also need a binary copy of seL4 with AEP binding support. From my understanding, the current public release of seL4 does not support this, but the open source release should have this.

Building
--------

### Prerequisites ###

* a cross-compiler: we're using gcc-arm (`apt-get install gcc-arm-linux-gnueabi`)
* make, GCC and libc development headers (`apt-get install build-essential`)
* ncurses development libraries (`apt-get install libncurses5-dev`)

You probably also want to install NFS and a TFTP server, but these are optional):
    apt-get install nfs-kernel-server nfs-common xinetd
    apt-get install tftpd-hpa tftp-hpa

If using NFS, make sure you enable the time service in xinetd, and setup an export (edit `/etc/exports`).

### Compiling ###

Once that's all installed, the following should be sufficient:

    make menuconfig
    make
    make reset # to reset the device, otherwise hit the reset button

You will need to ensure you've configured the host as a TFTP server, and that the device will connect to your machine to download the ELF image and boot from that.

Otherwise, you need to copy the resulting image from `bin` and somehow load that on your device. Usually you can just copy it to an SD card and specify the file from the bootloader.

Boot services, init, and swap
------------------------------

Edit `src/apps/sos/src/boot/boot.txt` as required. Alternatively, you can edit it directly in the CPIO archive.

Ensure you comment out applications you are not building with, otherwise the boot process won't be able to locate them. You can specify applications that are either in CPIO or over the NFS mount point, if you've defined one. 

You can specify a swap file, which must be on NFS. You can also specify what filesystems to mount, and where. Edit `src/apps/sos/src/boot/fstab.txt` as required.

Using
-----

SOS will start the boot app as given in `boot.txt`. By default, this is `sosh`, a simple shell.

From that, you can run programs over off the filesystem, which may be on CPIO and/or NFS, depending on how you've mounted the filesystems. There are a few built-in commands, as well -- use `help` to find out about these.

License
-------

Released under the MIT license.

seL4 is subject to its own license terms.
