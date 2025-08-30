#include <stdio.h>
#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <ucontext.h> // Required for ucontext_t and register definitions
#include <execinfo.h> // Required for backtrace
#include <pthread.h>  // Required for pthread_self()
#define LCD_ADDRESS 0x4c800000  
#define LCD_SIZE 0x100
#define RTC_ADDRESS 0x57000000  
#define RTC_SIZE 0x94
#define Timer_ADDRESS 0x51000000  
#define Timer_SIZE 0x40
#define DUMMY_ADDRESS 0x00000000  
#define DUMMY_SIZE 0x40
void entry_point(void);
void start_keyboard_listener(void);
void init_mouse(void);
void sigsegv_handler_continue(int sig, siginfo_t *si, void *ucontext) {
    char buffer[256];
    
    // Using write() is safe inside a signal handler.
    write(STDERR_FILENO, "\n--- WARNING: Segmentation Fault Caught ---\n", 43);

    // Get the current thread ID
    pthread_t tid = pthread_self();
    // pthread_t is an opaque type, printing it as a hex long is a common portable way
    snprintf(buffer, sizeof(buffer), "Caused by Thread ID (pthread_t): 0x%lx\n", (unsigned long)tid);
    write(STDERR_FILENO, buffer, strlen(buffer));

    // Print the address that caused the fault
    snprintf(buffer, sizeof(buffer), "Faulting Address: %p\n", si->si_addr);
    write(STDERR_FILENO, buffer, strlen(buffer));

    // Extract the Program Counter (PC) from the context for ARM
    ucontext_t *uc = (ucontext_t *)ucontext;
    void *pc = (void *)uc->uc_mcontext.arm_pc;

    snprintf(buffer, sizeof(buffer), "Instruction Pointer (PC) at fault: %p\n", pc);
    write(STDERR_FILENO, buffer, strlen(buffer));

    // --- Print the RAW Stack Trace (Hex Addresses) ---
    write(STDERR_FILENO, "Stack Trace (Raw Addresses):\n", 29);
    void *trace[32]; // Get up to 32 frames
    int trace_size = backtrace(trace, 32);
    
    for (int i = 0; i < trace_size; ++i) {
        snprintf(buffer, sizeof(buffer), "    [%d] %p\n", i, trace[i]);
        write(STDERR_FILENO, buffer, strlen(buffer));
    }

    // --- Modify the Program Counter to skip the faulting instruction ---
    // Assuming a 4-byte ARM instruction. This is a critical assumption.
    uc->uc_mcontext.arm_pc += 4;
    
    snprintf(buffer, sizeof(buffer), "Attempting to continue execution at new PC: %p\n", (void*)uc->uc_mcontext.arm_pc);
    write(STDERR_FILENO, buffer, strlen(buffer));
    write(STDERR_FILENO, "-------------------------------------------\n\n", 44);
}

int memallocate(size_t desired_address,size_t size){
void *addr = (void *)desired_address;

    void *mem = mmap(addr, size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    
    if (mem == MAP_FAILED) {
        perror("mmap failed");
        return -1;
    }
    
    if (mem != addr) {
        printf("Warning: Got different address (%p) than requested (%p)\n", mem, addr);
        return -2;
    } else {
        printf("Successfully allocated at %p\n", mem);
        return 0;
    }
}
int main(){
 struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = sigsegv_handler_continue;
    sa.sa_flags = SA_SIGINFO;

    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("sigaction failed");
        return 1;
    }
printf("Emulation Start!\n");
memallocate(LCD_ADDRESS,LCD_SIZE);
memallocate(RTC_ADDRESS,RTC_SIZE);
memallocate(Timer_ADDRESS,Timer_SIZE);
//memallocate(DUMMY_ADDRESS,DUMMY_SIZE);
start_keyboard_listener();
init_mouse();
entry_point();
return 0;
}
