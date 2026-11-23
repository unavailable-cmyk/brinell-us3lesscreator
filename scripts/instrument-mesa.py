"""Apply fail-closed timing/evidence hooks to pinned Mesa 26.2.6 Zink source."""
import argparse
import hashlib
import json
from pathlib import Path

HASHES = {
    "zink_draw.cpp": "d2e9eb00aac90bef60193494c194e982dc1ec90a2906f7c8922e2b399fdb6079",
    "zink_kopper.c": "54574a34d03d0f68cc9de0f55649cc2230ad2d386d8eb6b9cf14500703862625",
    "zink_batch.c": "21ffd38d7b37a426642ad009434d4391316c185966ab21cda0d39177e3140fec",
    "zink_context.c": "c1e9e0000b75a89061f9ca85b60dc42cadbf20683a72da99387c6ce8baa0301e",
    "meson.build": "5713293c8f9ea0e6846a1d0d301e9980cf5d4208cc91a5e81194371210999d44",
    "wgl/stw_framebuffer.c": "3d665c84b1140055c1122bcae807f0a57f4c669fe47bdf0af6325a2be28ce66d",
    "wgl/meson.build": "c155839dc8c2a7c894c308863d968ad29914d82265ef7d846a70fb35fb0f19af",
}


def instrument(source: Path, project: Path) -> dict:
    zink = source / "src/gallium/drivers/zink"
    wgl = source / "src/gallium/frontends/wgl"
    pristine = project / ".tools/mesa-26.2.6-pristine/src/gallium/drivers/zink"
    pristine_wgl = project / ".tools/mesa-26.2.6-pristine/src/gallium/frontends/wgl"
    support = project / "integration/mesa"
    marker = zink / "vulkanizer-patch.json"
    if not pristine.exists():
        raise RuntimeError("Checksum-verified pristine Mesa source is required to update instrumentation")

    originals = {}
    for key, expected in HASHES.items():
        root = pristine_wgl if key.startswith("wgl/") else pristine
        name = key[4:] if key.startswith("wgl/") else key
        value = (root / name).read_bytes()
        if hashlib.sha256(value).hexdigest() != expected:
            raise RuntimeError(f"Not the verified pristine Mesa 26.2.6 file: {key}")
        originals[key] = value
    texts = {name: originals[name].decode("utf-8") for name in HASHES if not name.startswith("wgl/")}
    wgl_texts = {name[4:]: originals[name].decode("utf-8") for name in HASHES if name.startswith("wgl/")}

    texts["zink_draw.cpp"] = '#include "vulkanizer_evidence.h"\n' + texts["zink_draw.cpp"]
    import re
    texts["zink_draw.cpp"], draw_sites = re.subn(
        r"VKCTX\((CmdDraw\w*)\)", r"VULKANIZER_VK_DRAW(\1)", texts["zink_draw.cpp"]
    )
    texts["zink_draw.cpp"], dispatch_sites = re.subn(
        r"VKCTX\((CmdDispatch(?:Indirect)?)\)",
        r"VULKANIZER_VK_DISPATCH(\1)", texts["zink_draw.cpp"]
    )
    if draw_sites != 16:
        raise RuntimeError(f"Expected 16 Vulkan draw call sites; found {draw_sites}")
    if dispatch_sites != 2:
        raise RuntimeError(f"Expected 2 Vulkan compute dispatch call sites; found {dispatch_sites}")

    kopper = texts["zink_kopper.c"]
    surface = "      error = VKSCR(CreateWin32SurfaceKHR)(screen->instance, win32, NULL, &surface);"
    present = "   VkResult error2 = VKSCR(QueuePresentKHR)(screen->queue, &cpi->info);"
    if kopper.count(surface) != 1 or kopper.count(present) != 1:
        raise RuntimeError("Mesa Win32 WSI call sites changed")
    kopper = '#include "vulkanizer_evidence.h"\n' + kopper
    kopper = kopper.replace(surface, surface + "\n      vulkanizer_evidence_surface(win32->hwnd, error);")
    kopper = kopper.replace(
        present,
        "   uint64_t present_start_ns = vulkanizer_evidence_now_ns();\n" + present +
        "\n   uint64_t present_elapsed_ns = present_start_ns ? vulkanizer_evidence_now_ns() - present_start_ns : 0;\n"
        "#ifdef VK_USE_PLATFORM_WIN32_KHR\n"
        "   if (cdt->type == KOPPER_WIN32) {\n"
        "      VkWin32SurfaceCreateInfoKHR *win32 = (VkWin32SurfaceCreateInfoKHR *)&cdt->info.bos;\n"
        "      vulkanizer_evidence_present(win32->hwnd, error2 == VK_SUCCESS ? error : error2, present_elapsed_ns);\n"
        "   }\n"
        "#endif"
    )
    texts["zink_kopper.c"] = kopper

    acquire = "      ret = VKSCR(AcquireNextImageKHR)(screen->dev, cdt->swapchain->swapchain, timeout, acquire, VK_NULL_HANDLE, &res->obj->dt_idx);"
    if kopper.count(acquire) != 1:
        raise RuntimeError("Mesa Zink AcquireNextImageKHR call site changed")
    texts["zink_kopper.c"] = texts["zink_kopper.c"].replace(
        acquire,
        "      uint64_t acquire_start_ns = vulkanizer_evidence_now_ns();\n" + acquire +
        "\n      if (acquire_start_ns)\n"
        "         vulkanizer_evidence_acquire_elapsed(vulkanizer_evidence_now_ns() - acquire_start_ns);",
        1,
    )

    batch = texts["zink_batch.c"]
    submit = "      VKSCR(QueueSubmit)(screen->queue, num_si, submit, VK_NULL_HANDLE),"
    if batch.count(submit) != 1:
        raise RuntimeError("Mesa Zink batch QueueSubmit call site changed")
    batch = '#include "vulkanizer_evidence.h"\n' + batch
    batch = batch.replace(
        "   simple_mtx_lock(screen->queue_lock);\n   VRAM_ALLOC_LOOP(result,\n" + submit,
        "   simple_mtx_lock(screen->queue_lock);\n"
        "   uint64_t submit_start_ns = vulkanizer_evidence_now_ns();\n"
        "   VRAM_ALLOC_LOOP(result,\n" + submit,
        1,
    )
    batch = batch.replace(
        "   );\n   simple_mtx_unlock(screen->queue_lock);",
        "   );\n"
        "   if (submit_start_ns)\n"
        "      vulkanizer_evidence_submit_elapsed(vulkanizer_evidence_now_ns() - submit_start_ns);\n"
        "   simple_mtx_unlock(screen->queue_lock);",
        1,
    )
    if batch == texts["zink_batch.c"] or "vulkanizer_evidence_submit_elapsed" not in batch:
        raise RuntimeError("Unable to instrument Mesa Zink batch submission")
    texts["zink_batch.c"] = batch

    context = texts["zink_context.c"]
    flush = "static void\nzink_flush(struct pipe_context *pctx,"
    flush_end = "   if (bs) {\n      if (!(flags & (PIPE_FLUSH_DEFERRED | PIPE_FLUSH_ASYNC)))\n         sync_flush(ctx, bs);\n   }\n}"
    if context.count(flush) != 1 or context.count(flush_end) != 1:
        raise RuntimeError("Mesa Zink flush instrumentation site changed")
    context = '#include "vulkanizer_evidence.h"\n' + context.replace(
        flush, "static void\nzink_flush(struct pipe_context *pctx,", 1
    )
    context = context.replace(
        "   struct zink_context *ctx = zink_context(pctx);\n   bool deferred = flags & PIPE_FLUSH_DEFERRED;",
        "   uint64_t flush_start_ns = vulkanizer_evidence_now_ns();\n"
        "   struct zink_context *ctx = zink_context(pctx);\n   bool deferred = flags & PIPE_FLUSH_DEFERRED;",
        1,
    )
    context = context.replace(
        flush_end,
        "   if (bs) {\n      if (!(flags & (PIPE_FLUSH_DEFERRED | PIPE_FLUSH_ASYNC)))\n         sync_flush(ctx, bs);\n   }\n"
        "   if (flush_start_ns)\n"
        "      vulkanizer_evidence_flush_elapsed(vulkanizer_evidence_now_ns() - flush_start_ns);\n}",
        1,
    )
    if "vulkanizer_evidence_flush_elapsed" not in context:
        raise RuntimeError("Unable to instrument Mesa Zink flush")
    texts["zink_context.c"] = context

    framebuffer = wgl_texts["stw_framebuffer.c"]
    if '#include "vulkanizer_wgl_evidence.h"' not in framebuffer:
        framebuffer = framebuffer.replace(
            '#include "stw_st.h"', '#include "stw_st.h"\n#include "vulkanizer_wgl_evidence.h"', 1
        )
    fn_start = "stw_framebuffer_swap_locked(HDC hdc, struct stw_framebuffer *fb)\n{\n   struct stw_context *ctx = stw_current_context();"
    if framebuffer.count(fn_start) != 1:
        raise RuntimeError("Mesa WGL framebuffer swap function changed")
    framebuffer = framebuffer.replace(
        fn_start,
        fn_start + "\n   uint64_t frame_start_ns = vulkanizer_wgl_now_ns();\n"
        "   uint64_t flush_total_ns = 0, wait_total_ns = 0, present_stage_ns = 0;",
        1,
    )
    front_flush = "      if (ctx)\n         stw_st_flush(ctx->st, fb->drawable, ST_FLUSH_END_OF_FRAME | ST_FLUSH_FRONT);\n      return true;"
    if framebuffer.count(front_flush) != 1:
        raise RuntimeError("Mesa WGL single-buffer flush path changed")
    framebuffer = framebuffer.replace(
        front_flush,
        "      if (ctx) {\n"
        "         uint64_t flush_start_ns = vulkanizer_wgl_now_ns();\n"
        "         stw_st_flush(ctx->st, fb->drawable, ST_FLUSH_END_OF_FRAME | ST_FLUSH_FRONT);\n"
        "         if (flush_start_ns) flush_total_ns += vulkanizer_wgl_now_ns() - flush_start_ns;\n"
        "      }\n"
        "      if (frame_start_ns)\n"
        "         vulkanizer_wgl_record_swap(WindowFromDC(hdc), vulkanizer_wgl_now_ns() - frame_start_ns, flush_total_ns, 0, 0, TRUE);\n"
        "      return true;",
        1,
    )
    draw_flush = "         /* flush current context */\n         stw_st_flush(ctx->st, fb->drawable, ST_FLUSH_END_OF_FRAME);"
    if framebuffer.count(draw_flush) != 1:
        raise RuntimeError("Mesa WGL draw flush path changed")
    framebuffer = framebuffer.replace(
        draw_flush,
        "         /* flush current context */\n"
        "         uint64_t flush_start_ns = vulkanizer_wgl_now_ns();\n"
        "         stw_st_flush(ctx->st, fb->drawable, ST_FLUSH_END_OF_FRAME);\n"
        "         if (flush_start_ns) flush_total_ns += vulkanizer_wgl_now_ns() - flush_start_ns;",
        1,
    )
    wait_call = "      wait_swap_interval(fb, interval);"
    if framebuffer.count(wait_call) != 1:
        raise RuntimeError("Mesa WGL swap-interval wait changed")
    framebuffer = framebuffer.replace(
        wait_call,
        "      uint64_t wait_start_ns = vulkanizer_wgl_now_ns();\n" + wait_call +
        "\n      if (wait_start_ns) wait_total_ns += vulkanizer_wgl_now_ns() - wait_start_ns;",
        1,
    )
    present_call = "   return stw_st_swap_framebuffer_locked(ctx ? ctx->st : NULL, hdc, fb->drawable);"
    if framebuffer.count(present_call) != 1:
        raise RuntimeError("Mesa WGL swap/present stage changed")
    framebuffer = framebuffer.replace(
        present_call,
        "   uint64_t present_start_ns = vulkanizer_wgl_now_ns();\n"
        "   BOOL succeeded = stw_st_swap_framebuffer_locked(ctx ? ctx->st : NULL, hdc, fb->drawable);\n"
        "   if (present_start_ns) present_stage_ns = vulkanizer_wgl_now_ns() - present_start_ns;\n"
        "   if (frame_start_ns)\n"
        "      vulkanizer_wgl_record_swap(WindowFromDC(hdc), vulkanizer_wgl_now_ns() - frame_start_ns,\n"
        "                                  flush_total_ns, wait_total_ns, present_stage_ns, succeeded);\n"
        "   return succeeded;",
        1,
    )
    wgl_texts["stw_framebuffer.c"] = framebuffer
    wgl_texts["meson.build"] = wgl_texts["meson.build"].replace(
        "files_libwgl = files(", "files_libwgl = files(\n  'vulkanizer_wgl_evidence.c',", 1
    )

    texts["meson.build"] = texts["meson.build"].replace(
        "files_libzink = files(", "files_libzink = files(\n  'vulkanizer_evidence.c',", 1
    )
    outputs = {f"zink/{name}": value.encode("utf-8") for name, value in texts.items()}
    outputs.update({f"zink/{name}": (support / name).read_bytes() for name in ("vulkanizer_evidence.c", "vulkanizer_evidence.h")})
    outputs.update({f"wgl/{name}": value.encode("utf-8") for name, value in wgl_texts.items()})
    outputs.update({f"wgl/vulkanizer_{name}": (support / f"vulkanizer_{name}").read_bytes()
                    for name in ("wgl_evidence.c", "wgl_evidence.h")})
    manifest = {
        "mesa_version": "26.2.6",
        "draw_dispatch_sites": draw_sites,
        "compute_dispatch_sites": dispatch_sites,
        "timings": ["zink_batch vkQueueSubmit CPU duration", "Win32 vkQueuePresentKHR CPU duration"],
        "upstream_hashes": HASHES,
        "patched_hashes": {name: hashlib.sha256(value).hexdigest() for name, value in outputs.items()},
    }
    for name, value in outputs.items():
        is_wgl = name.startswith("wgl/")
        root = wgl if is_wgl else zink
        filename = name[4:] if is_wgl else name[5:]
        (root / filename).write_bytes(value)
    marker.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    result = instrument(args.source.resolve(), Path(__file__).resolve().parents[1])
    print(f"Mesa {result['mesa_version']}: instrumented Vulkan API timings and WGL swap stages")
