## Crash Report

### Failed Assertion
```
Assertion: c != NULL
File: ../main.cpp
Line: 8
Function: void foo(char*)
```

### Info
```
Signal: 6
Executable: /home/arjan/git/turbotest9a/build/testcase
CrashCookie: run_1002
```

### Backtrace

| Frame | Address | Function | Location |
|---|---|---|---|
| 0 | `0x7fba45ea11dc` | `__pthread_kill_implementation` | nptl/nptl/pthread_kill.c:44:76 |
| 1 | `0x7fba45e4a842` | `__GI_raise` | sysdeps/posix/raise.c:27:6 |
| 2 | `0x7fba45e324b2` | `__GI_abort` | stdlib/stdlib/abort.c:85:3 |
| 3 | `0x7fba45e32424` | `__GI___assert_perror_fail` | assert/assert/assert-perr.c:31:1 |
| 4 | `0x7fba462f54e4` | `__assert_fail` | /home/arjan/git/turbostar2/src/crash_catcher/crash_catcher.c:420:1 |
| 5 | `0x55b73852e191` | `foo(char*)` | /home/arjan/git/turbotest3/main.cpp:9:8 |
| 6 | `0x55b73852e1ea` | `main` | /home/arjan/git/turbotest3/main.cpp:17:9 |


### Coredump Debugging
A coredump is available for this crash. You can launch a GDB session to debug this coredump by calling the 'agent_debug_coredump' tool:
```json
{
  "name": "agent_debug_coredump",
  "arguments": {
    "crash_id": "1474548"
  }
}
```