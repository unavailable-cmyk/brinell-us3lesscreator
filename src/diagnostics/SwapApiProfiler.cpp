#include "SwapApiProfiler.hpp"
#include "core/RendererManager.hpp"
#include <Geode/Geode.hpp>
#include <windows.h>
#include <cstring>

namespace vulkanizer {
namespace {
using SwapBuffersFn = BOOL(WINAPI*)(HDC);
SwapBuffersFn g_original = nullptr;
geode::Patch* g_patch = nullptr;

BOOL WINAPI timedSwapBuffers(HDC dc) {
    auto& manager = RendererManager::get();
    if (!manager.diagnosticsEnabled())
        return g_original ? g_original(dc) : FALSE;
    auto start = RendererManager::Clock::now();
    BOOL result = g_original ? g_original(dc) : FALSE;
    double elapsed = std::chrono::duration<double, std::milli>(RendererManager::Clock::now() - start).count();
    manager.recordSystemSwapBuffers(elapsed);
    return result;
}

void** findImport(HMODULE module) {
    if (!module) return nullptr;
    auto* base = reinterpret_cast<std::byte*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return nullptr;
    auto const& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size) return nullptr;
    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; imports->Name; ++imports) {
        auto* dll = reinterpret_cast<char*>(base + imports->Name);
        if (_stricmp(dll, "GDI32.dll") != 0 && _stricmp(dll, "GDI32Full.dll") != 0) continue;
        if (!imports->OriginalFirstThunk) return nullptr;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + imports->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + imports->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            auto* entry = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<char*>(entry->Name), "SwapBuffers") == 0)
                return reinterpret_cast<void**>(&slots->u1.Function);
        }
    }
    return nullptr;
}
}

bool installSwapApiProfiler() {
    if (g_patch) return true;
    void** slot = findImport(GetModuleHandleW(L"libcocos2d.dll"));
    if (!slot || !*slot) {
        geode::log::warn("GDI SwapBuffers timing unavailable: import not found in libcocos2d.dll");
        return false;
    }
    g_original = reinterpret_cast<SwapBuffersFn>(*slot);
    geode::ByteVector replacement(sizeof(void*));
    void* detour = reinterpret_cast<void*>(&timedSwapBuffers);
    std::memcpy(replacement.data(), &detour, sizeof(detour));
    auto result = geode::Mod::get()->patch(slot, std::move(replacement));
    if (!result) {
        g_original = nullptr;
        geode::log::warn("GDI SwapBuffers timing unavailable: could not patch Cocos import");
        return false;
    }
    g_patch = result.unwrap();
    if (!g_patch->isEnabled()) {
        auto enabled = g_patch->enable();
        if (!enabled) {
            g_patch = nullptr;
            g_original = nullptr;
            geode::log::warn("GDI SwapBuffers timing unavailable: patch could not be enabled");
            return false;
        }
    }
    geode::log::info("GDI SwapBuffers timing active on libcocos2d.dll import");
    return true;
}

void uninstallSwapApiProfiler() {
    if (g_patch && g_patch->isEnabled())
        (void)g_patch->disable();
    g_patch = nullptr;
    g_original = nullptr;
}
}
