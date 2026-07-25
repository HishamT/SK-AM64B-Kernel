# SK-AM64B Linux Character Device Driver

This project is a simple Linux kernel character device driver developed on the TI SK-AM64B board.

The driver creates a `/dev/hello` device that allows a user-space program to communicate with kernel-space code using standard Linux file operations such as `open()`, `read()`, `write()`, `close()`, and `ioctl()`.

## Overview

The goal of this project is to demonstrate the basic structure of a Linux character device driver.

The driver supports:

* Loading and unloading as a kernel module
* Automatic creation of `/dev/hello`
* Reading data from the driver
* Writing data to the driver
* Clearing the stored buffer with an `ioctl()` command
* Protecting shared driver state with a mutex

## Files

```text
.
├── Makefile
├── hello_char.c
├── hello_ioctl.h
└── helloctl.c
```

## File Descriptions

### `hello_char.c`

This is the Linux kernel module.

It registers a character device and implements the device operations:

```c
.open
.read
.write
.unlocked_ioctl
.release
```

The driver stores data written from user space in a kernel buffer. When the device is read, the stored data is copied back to user space.

The driver also uses a mutex to protect shared data from concurrent access.

### `hello_ioctl.h`

This header contains the shared ioctl command definition used by both the kernel module and the user-space test program.

```c
#define HELLO_IOCTL_CLEAR _IO('h', 1)
```

Keeping the ioctl definition in a shared header ensures that the kernel driver and user-space program agree on the same command value.

### `helloctl.c`

This is a small user-space program that opens `/dev/hello` and sends the `HELLO_IOCTL_CLEAR` command to the driver.

It is used to clear the driver's internal buffer.

### `Makefile`

This builds the kernel module using the kernel build system.

## Features

### Character Device Registration

The driver uses the modern character device registration flow:

```c
alloc_chrdev_region()
cdev_init()
cdev_add()
class_create()
device_create()
```

This allows the driver to create `/dev/hello` automatically when the module is loaded.

### Read Support

Reading from `/dev/hello` copies data from the kernel buffer into a user-space buffer using:

```c
copy_to_user()
```

Example:

```bash
cat /dev/hello
```

### Write Support

Writing to `/dev/hello` copies data from a user-space buffer into the kernel buffer using:

```c
copy_from_user()
```

Example:

```bash
echo "hello kernel" > /dev/hello
```

### ioctl Support

The driver supports an ioctl command that clears the internal buffer.

Example:

```bash
./helloctl
```

### Mutex Protection

The driver protects its shared buffer with a mutex:

```c
static DEFINE_MUTEX(hello_lock);
```

This prevents simultaneous reads, writes, or ioctl operations from corrupting shared state.

## Example Usage

Build the kernel module:

```bash
make clean
make
```

Load the module:

```bash
sudo insmod hello_char.ko
```

Check that the device was created:

```bash
ls -l /dev/hello
```

If needed, adjust permissions:

```bash
sudo chmod 666 /dev/hello
```

Write data to the device:

```bash
echo "hello from user space" > /dev/hello
```

Read the data back:

```bash
cat /dev/hello
```

Expected output:

```text
hello from user space
```

Build the ioctl test program:

```bash
gcc helloctl.c -o helloctl
```

Clear the buffer:

```bash
./helloctl
```

Read again:

```bash
cat /dev/hello
```

Expected output:

```text
```

The blank output means the buffer was cleared.

Check kernel log messages:

```bash
dmesg | tail -n 20
```

Unload the module:

```bash
sudo rmmod hello_char
```

## Concepts Demonstrated

This project demonstrates several important Linux kernel programming concepts:

* Loadable kernel modules
* Character device drivers
* Major and minor device numbers
* `struct file_operations`
* `read()` and `write()` callbacks
* User-space to kernel-space copying
* `copy_to_user()`
* `copy_from_user()`
* `ioctl()` commands
* Shared user/kernel ioctl headers
* `struct cdev`
* Automatic `/dev` device creation
* Kernel mutexes
* Kernel logging with `pr_info()` and `pr_err()`

## System

This project was developed on an SK-AM64B running Linux:

```text
Linux 6.12.57-vendor-k3
AArch64 / ARM64
```

## Notes

This is a learning project. It does not control real hardware yet.

The next planned steps are:

* Replace the fixed static buffer with dynamically allocated kernel memory using `kmalloc()` and `kfree()`
* Add a proper udev rule for device permissions
* Add sysfs attributes
* Convert the driver into a platform driver
* Connect the driver to real hardware such as GPIO or SPI
