# PrimeShim

A compatibility layer that converts BESTAOS API calls to Linux system calls, enabling HP Prime's firmware to run on other Linux ARM systems. The ultimate goal of this project is to replace the kernel of the HP Prime G1 from BESTAOS to Linux, addressing stability issues (at least it wont nuke the c driver and ruin the nand).

Thanks to Project Muteki(https://github.com/Project-Muteki/muteki)'s efforts for making this project possible.

## How to Use

The project is in a very early stage. The `data2.asm` and `text2.asm` files correspond to HP Prime OS version 15270 (2025-01-31). Use `make all` to compile the project. The resulting executable is `./build/arm.elf`.

- If you are on an x86/64 Arch Linux system, use the following command to run:
  ```bash
  sudo qemu-arm -L /usr/arm-linux-gnueabi ./build/arm.elf
  ```

- If you are on a Linux system with a desktop environment, switch to another TTY (using Ctrl+Alt+Fn) to run the executable, as it copies the screen buffer to `/dev/fb0` for display.

The application can handle mouse input; a black pixel on the screen indicates the cursor position.
