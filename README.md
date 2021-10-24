# linux-input-share-android
Share your computer's mouse and keyboard with your Android device via ADB (root needed). It uses [uinput](https://www.kernel.org/doc/html/v4.12/input/uinput.html), a Linux kernel module that makes it possible to emulate input devices from userspace.

## main.c
This file runs in your PC. 
### 1. Configure
Change the following values at the top of the file:

* line 18: `MOUSE_EVDEV_DEVICE`, change it to your mouse evdev device file
* line 19: `KEYB_EVDEV_DEVICE`, change it to your keyboard evdev device file
    > You can find the device file with this command:
    > ```
    > evtest
    > ```

* line 22: `MOUSE_X11_DEVICE_ID`, change it to your mouse X11 device ID
* line 23: `KEYBOARD_X11_DEVICE_ID`, change it to your keyboard X11 device ID
  > You can find the X11 device IDs with the following command:
  > ```
  > xinput list
  > ```

### 2. Compile
Compile it with
```
gcc -lpthread main.c -o main
```
and run it with
```
adb reverse tcp:1237 tcp:1237
sudo ./main
```

## receiver.c
This file runs in your device. It doesn't need to be changed. Just compile it for your device, and run it with root privileges. It will try to connect to the server (`main.c`) at `localhost:1237`.

---

## Switching input between phone and PC
You can switch via the numlock Enter key (keycode 96).
