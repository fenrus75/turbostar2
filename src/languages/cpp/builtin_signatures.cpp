#include "languages/cpp/builtin_signatures.h"

#include <string_view>
#include <unordered_map>

namespace languages::cpp
{

std::optional<std::string_view> get_builtin_signature(std::string_view symbol_name) noexcept
{
	// Strip leading :: or std:: if present for flexible matching
	if (symbol_name.starts_with("::")) {
		symbol_name.remove_prefix(2);
	}

	// Lookup table for standard C and C++ library function signatures
	static const std::unordered_map<std::string_view, std::string_view> signatures = {
	    // C standard I/O (<stdio.h> / <cstdio>)
	    {"printf", "int printf(const char *format, ...)"},
	    {"fprintf", "int fprintf(FILE *stream, const char *format, ...)"},
	    {"sprintf", "int sprintf(char *str, const char *format, ...)"},
	    {"snprintf", "int snprintf(char *str, size_t size, const char *format, ...)"},
	    {"scanf", "int scanf(const char *format, ...)"},
	    {"fscanf", "int fscanf(FILE *stream, const char *format, ...)"},
	    {"sscanf", "int sscanf(const char *str, const char *format, ...)"},
	    {"puts", "int puts(const char *s)"},
	    {"fputs", "int fputs(const char *s, FILE *stream)"},
	    {"fopen", "FILE *fopen(const char *filename, const char *mode)"},
	    {"fclose", "int fclose(FILE *stream)"},
	    {"fread", "size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)"},
	    {"fwrite", "size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)"},
	    {"fflush", "int fflush(FILE *stream)"},
	    {"fseek", "int fseek(FILE *stream, long offset, int whence)"},
	    {"ftell", "long ftell(FILE *stream)"},
	    {"rewind", "void rewind(FILE *stream)"},
	    {"perror", "void perror(const char *s)"},

	    // C standard memory & general utilities (<stdlib.h> / <cstdlib>)
	    {"malloc", "void *malloc(size_t size)"},
	    {"calloc", "void *calloc(size_t nmemb, size_t size)"},
	    {"realloc", "void *realloc(void *ptr, size_t size)"},
	    {"free", "void free(void *ptr)"},
	    {"abort", "void abort()"},
	    {"exit", "void exit(int status)"},
	    {"atoi", "int atoi(const char *nptr)"},
	    {"atol", "long atol(const char *nptr)"},
	    {"strtol", "long strtol(const char *nptr, char **endptr, int base)"},
	    {"strtoul", "unsigned long strtoul(const char *nptr, char **endptr, int base)"},
	    {"getenv", "char *getenv(const char *name)"},
	    {"setenv", "int setenv(const char *name, const char *value, int overwrite)"},
	    {"unsetenv", "int unsetenv(const char *name)"},

	    // C standard string & memory (<string.h> / <cstring>)
	    {"memset", "void *memset(void *s, int c, size_t n)"},
	    {"memcpy", "void *memcpy(void *dest, const void *src, size_t n)"},
	    {"memmove", "void *memmove(void *dest, const void *src, size_t n)"},
	    {"memcmp", "int memcmp(const void *s1, const void *s2, size_t n)"},
	    {"memchr", "void *memchr(const void *s, int c, size_t n)"},
	    {"strlen", "size_t strlen(const char *s)"},
	    {"strcpy", "char *strcpy(char *dest, const char *src)"},
	    {"strncpy", "char *strncpy(char *dest, const char *src, size_t n)"},
	    {"strcmp", "int strcmp(const char *s1, const char *s2)"},
	    {"strncmp", "int strncmp(const char *s1, const char *s2, size_t n)"},
	    {"strcat", "char *strcat(char *dest, const char *src)"},
	    {"strncat", "char *strncat(char *dest, const char *src, size_t n)"},
	    {"strchr", "char *strchr(const char *s, int c)"},
	    {"strrchr", "char *strrchr(const char *s, int c)"},
	    {"strstr", "char *strstr(const char *haystack, const char *needle)"},
	    {"strdup", "char *strdup(const char *s)"},
	    {"strerror", "char *strerror(int errnum)"},

	    // POSIX file & system calls (<unistd.h>, <fcntl.h>, <sys/stat.h>)
	    {"open", "int open(const char *pathname, int flags, mode_t mode)"},
	    {"close", "int close(int fd)"},
	    {"read", "ssize_t read(int fd, void *buf, size_t count)"},
	    {"write", "ssize_t write(int fd, const void *buf, size_t count)"},
	    {"lseek", "off_t lseek(int fd, off_t offset, int whence)"},
	    {"unlink", "int unlink(const char *pathname)"},
	    {"stat", "int stat(const char *pathname, struct stat *statbuf)"},
	    {"fstat", "int fstat(int fd, struct stat *statbuf)"},
	    {"lstat", "int lstat(const char *pathname, struct stat *statbuf)"},
	    {"mkdir", "int mkdir(const char *pathname, mode_t mode)"},
	    {"rmdir", "int rmdir(const char *pathname)"},
	    {"chdir", "int chdir(const char *path)"},
	    {"getcwd", "char *getcwd(char *buf, size_t size)"},
	    {"access", "int access(const char *pathname, int mode)"},
	    {"fork", "pid_t fork()"},
	    {"pipe", "int pipe(int pipefd[2])"},
	    {"dup2", "int dup2(int oldfd, int newfd)"},

	    // C++ standard utility & algorithms (<algorithm>, <utility>, <memory>, <format>, <print>)
	    {"std::min", "const T &std::min(const T &a, const T &b)"},
	    {"min", "const T &std::min(const T &a, const T &b)"},
	    {"std::max", "const T &std::max(const T &a, const T &b)"},
	    {"max", "const T &std::max(const T &a, const T &b)"},
	    {"std::clamp", "const T &std::clamp(const T &v, const T &lo, const T &hi)"},
	    {"clamp", "const T &std::clamp(const T &v, const T &lo, const T &hi)"},
	    {"std::move", "T &&std::move(T &t)"},
	    {"move", "T &&std::move(T &t)"},
	    {"std::forward", "T &&std::forward(T &t)"},
	    {"forward", "T &&std::forward(T &t)"},
	    {"std::make_unique", "std::unique_ptr<T> std::make_unique(Args &&...args)"},
	    {"make_unique", "std::unique_ptr<T> std::make_unique(Args &&...args)"},
	    {"std::make_shared", "std::shared_ptr<T> std::make_shared(Args &&...args)"},
	    {"make_shared", "std::shared_ptr<T> std::make_shared(Args &&...args)"},
	    {"std::format", "std::string std::format(std::format_string<Args...> fmt, Args &&...args)"},
	    {"format", "std::string std::format(std::format_string<Args...> fmt, Args &&...args)"},
	    {"std::print", "void std::print(std::format_string<Args...> fmt, Args &&...args)"},
	    {"print", "void std::print(std::format_string<Args...> fmt, Args &&...args)"},
	    {"std::println", "void std::println(std::format_string<Args...> fmt, Args &&...args)"},
	    {"println", "void std::println(std::format_string<Args...> fmt, Args &&...args)"},
	};

	auto it = signatures.find(symbol_name);
	if (it != signatures.end()) {
		return it->second;
	}

	// Try stripping "std::" if provided
	if (symbol_name.starts_with("std::")) {
		auto stripped = symbol_name.substr(5);
		auto it_stripped = signatures.find(stripped);
		if (it_stripped != signatures.end()) {
			return it_stripped->second;
		}
	}

	return std::nullopt;
}

} // namespace languages::cpp
