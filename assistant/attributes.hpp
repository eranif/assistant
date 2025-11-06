#pragma once

#if defined(__clang__)
#define THREAD_ANNOTATION_ATTRIBUTE__(x) __attribute__((x))
#else
#define THREAD_ANNOTATION_ATTRIBUTE__(x)
#endif

// Informs the analysis tool that a class is a RAII-style lock [1.1.5].
#define SCOPED_CAPABILITY THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)

// Documents that a function requires a capability to be held by the caller
// [1.1.6].
#define REQUIRES(x) THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(x))

// Documents that a function acquires a capability [1.4.3].
#define ACQUIRE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability(__VA_ARGS__))

// Documents that a function releases a capability [1.4.3].
#define RELEASE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(release_capability(__VA_ARGS__))

// Documents that a member is guarded by a specific capability [1.1.6].
#define GUARDED_BY(x) THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))

// Documents that the calling thread must already hold an exclusive lock on the
// specified mutex
#define CALLER_MUST_LOCK(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(exclusive_locks_required(x))

// Documents the locks that cannot be held by callers of this function, as they
// might be acquired by this function
#define FUNCTION_LOCKS(x) THREAD_ANNOTATION_ATTRIBUTE__((locks_excluded(x)))