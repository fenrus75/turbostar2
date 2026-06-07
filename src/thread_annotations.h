#pragma once

#include <mutex>

// Enable thread safety attributes only with Clang.
#if defined(__clang__) && (!defined(SWIG))
#define THREAD_ANNOTATION_ATTRIBUTE__(x) __attribute__((x))
#else
#define THREAD_ANNOTATION_ATTRIBUTE__(x) // no-op
#endif

#define CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(capability(x))

#define SCOPED_CAPABILITY \
  THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)

#define GUARDED_BY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))

#define PT_GUARDED_BY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(pt_guarded_by(x))

#define ACQUIRED_BEFORE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquired_before(__VA_ARGS__))

#define ACQUIRED_AFTER(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquired_after(__VA_ARGS__))

#define REQUIRES(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(__VA_ARGS__))

#define REQUIRES_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(requires_shared_capability(__VA_ARGS__))

#define ACQUIRE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability(__VA_ARGS__))

#define ACQUIRE_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquire_shared_capability(__VA_ARGS__))

#define RELEASE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(release_capability(__VA_ARGS__))

#define RELEASE_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(release_shared_capability(__VA_ARGS__))

#define TRY_ACQUIRE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_capability(__VA_ARGS__))

#define TRY_ACQUIRE_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_shared_capability(__VA_ARGS__))

#define EXCLUDE_FROM_ANALYSIS \
  THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)

#define ASSERT_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(assert_capability(x))

#define ASSERT_SHARED_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(assert_shared_capability(x))

#define RETURN_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(lock_returned(x))

#if defined(__clang__)

class CAPABILITY("mutex") safe_mutex : public std::mutex {
public:
	using std::mutex::mutex;
};

class SCOPED_CAPABILITY safe_lock_guard {
public:
	safe_lock_guard(safe_mutex& m) ACQUIRE(m) : lock_(m) {}
	~safe_lock_guard() RELEASE() {}
private:
	std::lock_guard<std::mutex> lock_;
};

class SCOPED_CAPABILITY safe_unique_lock : public std::unique_lock<std::mutex> {
public:
	safe_unique_lock(safe_mutex& m) ACQUIRE(m) : std::unique_lock<std::mutex>(m) {}
	safe_unique_lock(safe_mutex& m, std::defer_lock_t t) : std::unique_lock<std::mutex>(m, t) {}
	~safe_unique_lock() RELEASE() {}

	void lock() ACQUIRE() { std::unique_lock<std::mutex>::lock(); }
	void unlock() RELEASE() { std::unique_lock<std::mutex>::unlock(); }
};

#else

using safe_mutex = std::mutex;
using safe_lock_guard = std::lock_guard<std::mutex>;
using safe_unique_lock = std::unique_lock<std::mutex>;

#endif
