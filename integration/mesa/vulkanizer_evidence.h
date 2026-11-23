#ifndef VULKANIZER_MESA_EVIDENCE_H
#define VULKANIZER_MESA_EVIDENCE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void vulkanizer_evidence_draw(void);
void vulkanizer_evidence_compute_dispatch(void);
void vulkanizer_evidence_surface(void *hwnd, int result);
int vulkanizer_evidence_timing_enabled(void);
uint64_t vulkanizer_evidence_now_ns(void);
void vulkanizer_evidence_submit_elapsed(uint64_t elapsed_ns);
void vulkanizer_evidence_flush_elapsed(uint64_t elapsed_ns);
void vulkanizer_evidence_acquire_elapsed(uint64_t elapsed_ns);
void vulkanizer_evidence_present(void *hwnd, int result, uint64_t elapsed_ns);
#ifdef __cplusplus
}
#endif
/* Preserve the surrounding if/else statement and the original function call. */
#define VULKANIZER_VK_DRAW(name) (vulkanizer_evidence_draw(), VKCTX(name))
#define VULKANIZER_VK_DISPATCH(name) (vulkanizer_evidence_compute_dispatch(), VKCTX(name))
#endif
