# Read-Only Memory-Mapped File (`mapped_file`)

## Overview & Goals
`mapped_file` (`src/mapped_file.{h,cpp}`) provides a reusable, move-only RAII wrapper for reading regular files via `mmap(PROT_READ, MAP_PRIVATE)` with a transparent fallback to heap allocation (`std::vector<uint8_t>`) and `::read` when `mmap` is unavailable or fails. It exposes both `std::span<const uint8_t>` (`bytes()`) and `std::string_view` (`string_view()`) views so binary tools (`x86_disassemble`, `elf_list_sections`, `elf_list_symbols`, `hexdump`, `hexinspect`) and text tools can inspect large files without copying the entire file into heap memory.

## Key Responsibilities
- **Descriptor-Level Regular File Validation**: Opens the file with `O_RDONLY | O_CLOEXEC` and checks `S_ISREG(st.st_mode)` via `fstat(fd, &st)` on the open file descriptor to prevent TOCTOU races and reject directories, FIFOs, sockets, and device nodes.
- **Immediate File Descriptor Release**: Closes `fd` immediately after `mmap` succeeds so active `mapped_file` instances do not consume process file descriptor table slots.
- **Zero-Byte File Support**: Handles `st.st_size == 0` without calling `mmap` (which returns `EINVAL` on size 0 in POSIX), setting `is_open() == true`, `empty() == true`, and `size() == 0`.
- **Transparent Heap Fallback**: If `mmap` returns `MAP_FAILED` (e.g., on FUSE/virtual filesystems or under `vm.max_map_count` pressure), falls back to reading up to 256MB into an internal `std::vector<uint8_t>` so callers using `bytes()` or `string_view()` are completely unaffected.
- **Compile-Time Dangling View Prevention**: Marks `bytes()`, `string_view()`, and the implicit conversion operators as `const &` and deletes the `const &&` overloads so binding a `std::span` or `std::string_view` to a temporary `mapped_file` is rejected at compile time.

## Lessons Learned
- **Eliminating Arbitrary 50MB Tool Caps**: Binary inspection tools previously imposed a 50MB file size limit because they allocated `std::vector<uint8_t>(file_size)` on the heap and eagerly read the entire file. Mapping with `PROT_READ | MAP_PRIVATE` lets the OS kernel fault in only the pages actually touched (such as `.symtab`, `.strtab`, and the target `.text` function), supporting multi-hundred-megabyte unstripped C++ debug binaries with near-zero RSS overhead.
