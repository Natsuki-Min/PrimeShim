#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

//==============================================================================
// START: Color Format Configuration
//==============================================================================

// Define the available internal buffer formats
#define FORMAT_ARGB1555   1 // 16-bit per pixel
#define FORMAT_ARGB8888   2 // 32-bit per pixel (for RGB888 / ARGB8888)

// Set the buffer format here or define it during compilation
// (e.g., gcc -DBUFFER_COLOR_FORMAT=FORMAT_ARGB8888 ...)
#ifndef BUFFER_COLOR_FORMAT
#define BUFFER_COLOR_FORMAT FORMAT_ARGB8888 // Default to 16-bit ARGB1555
#endif

// Define the pixel data type and helper macros based on the chosen format
#if BUFFER_COLOR_FORMAT == FORMAT_ARGB1555
    #pragma message "Using 16-bit ARGB1555 internal buffer"
    typedef uint16_t buffer_pixel_t;
    #define MAKE_COLOR(r, g, b, a) \
        (buffer_pixel_t)((((a) > 127) ? (1 << 15) : 0) | \
                         (((r) >> 3) << 10) | \
                         (((g) >> 3) << 5)  | \
                         ((b) >> 3))

#elif BUFFER_COLOR_FORMAT == FORMAT_ARGB8888
    #pragma message "Using 32-bit ARGB8888 internal buffer"
    typedef uint32_t buffer_pixel_t;
    #define MAKE_COLOR(r, g, b, a) \
        (buffer_pixel_t)(((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

#else
    #error "Invalid or undefined BUFFER_COLOR_FORMAT."
#endif

//==============================================================================
// END: Color Format Configuration
//==============================================================================


// Define the LCD_MAGIC structure
struct LCD_MAGIC {
    uint16_t SomeVal;
    uint16_t x_res;
    uint16_t y_res;
    uint16_t pixel_bits;
    uint16_t bytes_per_line;
    uint16_t brightness_level;
    uint32_t unk1_0;
    uint32_t window1_bufferstart; // Virtual address of framebuffer
};

// Global variables for framebuffer management
static int fb_fd = -1;
static struct LCD_MAGIC LcdMagic;
static void *fb_mmap = NULL;

// Screen information
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static int screen_width = 0;
static int screen_height = 0;
static int screen_bpp = 0;
static int screen_stride = 0;

// Double-buffering variables (type and size depend on the chosen format)
static buffer_pixel_t display_buffer[320 * 240];
static buffer_pixel_t shadow_buffer[320 * 240];
static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t update_thread;
static int update_running = 0;

static int cursor_x = -1; // -1 means cursor is hidden
static int cursor_y = -1;
static pthread_mutex_t cursor_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function to initialize framebuffer and LCD_MAGIC struct
static int initialize_fb() {
    if (fb_fd != -1) return 0; // Already initialized

    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        perror("Failed to open /dev/fb0");
        return -1;
    }

    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) || ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo)) {
        perror("Error reading screen information");
        close(fb_fd);
        fb_fd = -1;
        return -1;
    }

    screen_width = vinfo.xres;
    screen_height = vinfo.yres;
    screen_bpp = vinfo.bits_per_pixel;
    screen_stride = finfo.line_length;
    printf("Screen: %dx%d, %dbpp, stride: %d bytes\n",
           screen_width, screen_height, screen_bpp, screen_stride);

    long screensize = screen_stride * screen_height;
    fb_mmap = mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mmap == MAP_FAILED) {
        perror("mmap failed");
        close(fb_fd);
        fb_fd = -1;
        return -1;
    }

    // Populate LCD_MAGIC struct based on our chosen buffer format
    LcdMagic.SomeVal = 0x5850;
    LcdMagic.x_res = 320;
    LcdMagic.y_res = 240;
    LcdMagic.pixel_bits = sizeof(buffer_pixel_t) * 8;
    LcdMagic.bytes_per_line = 320 * sizeof(buffer_pixel_t);
    LcdMagic.brightness_level = 2;
    LcdMagic.unk1_0 = 8;
    LcdMagic.window1_bufferstart = (uint32_t)(uintptr_t)shadow_buffer;

    return 0;
}

// Frame update thread function
static void* update_thread_func(void* arg) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 41666666; // ~24fps

    // <<< NEW: Create the cursor color once using our portable macro
    const buffer_pixel_t cursor_color = MAKE_COLOR(0, 0, 0, 255); // Black

    while (update_running) {
        pthread_mutex_lock(&buffer_mutex);
        memcpy(display_buffer, shadow_buffer, sizeof(display_buffer));
        pthread_mutex_unlock(&buffer_mutex);

        // <<< NEW SECTION: START >>>
        // Draw the cursor on top of the temporary display_buffer
        int local_cursor_x, local_cursor_y;

        // Get the cursor position safely
        pthread_mutex_lock(&cursor_mutex);
        local_cursor_x = cursor_x;
        local_cursor_y = cursor_y;
        pthread_mutex_unlock(&cursor_mutex);

        // Check if the cursor is within the buffer bounds
        if (local_cursor_y >= 0 && local_cursor_y < 240 &&
            local_cursor_x >= 0 && local_cursor_x < 320)
        {
            //printf("[LCD]x,y:%d,%d\n",cursor_x,cursor_y);
            // Calculate the position in the 1D array and draw the pixel
            display_buffer[local_cursor_y * 320 + local_cursor_x] = cursor_color;
        }
        // <<< NEW SECTION: END >>>

        if (fb_mmap) {
            int x, y;
            uint8_t *fb_ptr = (uint8_t *)fb_mmap;

            for (y = 0; y < 240 && y < screen_height; y++) {
                // <<< MODIFIED: The source is now display_buffer, which includes the cursor
                buffer_pixel_t *src_row = display_buffer + (y * 320);
                uint8_t *dest_row = fb_ptr + (y * screen_stride);

                // --- OPTIMIZATION: If source and dest formats match, copy the whole row ---
                if (sizeof(buffer_pixel_t) * 8 == screen_bpp) {
                    memcpy(dest_row, src_row, 320 * sizeof(buffer_pixel_t));
                    continue;
                }

                // --- If formats differ, convert pixel by pixel ---
                for (x = 0; x < 320 && x < screen_width; x++) {
                    uint8_t r, g, b, a;

                    // Step 1: Deconstruct the source pixel into 8-bit R,G,B,A components
#if BUFFER_COLOR_FORMAT == FORMAT_ARGB1555
                    uint16_t src_pixel = src_row[x];
                    r = ((src_pixel >> 10) & 0x1F) << 3;
                    g = ((src_pixel >> 5)  & 0x1F) << 3;
                    b = (src_pixel         & 0x1F) << 3;
                    a = (src_pixel >> 15) ? 0xFF : 0x00;
#elif BUFFER_COLOR_FORMAT == FORMAT_ARGB8888
                    uint32_t src_pixel = src_row[x];
                    a = (src_pixel >> 24) & 0xFF;
                    r = (src_pixel >> 16) & 0xFF;
                    g = (src_pixel >> 8)  & 0xFF;
                    b = (src_pixel)       & 0xFF;
#endif

                    // Step 2: Construct the destination pixel based on screen bpp
                    if (screen_bpp == 32) {
                        ((uint32_t*)dest_row)[x] = (a << 24) | (r << 16) | (g << 8) | b;
                    } else if (screen_bpp == 24) {
                        dest_row[x*3 + 0] = b; // Common BGR order for FB
                        dest_row[x*3 + 1] = g;
                        dest_row[x*3 + 2] = r;
                    } else if (screen_bpp == 16) {
                        // Assuming target is RGB565, which is most common for 16bpp FBs
                        ((uint16_t*)dest_row)[x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                    }
                }
            }
        }
        nanosleep(&ts, NULL);
    }
    return NULL;
}

// Turns on LCD (initializes framebuffer and starts update thread)
void SDKLIB_LCDOn() {
    if (fb_fd != -1) return;
    if (initialize_fb() == 0) {
        update_running = 1;
        pthread_create(&update_thread, NULL, update_thread_func, NULL);
    }
}

// Clears the screen to black
void SDKLIB_ClearScreen() {
    printf("[LCD]Clear Screen!\n");
    pthread_mutex_lock(&buffer_mutex);
    memset(shadow_buffer, 0, sizeof(shadow_buffer));
    pthread_mutex_unlock(&buffer_mutex);
}

// Returns initialized LCD_MAGIC structure
struct LCD_MAGIC** SDKLIB_GetActiveLCD() {
    printf("[LCD]Got LCD %p (pixel_bits=%d)\n", &LcdMagic, LcdMagic.pixel_bits);
    if (fb_fd == -1) {
        SDKLIB_LCDOn();
    }
    static struct LCD_MAGIC* pLcdMagic = &LcdMagic;
    return &pLcdMagic;
}

// Function to stop update thread and clean up
void SDKLIB_LCDOff() {
    if (!update_running) return;
    update_running = 0;
    pthread_join(update_thread, NULL);
    if (fb_mmap) {
        munmap(fb_mmap, screen_stride * screen_height);
        fb_mmap = NULL;
    }
    if (fb_fd != -1) {
        close(fb_fd);
        fb_fd = -1;
    }
}
void LCD_drawcursor(int x, int y) {
    pthread_mutex_lock(&cursor_mutex);
    cursor_x = x;
    cursor_y = y;
    
    pthread_mutex_unlock(&cursor_mutex);
}
