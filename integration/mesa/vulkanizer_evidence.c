/* Optional CPU-side timing and API evidence for the pinned Mesa Zink build.
 * Measurements are emitted once per 120 successful Win32 presents. They do
 * not change synchronization, rendering, or presentation policy. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "vulkanizer_evidence.h"

static INIT_ONCE initialized = INIT_ONCE_STATIC_INIT;
static SRWLOCK output_lock = SRWLOCK_INIT;
static wchar_t output_path[32768];
static BOOL enabled;
static BOOL every_present;
static uint64_t qpc_frequency;
static volatile LONG64 draw_calls, compute_dispatch_calls, present_calls, present_errors;
static volatile LONG64 submit_calls, submit_ns, submit_max_ns;
static volatile LONG64 window_present_calls, window_present_ns, window_present_max_ns;
static volatile LONG64 flush_calls, flush_ns, flush_max_ns;
static volatile LONG64 acquire_calls, acquire_ns, acquire_max_ns;

static BOOL CALLBACK initialize(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
   LARGE_INTEGER frequency;
   (void)once; (void)parameter; (void)context;
   DWORD length = GetEnvironmentVariableW(L"VULKANIZER_EVIDENCE_FILE", output_path, 32768);
   enabled = length > 0 && length < 32768;
   wchar_t present_mode[2];
   every_present = GetEnvironmentVariableW(L"VULKANIZER_EVIDENCE_EVERY_PRESENT",
                                            present_mode, 2) == 1 && present_mode[0] == L'1';
   if (enabled && QueryPerformanceFrequency(&frequency))
      qpc_frequency = (uint64_t)frequency.QuadPart;
   else
      enabled = FALSE;
   return TRUE;
}

static BOOL tracing(void)
{
   InitOnceExecuteOnce(&initialized, initialize, NULL, NULL);
   return enabled;
}

int vulkanizer_evidence_timing_enabled(void)
{
   return tracing();
}

uint64_t vulkanizer_evidence_now_ns(void)
{
   LARGE_INTEGER counter;
   uint64_t ticks;
   if (!tracing() || !QueryPerformanceCounter(&counter))
      return 0;
   ticks = (uint64_t)counter.QuadPart;
   return (ticks / qpc_frequency) * 1000000000ull +
          ((ticks % qpc_frequency) * 1000000000ull) / qpc_frequency;
}

static LONG64 atomic_max(volatile LONG64 *target, uint64_t value)
{
   LONG64 previous = InterlockedCompareExchange64(target, 0, 0);
   while ((uint64_t)previous < value) {
      LONG64 observed = InterlockedCompareExchange64(target, (LONG64)value, previous);
      if (observed == previous)
         return previous;
      previous = observed;
   }
   return previous;
}

static void write_event(const char *event, void *hwnd, int result)
{
   AcquireSRWLockExclusive(&output_lock);
   FILE *file = _wfopen(output_path, L"ab");
   if (file) {
      fprintf(file, "{\"schema\":2,\"source\":\"mesa-26.2.6-zink\",\"event\":\"%s\","
                    "\"pid\":%lu,\"hwnd\":\"0x%llx\",\"result\":%d,"
                    "\"draw_calls\":%lld,\"compute_dispatch_calls\":%lld,"
                    "\"present_calls\":%lld,\"present_errors\":%lld,"
                    "\"window_present_calls\":%lld,\"window_present_total_ms\":%.6f,"
                    "\"window_present_max_ms\":%.6f,\"window_submit_calls\":%lld,"
                    "\"window_submit_total_ms\":%.6f,\"window_submit_max_ms\":%.6f,"
                    "\"window_flush_calls\":%lld,\"window_flush_total_ms\":%.6f,"
                    "\"window_flush_max_ms\":%.6f,\"window_acquire_calls\":%lld,"
                    "\"window_acquire_total_ms\":%.6f,\"window_acquire_max_ms\":%.6f}\n",
              event, GetCurrentProcessId(), (unsigned long long)(uintptr_t)hwnd, result,
              InterlockedCompareExchange64(&draw_calls, 0, 0),
              InterlockedCompareExchange64(&compute_dispatch_calls, 0, 0),
              InterlockedCompareExchange64(&present_calls, 0, 0),
              InterlockedCompareExchange64(&present_errors, 0, 0),
              InterlockedExchange64(&window_present_calls, 0),
              InterlockedExchange64(&window_present_ns, 0) / 1000000.0,
              InterlockedExchange64(&window_present_max_ns, 0) / 1000000.0,
              InterlockedExchange64(&submit_calls, 0),
              InterlockedExchange64(&submit_ns, 0) / 1000000.0,
              InterlockedExchange64(&submit_max_ns, 0) / 1000000.0,
              InterlockedExchange64(&flush_calls, 0),
              InterlockedExchange64(&flush_ns, 0) / 1000000.0,
              InterlockedExchange64(&flush_max_ns, 0) / 1000000.0,
              InterlockedExchange64(&acquire_calls, 0),
              InterlockedExchange64(&acquire_ns, 0) / 1000000.0,
              InterlockedExchange64(&acquire_max_ns, 0) / 1000000.0);
      fclose(file);
   }
   ReleaseSRWLockExclusive(&output_lock);
}

void vulkanizer_evidence_draw(void)
{
   if (tracing()) InterlockedIncrement64(&draw_calls);
}

void vulkanizer_evidence_compute_dispatch(void)
{
   if (tracing()) InterlockedIncrement64(&compute_dispatch_calls);
}

void vulkanizer_evidence_surface(void *hwnd, int result)
{
   if (tracing()) write_event("vkCreateWin32SurfaceKHR", hwnd, result);
}

void vulkanizer_evidence_submit_elapsed(uint64_t elapsed_ns)
{
   if (!tracing()) return;
   InterlockedIncrement64(&submit_calls);
   InterlockedExchangeAdd64(&submit_ns, (LONG64)elapsed_ns);
   atomic_max(&submit_max_ns, elapsed_ns);
}

void vulkanizer_evidence_flush_elapsed(uint64_t elapsed_ns)
{
   if (!tracing()) return;
   InterlockedIncrement64(&flush_calls);
   InterlockedExchangeAdd64(&flush_ns, (LONG64)elapsed_ns);
   atomic_max(&flush_max_ns, elapsed_ns);
}

void vulkanizer_evidence_acquire_elapsed(uint64_t elapsed_ns)
{
   if (!tracing()) return;
   InterlockedIncrement64(&acquire_calls);
   InterlockedExchangeAdd64(&acquire_ns, (LONG64)elapsed_ns);
   atomic_max(&acquire_max_ns, elapsed_ns);
}

void vulkanizer_evidence_present(void *hwnd, int result, uint64_t elapsed_ns)
{
   if (!tracing()) return;
   LONG64 frame = InterlockedIncrement64(&present_calls);
   InterlockedIncrement64(&window_present_calls);
   InterlockedExchangeAdd64(&window_present_ns, (LONG64)elapsed_ns);
   atomic_max(&window_present_max_ns, elapsed_ns);
   /* VK_SUCCESS and VK_SUBOPTIMAL_KHR both represent a presented image. */
   if (result != 0 && result != 1000001003) {
      if (InterlockedIncrement64(&present_errors) <= 8)
         write_event("vkQueuePresentKHR", hwnd, result);
   } else if (frame == 1 || every_present || frame % 120 == 0) {
      write_event("vkQueuePresentKHR", hwnd, result);
   }
}
