/* Optional per-call WGL swap timing; emitted once per 120 swaps. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "vulkanizer_wgl_evidence.h"

static INIT_ONCE initialized = INIT_ONCE_STATIC_INIT;
static SRWLOCK lock = SRWLOCK_INIT;
static wchar_t output_path[32768];
static uint64_t frequency;
static BOOL enabled;
static LONG64 calls, failures;
static LONG64 total_ns, total_max_ns;
static LONG64 flush_ns, flush_max_ns;
static LONG64 wait_ns, wait_max_ns;
static LONG64 present_ns, present_max_ns;

static BOOL CALLBACK initialize(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
   LARGE_INTEGER value;
   (void)once; (void)parameter; (void)context;
   DWORD length = GetEnvironmentVariableW(L"VULKANIZER_WGL_EVIDENCE_FILE", output_path, 32768);
   enabled = length > 0 && length < 32768;
   if (enabled && QueryPerformanceFrequency(&value))
      frequency = (uint64_t)value.QuadPart;
   else
      enabled = FALSE;
   return TRUE;
}

static BOOL tracing(void)
{
   InitOnceExecuteOnce(&initialized, initialize, NULL, NULL);
   return enabled;
}

uint64_t vulkanizer_wgl_now_ns(void)
{
   LARGE_INTEGER value;
   uint64_t ticks;
   if (!tracing() || !QueryPerformanceCounter(&value))
      return 0;
   ticks = (uint64_t)value.QuadPart;
   return (ticks / frequency) * 1000000000ull +
          ((ticks % frequency) * 1000000000ull) / frequency;
}

static void add_max(volatile LONG64 *target, uint64_t value)
{
   LONG64 old = InterlockedCompareExchange64(target, 0, 0);
   while ((uint64_t)old < value) {
      LONG64 current = InterlockedCompareExchange64(target, (LONG64)value, old);
      if (current == old)
         return;
      old = current;
   }
}

void vulkanizer_wgl_record_swap(HWND hwnd, uint64_t frame_ns, uint64_t flush_value,
                               uint64_t wait_value, uint64_t present_value, BOOL succeeded)
{
   LONG64 count;
   if (!tracing())
      return;
   InterlockedExchangeAdd64(&total_ns, (LONG64)frame_ns);
   add_max(&total_max_ns, frame_ns);
   InterlockedExchangeAdd64(&flush_ns, (LONG64)flush_value);
   add_max(&flush_max_ns, flush_value);
   InterlockedExchangeAdd64(&wait_ns, (LONG64)wait_value);
   add_max(&wait_max_ns, wait_value);
   InterlockedExchangeAdd64(&present_ns, (LONG64)present_value);
   add_max(&present_max_ns, present_value);
   if (!succeeded)
      InterlockedIncrement64(&failures);
   count = InterlockedIncrement64(&calls);
   if (count % 120)
      return;

   AcquireSRWLockExclusive(&lock);
   FILE *file = _wfopen(output_path, L"ab");
   if (file) {
      fprintf(file, "{\"schema\":1,\"source\":\"mesa-26.2.6-wgl\",\"event\":\"wglSwapBuffers\","
                    "\"pid\":%lu,\"hwnd\":\"0x%llx\",\"swap_calls\":120,\"failures\":%lld,"
                    "\"total_ms\":%.6f,\"total_max_ms\":%.6f,\"flush_ms\":%.6f,"
                    "\"flush_max_ms\":%.6f,\"interval_wait_ms\":%.6f,"
                    "\"interval_wait_max_ms\":%.6f,\"present_stage_ms\":%.6f,"
                    "\"present_stage_max_ms\":%.6f}\n",
              GetCurrentProcessId(), (unsigned long long)(uintptr_t)hwnd,
              InterlockedExchange64(&failures, 0),
              InterlockedExchange64(&total_ns, 0) / 1000000.0,
              InterlockedExchange64(&total_max_ns, 0) / 1000000.0,
              InterlockedExchange64(&flush_ns, 0) / 1000000.0,
              InterlockedExchange64(&flush_max_ns, 0) / 1000000.0,
              InterlockedExchange64(&wait_ns, 0) / 1000000.0,
              InterlockedExchange64(&wait_max_ns, 0) / 1000000.0,
              InterlockedExchange64(&present_ns, 0) / 1000000.0,
              InterlockedExchange64(&present_max_ns, 0) / 1000000.0);
      fclose(file);
   }
   ReleaseSRWLockExclusive(&lock);
}
