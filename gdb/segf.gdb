# segf.gdb
#
# Connect to QEMU
target remote localhost:1234

# Set architecture
set architecture arm

# Load symbols (adjust path as needed)
file ../build/arm.elf

# Check if program is already terminated
if $_isvoid($_proxy)
  echo Program has already terminated. Try restarting QEMU with -S flag.\n
  quit
end

# Handle segmentation faults
handle SIGSEGV stop noprint

define print_debug_info
  echo \n=== SEGFAULT DETECTED ===\n
  echo Fault address: $pc\n
  echo \n=== REGISTER VALUES ===\n
  info registers all
  echo \n=== DISASSEMBLY AROUND $pc ===\n
  disassemble /r $pc-32, $pc+32
  echo \n=== STACK TRACE ===\n
  backtrace
  echo \n=== MEMORY MAP ===\n
  info proc mappings
  echo \n
end

# Set a hook to catch segfaults
python
def stop_handler(event):
    if hasattr(event, 'stop_signal') and event.stop_signal == 'SIGSEGV':
        gdb.execute('print_debug_info')
        
gdb.events.stop.connect(stop_handler)
end
break _start
continue
# Continue execution
continue
