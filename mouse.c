#include <pthread.h>
#include <unistd.h>
#include <linux/input.h>
#include <fcntl.h>
#include <stdio.h>
#include "ui.h"

// Global variables for cursor position
static int cursor_x = 0;
static int cursor_y = 0;
static pthread_mutex_t cursor_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function to draw cursor on LCD
void LCD_drawcursor(int x, int y) ;

// Thread function to handle mouse events// Thread function to handle mouse events
void* mouse_thread(void* arg) {
    int fd = open("/dev/input/mice", O_RDONLY);
    if (fd == -1) {
        perror("Unable to open mouse device");
        return NULL;
    }

    unsigned char data[3];
    int left_button = 0;
    int was_pressed = 0; // Move this variable declaration outside the loop
    
    while (1) {
        if (read(fd, data, sizeof(data)) == sizeof(data)) {
            left_button = data[0] & 0x1;
            int rel_x = (signed char)data[1];
            int rel_y = (signed char)data[2];
            
            pthread_mutex_lock(&cursor_mutex);
            int new_x = cursor_x + rel_x;
            int new_y = cursor_y - rel_y;
            
            // Constrain to screen boundaries
            if (new_x < 0) new_x = 0;
            if (new_x >= 320) new_x = 319;
            if (new_y < 0) new_y = 0;
            if (new_y >= 240) new_y = 239;
            
            cursor_x = new_x;
            cursor_y = new_y;
            pthread_mutex_unlock(&cursor_mutex);
            //printf("[MOUSE]x,y:%d,%d\n",cursor_x,cursor_y);
            // Draw cursor at new position
            LCD_drawcursor(cursor_x, cursor_y);
            
            // Create touch events if left button is pressed
            if (left_button) {
                UIMultipressEvent uime;
                uime.touch_x = cursor_x;
                uime.touch_y = cursor_y;
                
                if (!was_pressed) {
                //printf("[MOUSE]pressed left\n");
                    uime.type = UI_EVENT_TYPE_TOUCH_BEGIN;
                    was_pressed = 1;
                } else {
                    uime.type = UI_EVENT_TYPE_TOUCH_MOVE;
                }
                
                EnqueueEvent(uime);
            } else if (was_pressed) {
                UIMultipressEvent uime;
                uime.touch_x = cursor_x;
                uime.touch_y = cursor_y;
                uime.type = UI_EVENT_TYPE_TOUCH_END;
                EnqueueEvent(uime);
                was_pressed = 0;
            }
        }
        usleep(10000); // Sleep for 10ms
    }
    
    close(fd);
    return NULL;
}

// Initialize mouse handling
void init_mouse() {
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, mouse_thread, NULL);
    pthread_detach(thread_id);
}
