import pty
import os
import subprocess
import time

master_fd, slave_fd = pty.openpty()
env = os.environ.copy()
env['TERM'] = 'xterm-256color'
proc = subprocess.Popen(['./build/turbostar'], stdin=slave_fd, stdout=slave_fd, stderr=slave_fd, env=env)
os.close(slave_fd)
time.sleep(0.5)

# press Alt-F
os.write(master_fd, b'\x1bf')
time.sleep(0.5)

# read output
import fcntl
import termios
fcntl.fcntl(master_fd, fcntl.F_SETFL, os.O_NONBLOCK)
out = b''
try:
    while True:
        out += os.read(master_fd, 1024)
except BlockingIOError:
    pass

with open('out.txt', 'wb') as f:
    f.write(out)

os.write(master_fd, b'\x03')
proc.wait()
os.close(master_fd)
