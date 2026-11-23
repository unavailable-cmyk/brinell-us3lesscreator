#ifndef VULKANIZER_WGL_EVIDENCE_H
#define VULKANIZER_WGL_EVIDENCE_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

uint64_t vulkanizer_wgl_now_ns(void);
void vulkanizer_wgl_record_swap(HWND hwnd, uint64_t total_ns, uint64_t flush_ns,
                               uint64_t interval_wait_ns, uint64_t present_stage_ns,
                               BOOL succeeded);
#endif
