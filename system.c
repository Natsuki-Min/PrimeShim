#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <stdint.h>
#include <errno.h>
#include "ui.h"


const char *SDKLIB_GetCurrentPathA() {return "A:\\dummy";}

void* struc = NULL;
void* SDKLIB_ProgramIsRunningA(const char* pgm)
{
	printf("    program: %s\n",pgm);
	if (struc == NULL)
		struc=malloc(0x250);
	return struc;
}

int SDKLIB_WriteComDebugMsg(const char* fmt,...)
{
	printf("[DEBUG]");
	va_list args;
	va_start(args,fmt);
	vprintf(fmt,args);
	va_end(args);
}

int SDKLIB__LoadLibraryA(){
printf("Load Libarary stub\n");
return 0;
 	/*	auto ptr = __GET(char*, args->r1);
	auto sz = args->r2;
	auto img = GetPEImageByHandle(args->r0);
	auto vmpath = MapHostPathToVM(img->path.c_str());
	if (vmpath.size() >= sz)
		return sz;
	strcpy(ptr, vmpath.c_str());
	return vmpath.size();*/
 
 }
 
 // ====== File Operations ======

//-------------------------Essential---------------------------
void *SDKLIB_lcalloc(size_t n1,size_t n2){return calloc(n1,n2);}
void SDKLIB__lfree(void *ptr){free(ptr);}
void *SDKLIB_lmalloc(size_t size){return malloc(size);}
void *SDKLIB_lrealloc(void* ptr, size_t new_size){return realloc(ptr,new_size);}

#define MAX_CRITICAL_SECTIONS 256

// Use recursive mutexes to allow re-entrant locking
pthread_mutex_t g_cs[MAX_CRITICAL_SECTIONS] = {0};
pthread_mutexattr_t g_cs_attr;
int g_cs_initialized[MAX_CRITICAL_SECTIONS] = {0};
// Thread-local storage to track critical section nesting
__thread int thread_cs_stack[MAX_CRITICAL_SECTIONS] = {0};
__thread int thread_cs_stack_top = 0;

// Initialize mutex attributes once at program start
__attribute__((constructor)) 
static void init_mutex_attributes() {
    pthread_mutexattr_init(&g_cs_attr);
    pthread_mutexattr_settype(&g_cs_attr, PTHREAD_MUTEX_RECURSIVE);
}

uint32_t SDKLIB_OSInitCriticalSection(uint32_t id) {
    
    if (id >= 0 && id < MAX_CRITICAL_SECTIONS && !g_cs_initialized[id]) {
        pthread_mutex_init(&g_cs[id], &g_cs_attr);
        g_cs_initialized[id] = 1;
    }
    return id;
}

uint32_t SDKLIB_OSEnterCriticalSection(uint32_t id) {
    
    if (id >= 0 && id < MAX_CRITICAL_SECTIONS && g_cs_initialized[id]) {
        pthread_mutex_lock(&g_cs[id]);  // Recursive lock (safe for re-entry)
        thread_cs_stack[thread_cs_stack_top++] = id;  // Track locked mutex
    }
    return id;
}

uint32_t SDKLIB_OSLeaveCriticalSection(uint32_t id) {
    
    if (thread_cs_stack_top > 0 && thread_cs_stack[thread_cs_stack_top - 1] == id) {
        thread_cs_stack_top--;  // Pop from stack
        if (id >= 0 && id < MAX_CRITICAL_SECTIONS && g_cs_initialized[id]) {
            pthread_mutex_unlock(&g_cs[id]);
        }
    }
    return id;
}

uint32_t SDKLIB_OSSleep(uint32_t time) {
    struct timespec ts = {
        .tv_sec = time / 1000,
        .tv_nsec = (time % 1000) * 1000000
    };
    nanosleep(&ts, NULL);
    return time;
}
typedef struct {
	uint16_t Year;
	uint16_t Month;
	uint16_t DayOfWeek;
	uint16_t Day;
	uint16_t Hour;
	uint16_t Minute;
	uint16_t Second;
	uint16_t Milliseconds;
} SystemTime;

SystemTime* SDKLIB_GetSysTime(SystemTime* sysTime) {
    struct timeval tv;
    struct tm* parts;
    
    gettimeofday(&tv, NULL);
    parts = localtime(&tv.tv_sec);

    sysTime->Year = parts->tm_year + 1900;
    sysTime->Month = parts->tm_mon + 1;
    sysTime->DayOfWeek = parts->tm_wday;
    sysTime->Day = parts->tm_mday;
    sysTime->Hour = parts->tm_hour;
    sysTime->Minute = parts->tm_min;
    sysTime->Second = parts->tm_sec;
    sysTime->Milliseconds = (uint16_t)(tv.tv_usec / 1000);

    return sysTime;
}

pthread_t* SDKLIB_OSCreateThread(void* func,void* arg)
{
    pthread_t *thread = malloc(sizeof(pthread_t));
    if (thread == NULL) {
        return NULL;  // Memory allocation failed
    }

    int result = pthread_create(thread, NULL, func, arg);
    if (result != 0) {
        free(thread);  // Clean up if thread creation fails
        return NULL;
    }
   printf("[Thread] Create thread:%p\n",thread);
    return thread;
}


bool SDKLIB_OSSetThreadPriority(pthread_t *thread, short new_slot) {
    struct sched_param param;
    int policy;
    int min_prio, max_prio;
    
    // Get current scheduling policy
    if (pthread_getschedparam(*thread, &policy, &param) != 0) {
        return false;
    }
    
    // Determine priority range for the policy
    min_prio = sched_get_priority_min(policy);
    max_prio = sched_get_priority_max(policy);
    
    // Map the slot number to Linux priority range
    // Assuming Besta RTOS has 32 slots (0-31) like your example shows 8 and 18
    // We'll map slot 0 to min_prio and slot 31 to max_prio
    if(new_slot <0 || new_slot >63)
      printf("[ERROR] illegal slot value:%d",new_slot);
    int linux_prio = min_prio + (new_slot * (max_prio - min_prio)) / 63;
    
    param.sched_priority = linux_prio;
    
    if (pthread_setschedparam(*thread, policy, &param) != 0) {
        return false;
        
    }
    
    return true;
}
    
    
    
//---------------------event---------------------------


// Event structure using only pthread primitives
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool manual_reset;
    bool signaled;
} Event;

// Create a new event
Event* SDKLIB_OSCreateEvent(bool manual_reset, bool initial_state) {
    Event* ev = malloc(sizeof(Event));
    if (!ev) return NULL;
    
    pthread_mutex_init(&ev->mutex, NULL);
    pthread_cond_init(&ev->cond, NULL);
    ev->manual_reset = manual_reset;
    ev->signaled = initial_state;
    return ev;
}

// Set event to signaled state
void SDKLIB_OSSetEvent(Event* ev) {
    if (!ev) return;
    
    pthread_mutex_lock(&ev->mutex);
    ev->signaled = true;
    
    if (ev->manual_reset) {
        pthread_cond_broadcast(&ev->cond);
    } else {
        pthread_cond_signal(&ev->cond);
    }
    
    pthread_mutex_unlock(&ev->mutex);
}

// Reset event to non-signaled state
void ResetEvent(Event* ev) {
    if (!ev) return;
    
    pthread_mutex_lock(&ev->mutex);
    ev->signaled = false;
    pthread_mutex_unlock(&ev->mutex);
}

// Wait for event with timeout
bool SDKLIB_OSWaitForEvent(Event* ev, int timeout_ms) {
    if (!ev) return false;
    
    pthread_mutex_lock(&ev->mutex);
    
    // Handle already signaled state
    if (ev->signaled) {
        if (!ev->manual_reset) {
            ev->signaled = false;
        }
        pthread_mutex_unlock(&ev->mutex);
        return true;
    }
    
    // Handle immediate timeout
    if (timeout_ms == 0) {
        pthread_mutex_unlock(&ev->mutex);
        return false;
    }
    
    // Prepare timeout
    struct timespec ts;
    if (timeout_ms > 0) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
    }
    
    // Wait for signal
    int rc = 0;
    while (!ev->signaled && rc == 0) {
        if (timeout_ms < 0) {
            rc = pthread_cond_wait(&ev->cond, &ev->mutex);
        } else {
            rc = pthread_cond_timedwait(&ev->cond, &ev->mutex, &ts);
        }
    }
    
    // Check if event was signaled
    bool success = ev->signaled;
    if (success && !ev->manual_reset) {
        ev->signaled = false;
    }
    
    pthread_mutex_unlock(&ev->mutex);
    return success;
}

// Destroy event
void DestroyEvent(Event* ev) {
    if (!ev) return;
    
    pthread_cond_destroy(&ev->cond);
    pthread_mutex_destroy(&ev->mutex);
    free(ev);
}

static pthread_mutex_t events_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t events_cond = PTHREAD_COND_INITIALIZER;
static UIMultipressEvent events[8];
static int event_count = 0;
static bool consumer_waiting = false;

void EnqueueEvent(UIMultipressEvent uime) {
    pthread_mutex_lock(&events_mutex);
    
    if (event_count < 8) {
        events[event_count++] = uime;
        if (consumer_waiting) {
            pthread_cond_signal(&events_cond);
        }
    }
    // Else: queue full, event dropped
    
    pthread_mutex_unlock(&events_mutex);
}

uint32_t SDKLIB_GetEvent(ui_event_prime_s* event_prime) {
     
    memset(event_prime, 0, sizeof(*event_prime));
    event_prime->event_type = UI_EVENT_TYPE_TICK;

    pthread_mutex_lock(&events_mutex);

    if (event_count == 0) {
        consumer_waiting = true;
        pthread_cond_wait(&events_cond, &events_mutex);
        consumer_waiting = false;
    }

    if (event_count > 0) {
        // Check for key events
        for (int i = 0; i < event_count; i++) {
            if (events[i].type == UI_EVENT_TYPE_KEY || 
                events[i].type == UI_EVENT_TYPE_KEY_UP) {
                event_prime->event_type = UI_EVENT_TYPE_TICK_2;
                break;
            }
        }

        // Copy events
        event_prime->available_multipress_events = event_count;
        for (int i = 0; i < event_count; i++) {
            event_prime->multipress_events[i] = events[i];
        }
        event_count = 0;
    }
    
    pthread_mutex_unlock(&events_mutex);
    return 0;
}

//-----------------interrupt-------------
static volatile bool g_interrupt_running = false;
static pthread_t g_interrupt_thread_id;

// A struct to pass arguments to our new thread.
// This is cleaner than using multiple global variables.
typedef struct {
    void (*callback_fun)(void);
    // We could also pass a, b, or the interval here if needed.
} thread_args_t;


/**
 * @brief The main function for our interrupt-simulating thread.
 */
static void* interrupt_thread_routine(void* arg) {
    thread_args_t* args = (thread_args_t*)arg;
    void (*fun_to_call)(void) = args->callback_fun;

    // Free the heap-allocated arguments struct now that we've copied the data.
    free(args);

    // Define the sleep interval: 0.1 seconds = 100,000,000 nanoseconds.
    struct timespec sleep_duration = {0, 33333333L};//TODO:30FPS?

    // Loop until the main thread tells us to stop.
    while (g_interrupt_running) {
        // 1. Call the user's function.
        fun_to_call();

        // 2. Sleep for the specified interval.
        nanosleep(&sleep_duration, NULL);
    }

    printf("Interrupt thread is exiting.\n");
    return NULL;
}

/**
 * @brief Implementation of SDKLIB_InterruptInitialize.
 */
bool SDKLIB_InterruptInitialize(int a, int b, void* fun) {
    // Prevent initializing more than once.
    if (g_interrupt_running) {
        fprintf(stderr, "Error: Interrupt is already initialized.\n");
        return false;
    }
    
    // The user passes a generic void*, but we need a specific function pointer.
    // We cast it to the type we expect: a function that takes nothing and returns nothing.
    void (*callback)(void) = (void (*)(void))fun;
    if (callback == NULL) {
        fprintf(stderr, "Error: Provided function pointer is NULL.\n");
        return false;
    }

    // Allocate memory for thread arguments on the heap.
    thread_args_t* args = malloc(sizeof(thread_args_t));
    if (args == NULL) {
        perror("Failed to allocate memory for thread arguments");
        return false;
    }
    args->callback_fun = callback;
    
    // Set the flag to true *before* creating the thread.
    g_interrupt_running = true;

    // Create the new thread.
    if (pthread_create(&g_interrupt_thread_id, NULL, interrupt_thread_routine, args) != 0) {
        perror("Failed to create interrupt thread");
        g_interrupt_running = false; // Reset the flag on failure.
        free(args); // Clean up memory.
        return false;
    }

    // Detach the thread. This tells the OS that we won't be joining it later,
    // so its resources can be automatically reclaimed upon termination.
    // This is ideal for a long-running background task.
    pthread_detach(g_interrupt_thread_id);

    printf("Interrupt thread initialized successfully.\n");
    return true;
}

bool SDKLIB_InterruptDone(void) {return true;}

//------misc----------
uint32_t SDKLIB_BatteryLowCheck(){sched_yield();return 0;}



















uint32_t Stub_SDKVersion(){
return 0x10003;
} 
