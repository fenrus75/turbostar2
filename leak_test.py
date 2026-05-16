import pty
import os
import subprocess
import time
import select

# Start turbostar with valgrind
cmd = ["valgrind", "--leak-check=full", "--show-leak-kinds=definite,indirect,possible", "./build/turbostar"]
master_fd, slave_fd = pty.openpty()

proc = subprocess.Popen(
    cmd,
    stdin=slave_fd,
    stdout=slave_fd,
    stderr=subprocess.PIPE, # Capture valgrind output
    text=True
)
os.close(slave_fd)

# Wait for initialization
time.sleep(2)
print("Sending quit command")
# Send Quit: Ctrl-K then 'q'
os.write(master_fd, b'\x0b' + b'q')

# Wait for exit
try:
    proc.wait(timeout=10)
    print("Process exited")
except subprocess.TimeoutExpired:
    print("Timed out, killing process")
    proc.kill()
    _, stderr = proc.communicate()
    print(stderr)
    exit(1)

# Read stderr from valgrind
_, stderr = proc.communicate()
print(stderr)
