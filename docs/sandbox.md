# Sandbox goals and systemd-running implication

## Goals

- max security options, e.g. try to prevent escapes etc
- everything is read only except the project directory and below
- hide homedirectories as much as possible, only allow the current one IF the project is inside it
- custom /tmp tmpfs, writeable

- coredumps work and we can find them -- may need a directory in /var as writeable

## systemd-run parameters to implement this

```bash
systemd-run --pty --pipe --wait \
  -p ProtectSystem=strict \
  -p ReadWritePaths=<project directory> \"
  -p ProtectHome=tmpfs \
  -p "BindPaths=$HOME" \     <-- only if the project directory includes the homedir?
  -p "WorkingDirectory=<project directory>" \
  --property=NoNewPrivileges=true \
  --property=PrivateTmp=true \
  --property=PrivateDevices=true \
  --property=ProtectKernelTunables=true \
  --property=ProtectKernelModules=true \
  --property=MemoryDenyWriteExecute=true \
  --property=ProtectControlGroups=true \  
    <command>
```