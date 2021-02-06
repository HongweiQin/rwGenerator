# rwGenerator

In short, rwGenerator is a set of tools to generate I/O requests to a block device.

When learning Linux or debugging a block device driver, users usually ask themselves:

***How does the device act if I submit a read/write request with size X to offset Y?***

If you use `dd` or `fio` to submit requests, you might end up with unexpected I/Os. E.g., if you submit a 4KiB write request to device offset 0 using fio, the block device driver may receive several read requests before and after the 4KiB write request. If you're not aware of this and your driver happens to have some bugs in the read path, you might have some hard time debugging. Using the tools in `kernelModule` or `userspaceApp` can submit I/Os to the target block device within your specification. If you only want to issue one request, they won't submit two.

I'll keep adding more handy tools to this repo.

## Kernel Module

Userspace programs have different I/O submission path compared with file systems. rwGenerator has a kernel module to allow users to submit I/Os just as the way that file systems do. Read the `README.md` in the `kernelModule` directory to learn how to use it.

## userspaceApp

Using a kernel module is sometimes a no-op for some users. `userspaceApp` is more flexible.

Just `cd` to the dir, `make`, and then run the binary. The program will tell you how to use.

## f2fsAtomicWrite

You can use `f2fsAtomicWrite` in the `userspaceApp` to submit F2FS specific commands.

## fioscripts

This folder contains some helping scripts for `fio` so that users don't need to type the long parameters.

## QIOG

This is a kernel module to test the multi-thread maximum bandwidth of an Open-Channel SSD.
