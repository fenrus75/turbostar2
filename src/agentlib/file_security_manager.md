# File Security Manager (`file_security_manager`)

## Goals
The `file_security_manager` class provides path sandboxing, boundary validation, and ignore pattern filtering for all agent file tools. It ensures that autonomous agents cannot access, modify, or leak sensitive files outside the configured workspace or violate `.agentignore` policies.

## Architecture and Constraints
- **Allowed Roots & File Whitelisting**:
  - `add_allowed_root(path, access_type)`: Whitelists directory trees for `read` or `write` access.
  - `add_allowed_file(path, access_type)`: Whitelists specific single files.
- **Canonicalization & Symlink Resolution**:
  - All requested paths pass through filesystem canonicalization (`std::filesystem::canonical`) to prevent directory traversal attacks (`../../`) and symlink escapes.
- **Ignore Pattern Filtering**:
  - Automatically loads standard ignore patterns and `.agentignore` files. Rejects access to sensitive paths (e.g. `.git`, `.env`, private keys, secrets).
- **VFS Integration**:
  - Routes virtual URI schemes (`system://`, `tmp://`, `skills://`) to the attached `virtual_file_system` instance, validating URI schemes before allowing virtual reads or writes.

## Lessons Learned
- **Pre-Canonicalization Checks on New Files**: `std::filesystem::canonical` throws `ENOENT` if the target file does not yet exist (e.g. during a write operation). Canonicalizing the parent directory first allows new file creation within sandboxed boundaries without throwing false validation errors.
- **Hardcoded Secret Masks**: In addition to user-supplied `.agentignore` rules, hardcoding baseline protections for `.ssh`, `.gnupg`, and credentials files prevents prompt injection attacks from exfiltrating host secrets.
