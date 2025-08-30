#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "ui.h"

// Forward declaration, assuming this is defined elsewhere.
void EnqueueEvent(UIMultipressEvent uime);

static pthread_t keyboard_thread;
static int keyboard_thread_running = 0;
static int keyboard_fd = -1;

// Parse /proc/bus/input/devices to find a suitable keyboard device.
// This version is more robust as it processes one device block at a time.
static int open_keyboard_device(void)
{
    FILE *fp = fopen("/proc/bus/input/devices", "r");
    if (!fp) {
        perror("Failed to open /proc/bus/input/devices");
        return 0;
    }

    char line[256];
    char handlers[256] = {0};
    int has_ev_key = 0;

    // Read the file line by line
    while (fgets(line, sizeof(line), fp)) {
        // A blank line signifies the end of a device's information block
        if (line[0] == '\n') {
            // Check if the device we just finished reading is a keyboard
            if (has_ev_key && strstr(handlers, "kbd")) {
                // It's a keyboard. Now find its event handler.
                char *event_handler = strstr(handlers, "event");
                if (event_handler) {
                    char event_path[256];
                    int event_num;
                    if (sscanf(event_handler, "event%d", &event_num) == 1) {
                        snprintf(event_path, sizeof(event_path), "/dev/input/event%d", event_num);
                        
                        // Attempt to open the device file
                        keyboard_fd = open(event_path, O_RDONLY | O_NONBLOCK);
                        if (keyboard_fd != -1) {
                            printf("Successfully opened keyboard: %s\n", event_path);
                            fclose(fp);
                            return 1; // Success!
                        }
                    }
                }
            }

            // Reset state for the next device block
            handlers[0] = '\0';
            has_ev_key = 0;
        } 
        else if (strncmp(line, "H: Handlers=", 12) == 0) {
            // Copy the handlers line
            strncpy(handlers, line + 12, sizeof(handlers) - 1);
            handlers[sizeof(handlers) - 1] = '\0';
        } 
        else if (strncmp(line, "B: EV=", 6) == 0) {
            // Check for the EV_KEY bit
            if (strstr(line, "EV=120013") || strstr(line, "EV=100013")) { // Common bitmasks for keyboards
                 has_ev_key = 1;
            }
        }
    }

    // If we reach here, no suitable keyboard was found
    fprintf(stderr, "No suitable keyboard device found.\n");
    fclose(fp);
    return 0;
}

// Function to map Linux keycodes to device keycodes
static int map_key_to_device(int linux_keycode)
{
    // ... (Your mapping function is fine, no changes needed here) ...
    // Map Linux keycodes to device keycodes
    // This is just an example - you'll need to adjust based on your actual key mappings
    switch(linux_keycode)
     {
        // === 标准功能键 ===
        case KEY_ESC:        return 0x01; // Esc
        case KEY_LEFT:       return 0x02; // 左
        case KEY_UP:         return 0x03; // 上
        case KEY_RIGHT:      return 0x04; // 右
        case KEY_DOWN:       return 0x05; // 下
        case KEY_BACKSPACE:  return 0x0C; // 退格
        case KEY_ENTER:      return 0x0D; // 回车
        case KEY_SPACE:      return 0x20; // 空格
        case KEY_LEFTSHIFT:  return 0x8B; // Shift
        case KEY_RIGHTSHIFT: return 0x8B; // Shift

        // === 数字与符号键 ===
        case KEY_0:          return 0x30; // 0
        case KEY_1:          return 0x59; // 1
        case KEY_2:          return 0x5A; // 2
        case KEY_3:          return 0x33; // 3
        case KEY_4:          return 0x55; // 4
        case KEY_5:          return 0x56; // 5
        case KEY_6:          return 0x57; // 6
        case KEY_7:          return 0x51; // 7
        case KEY_8:          return 0x52; // 8
        case KEY_9:          return 0x53; // 9
        case KEY_X:          return 0x44; // X
        case KEY_COMMA:      return 0x4F; // ,
        case KEY_SLASH:      return 0x54; // /
        case KEY_KPASTERISK: return 0x58; // *
        case KEY_DOT:        return 0xB8; // .
        case KEY_MINUS:      return 0xB7; // +
        case KEY_EQUAL:      return 0xB9; // - (Note: This mapping might need adjustment)

        // === 特殊功能键 (映射自 QWERTY 布局) ===
        case KEY_Q:          return 0x41; // 变量
        case KEY_W:          return 0x42; // 工具箱
        case KEY_E:          return 0x43; // 数学模板
        case KEY_R:          return 0x45; // a b/c
        case KEY_T:          return 0x46; // ^
        case KEY_Y:          return 0x47; // SIN
        case KEY_U:          return 0x48; // COS
        case KEY_I:          return 0x49; // TAN
        case KEY_O:          return 0x4A; // LN
        case KEY_P:          return 0x4B; // LOG
        case KEY_A:          return 0x4C; // ^2
        case KEY_S:          return 0x4D; // +/-
        case KEY_D:          return 0x4E; // ()
        case KEY_F:          return 0x50; // 1e
        case KEY_G:          return 0x83; // ON
        case KEY_H:          return 0x91; // 符号视图
        case KEY_J:          return 0x93; // 消息
        case KEY_K:          return 0xB2; // 绘图
        case KEY_L:          return 0xB3; // 数值
        case KEY_Z:          return 0xB4; // 视图
        case KEY_C:          return 0xB5; // CAS
        case KEY_V:          return 0xB6; // Alpha
        case KEY_N:          return 0x95; // 帮助
        case KEY_M:          return 0xB1; // 应用
        case KEY_B:          return 0xE047; // 应用

        default: return -1; // 未映射的键
    }
}

static void* keyboard_listener_thread(void* arg)
{
    struct input_event ev;
    fd_set fds;
    
    // This static variable will store the correct event size once detected.
    // -1: unknown, 16: 32-bit struct, 24: 64-bit struct.
    static ssize_t event_size_to_read = -1;

    // Buffer large enough for the 64-bit event struct (24 bytes).
    char read_buf[24]; 

    while(keyboard_thread_running)
    {
        if(keyboard_fd != -1)
        {
            FD_ZERO(&fds);
            FD_SET(keyboard_fd, &fds);
            
            int ret = select(keyboard_fd + 1, &fds, NULL, NULL, NULL);
            
            if(ret > 0 && FD_ISSET(keyboard_fd, &fds))
            {
                ssize_t n;

                // --- DYNAMIC EVENT SIZE DETECTION ---
                if (event_size_to_read == -1) {
                    // We haven't determined the kernel's event size yet.
                    // Let's probe to find out.
                    // The mismatch occurs when a 32-bit app (sizeof=16) runs on a 64-bit kernel (expects 24).
                    // So, we optimistically try reading the size of the *other* architecture first.
                    ssize_t native_size = sizeof(struct input_event);
                    ssize_t other_size = (native_size == 16) ? 24 : 16;
                    
                    n = read(keyboard_fd, read_buf, other_size);
                    if (n == other_size) {
                        event_size_to_read = other_size;
                        printf("Detected kernel input_event size of %zd bytes (app is %zd bytes).\n", event_size_to_read, native_size);
                    } else if (n == -1 && errno == EINVAL) {
                        // The kernel rejected the 'other' size, so it must want our native size.
                        n = read(keyboard_fd, read_buf, native_size);
                        if (n == native_size) {
                           event_size_to_read = native_size;
                           printf("Detected kernel input_event size matches app size (%zd bytes).\n", event_size_to_read);
                        } else {
                            // This is a more serious, unrecoverable error.
                            perror("read error after EINVAL fallback");
                            close(keyboard_fd);
                            keyboard_fd = -1;
                            continue;
                        }
                    } else {
                        // Handle other read errors or unexpected read sizes during probing.
                        if (n == -1) perror("read error during probe");
                        else fprintf(stderr, "Unexpected read size %zd during probe.\n", n);
                        close(keyboard_fd);
                        keyboard_fd = -1;
                        continue;
                    }
                } else {
                    // We already know the size, so just read it.
                    n = read(keyboard_fd, read_buf, event_size_to_read);
                }
                
                // --- PROCESS THE READ DATA ---
                
                if (n == (ssize_t)-1) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        perror("read error");
                        close(keyboard_fd);
                        keyboard_fd = -1;
                        event_size_to_read = -1; // Reset detection on reconnect
                    }
                    continue;
                }

                if (n == 0) {
                     printf("Keyboard disconnected.\n");
                     close(keyboard_fd);
                     keyboard_fd = -1;
                     event_size_to_read = -1; // Reset detection on reconnect
                     continue;
                }

                if (n != event_size_to_read) {
                    fprintf(stderr, "Partial read: got %zd bytes, expected %zd bytes. Discarding.\n", n, event_size_to_read);
                    continue;
                }

                // --- UNPACK EVENT DATA FROM BUFFER ---
                // Manually unpack the struct fields if the read size doesn't match our native struct size.
                // This handles the 32-bit app on 64-bit kernel case.
                if (event_size_to_read != sizeof(struct input_event) && event_size_to_read == 24) {
                    // We are a 32-bit app that read a 64-bit (24-byte) structure.
                    // The layout is: timeval (16 bytes), type (2), code (2), value (4).
                    // So we copy from the correct offsets in the buffer.
                    memcpy(&ev.type,  read_buf + 16, sizeof(ev.type));
                    memcpy(&ev.code,  read_buf + 18, sizeof(ev.code));
                    memcpy(&ev.value, read_buf + 20, sizeof(ev.value));
                } else {
                    // Sizes match, we can just copy the whole structure.
                    memcpy(&ev, read_buf, sizeof(ev));
                }
                
                // --- PROCESS THE UNPACKED EVENT ---
                if(ev.type == EV_KEY)
                {
                    int device_keycode = map_key_to_device(ev.code);
                    if(device_keycode != -1)
                    {
                        UIMultipressEvent event;
                        memset(&event, 0, sizeof(event));
                        
                        if(ev.value == 1) // Key pressed
                        {
                            event.type = UI_EVENT_TYPE_KEY;
                            //printf("Key PRESS: Linux=0x%x, Device=0x%x\n", ev.code, device_keycode);
                        }
                        else if(ev.value == 0) // Key released
                        {
                            event.type = UI_EVENT_TYPE_KEY_UP;
                            //printf("Key RELEASE: Linux=0x%x, Device=0x%x\n", ev.code, device_keycode);
                        }
                        else { continue; }
                        
                        event.key_code0 = device_keycode;
                        EnqueueEvent(event);
                    }
                }
            }
            else if(ret == -1)
            {
                perror("select error");
                close(keyboard_fd);
                keyboard_fd = -1;
                event_size_to_read = -1; // Reset detection
            }
        }
        else
        {
            if(!open_keyboard_device()) {
                sleep(2);
            }
        }
    }
    
    if(keyboard_fd != -1) {
        close(keyboard_fd);
        keyboard_fd = -1;
    }
    
    return NULL;
}

// Start the keyboard listener thread
void start_keyboard_listener(void)
{
    // Add a check at startup to inform the user
    printf("Info: Application compiled with sizeof(struct input_event) = %zu\n", sizeof(struct input_event));
    
    keyboard_thread_running = 1;
    
    // Create thread
    pthread_create(&keyboard_thread, NULL, keyboard_listener_thread, NULL);
}

// Stop the keyboard listener thread
void stop_keyboard_listener(void)
{
    if(keyboard_thread_running) {
        keyboard_thread_running = 0;
        pthread_join(keyboard_thread, NULL);
    }
}
