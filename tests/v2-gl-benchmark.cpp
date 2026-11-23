#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include "v2/GlRenderer.hpp"
#include "v2-fixture.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

using namespace v2test;
namespace {
constexpr unsigned width = 512, height = 320, frames = 240;
constexpr GLenum timeElapsed = 0x88bf, queryResult = 0x8866;
using GetProc = PROC(WINAPI*)(LPCSTR);
struct Context {
    HMODULE library = nullptr;
    HWND window = nullptr;
    HDC dc = nullptr;
    HGLRC rc = nullptr;
    GetProc getProc = nullptr;
    BOOL(WINAPI* makeCurrent)(HDC, HGLRC) = nullptr;
    BOOL(WINAPI* deleteContext)(HGLRC) = nullptr;
    bool registered = false;
    void* resolve(const char* name) {
        auto address = GetProcAddress(library, name);
        if (!address) address = getProc(name);
        const auto bits = reinterpret_cast<std::uintptr_t>(address);
        if (bits <= 3 || bits == ~std::uintptr_t{}) throw std::runtime_error(std::string("Missing GL function ") + name);
        return reinterpret_cast<void*>(address);
    }
    void open(const std::filesystem::path& provider) {
        library = LoadLibraryExW(provider.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        require(library != nullptr, "Load OpenGL provider, Win32=" + std::to_string(GetLastError()));
        getProc = reinterpret_cast<GetProc>(GetProcAddress(library, "wglGetProcAddress"));
        require(getProc != nullptr, "wglGetProcAddress");
        auto create = reinterpret_cast<HGLRC(WINAPI*)(HDC)>(resolve("wglCreateContext"));
        makeCurrent = reinterpret_cast<decltype(makeCurrent)>(resolve("wglMakeCurrent"));
        deleteContext = reinterpret_cast<decltype(deleteContext)>(resolve("wglDeleteContext"));
        WNDCLASSW klass{};
        klass.style = CS_OWNDC;
        klass.lpfnWndProc = DefWindowProcW;
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = L"VulkV2ControlledPrototype";
        require(RegisterClassW(&klass) != 0, "Register V2 probe window");
        registered = true;
        window = CreateWindowExW(0, klass.lpszClassName, L"VulkV2 hidden benchmark", WS_POPUP,
            0, 0, width, height, nullptr, nullptr, klass.hInstance, nullptr);
        require(window != nullptr, "Create V2 probe window");
        dc = GetDC(window);
        require(dc != nullptr, "Get V2 DC");
        PIXELFORMATDESCRIPTOR format{};
        format.nSize = sizeof(format);
        format.nVersion = 1;
        format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        format.iPixelType = PFD_TYPE_RGBA;
        format.cColorBits = 32;
        format.cAlphaBits = 8;
        const auto pixelFormat = ChoosePixelFormat(dc, &format);
        require(pixelFormat > 0 && SetPixelFormat(dc, pixelFormat, &format), "Set V2 pixel format");
        rc = create(dc);
        require(rc && makeCurrent(dc, rc), "Create V2 bootstrap context");
        using CreateAttribs = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
        auto attribs = reinterpret_cast<CreateAttribs>(resolve("wglCreateContextAttribsARB"));
        const int attributes[]{0x2091, 3, 0x2092, 3, 0x9126, 1, 0};
        auto modern = attribs(dc, nullptr, attributes);
        require(modern != nullptr, "Create GL 3.3 context");
        if (!makeCurrent(dc, modern)) { deleteContext(modern); throw std::runtime_error("Bind GL 3.3 context"); }
        deleteContext(rc);
        rc = modern;
    }
    ~Context() {
        if (rc) { makeCurrent(nullptr, nullptr); deleteContext(rc); }
        if (dc) ReleaseDC(window, dc);
        if (window) DestroyWindow(window);
        if (registered) UnregisterClassW(L"VulkV2ControlledPrototype", GetModuleHandleW(nullptr));
        if (library) FreeLibrary(library);
    }
};
struct Api {
#define PROC(R, N, A) R(APIENTRY* N)A = nullptr
    PROC(const GLubyte*, GetString, (GLenum));
    PROC(void, Viewport, (GLint, GLint, GLsizei, GLsizei));
    PROC(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat));
    PROC(void, Clear, (GLbitfield));
    PROC(void, Finish, ());
    PROC(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*));
    PROC(void, GenTextures, (GLsizei, GLuint*));
    PROC(void, DeleteTextures, (GLsizei, const GLuint*));
    PROC(void, ActiveTexture, (GLenum));
    PROC(void, BindTexture, (GLenum, GLuint));
    PROC(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*));
    PROC(void, TexParameteri, (GLenum, GLenum, GLint));
    PROC(void, Enable, (GLenum));
    PROC(void, Disable, (GLenum));
    PROC(void, BlendFunc, (GLenum, GLenum));
    PROC(void, GenQueries, (GLsizei, GLuint*));
    PROC(void, DeleteQueries, (GLsizei, const GLuint*));
    PROC(void, BeginQuery, (GLenum, GLuint));
    PROC(void, EndQuery, (GLenum));
    PROC(void, GetQueryObjectui64v, (GLuint, GLenum, std::uint64_t*));
#undef PROC
    explicit Api(Context& context) {
#define LOAD(N) N = reinterpret_cast<decltype(N)>(context.resolve("gl" #N))
        LOAD(GetString); LOAD(Viewport); LOAD(ClearColor); LOAD(Clear); LOAD(Finish); LOAD(ReadPixels);
        LOAD(GenTextures); LOAD(DeleteTextures); LOAD(ActiveTexture); LOAD(BindTexture); LOAD(TexImage2D);
        LOAD(TexParameteri); LOAD(Enable); LOAD(Disable); LOAD(BlendFunc); LOAD(GenQueries);
        LOAD(DeleteQueries); LOAD(BeginQuery); LOAD(EndQuery); LOAD(GetQueryObjectui64v);
#undef LOAD
    }
};
struct Materials : RenderSink {
    Api& gl;
    GLuint texture = 0;
    std::vector<ObjectId> nativeDraws;
    std::function<void(ObjectId)> nativeDraw;
    explicit Materials(Api& api) : gl(api) {
        gl.GenTextures(1, &texture);
        gl.ActiveTexture(0x84c0);
        gl.BindTexture(GL_TEXTURE_2D, texture);
        const std::array<std::uint8_t, 16> pixels{255, 50, 80, 255, 20, 240, 100, 255,
            50, 80, 255, 255, 220, 180, 40, 255};
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    ~Materials() { gl.DeleteTextures(1, &texture); }
    void bindMaterial(MaterialId material) override {
        gl.ActiveTexture(0x84c0);
        gl.BindTexture(GL_TEXTURE_2D, texture);
        gl.Enable(GL_BLEND);
        if (material == 1) gl.BlendFunc(GL_SRC_ALPHA, GL_ONE);
        else gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    void drawNative(ObjectId id) override {
        nativeDraws.push_back(id);
        if (nativeDraw) nativeDraw(id);
    }
};
std::array<float, 16> matrix() {
    return {2.f / width, 0, 0, 0, 0, 2.f / height, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1};
}
std::vector<std::uint8_t> image(Api& gl) {
    std::vector<std::uint8_t> result(width * height * 4);
    gl.ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
    return result;
}
std::string jsonString(const char* text) {
    std::string result = "\"";
    for (; *text; ++text) {
        if (*text == '\\' || *text == '"') result += '\\';
        require(static_cast<unsigned char>(*text) >= 32, "Unexpected control byte in GL identity");
        result += *text;
    }
    return result + '"';
}
void moveReference(std::vector<Decoration>& objects, GroupId group, Translation delta) {
    for (auto& object : objects) {
        if (std::find(object.groups.begin(), object.groups.end(), group) == object.groups.end()) continue;
        object.position.x += delta.x;
        object.position.y += delta.y;
    }
}
// Build CPU-reference geometry once, outside all timed sections.
void materializeReference(std::vector<Decoration>& reference, std::span<const Decoration> original) {
    for (std::size_t id = 0; id < reference.size(); ++id) {
        const double dx = reference[id].position.x - original[id].position.x;
        const double dy = reference[id].position.y - original[id].position.y;
        for (unsigned corner = 0; corner < 4; ++corner) {
            reference[id].quad[corner].x = static_cast<float>(original[id].quad[corner].x + dx);
            reference[id].quad[corner].y = static_cast<float>(original[id].quad[corner].y + dy);
        }
    }
}
unsigned compare(const std::vector<std::uint8_t>& actual, const std::vector<std::uint8_t>& expected) {
    unsigned errors = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) if (actual[i] != expected[i]) ++errors;
    return errors;
}
unsigned coloredPixels(const std::vector<std::uint8_t>& pixels) {
    unsigned count = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4)
        if (pixels[i] || pixels[i + 1] || pixels[i + 2]) ++count;
    return count;
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        std::filesystem::path provider;
        if (argc == 2) provider = std::filesystem::absolute(argv[1]);
        else {
            wchar_t system[MAX_PATH]{};
            require(GetSystemDirectoryW(system, MAX_PATH) != 0, "System OpenGL directory");
            provider = std::filesystem::path(system) / L"opengl32.dll";
        }
        Context context;
        context.open(provider);
        Api gl(context);
        Materials materials(gl);
        GlRenderer renderer([&](const char* name) { return context.resolve(name); });
        GlRenderer oracleRenderer([&](const char* name) { return context.resolve(name); });
        gl.Viewport(0, 0, width, height);
        gl.Disable(GL_DITHER);
        gl.ClearColor(0, 0, 0, 0);
        const auto mvp = matrix();
        std::cout << "{\"schema\":1,\"scope\":\"controlled_shared_translation_gl33\",\"renderer\":"
            << jsonString(reinterpret_cast<const char*>(gl.GetString(GL_RENDERER))) << ",\"gl_version\":"
            << jsonString(reinterpret_cast<const char*>(gl.GetString(GL_VERSION)))
            << ",\"width\":" << width << ",\"height\":" << height << ",\"material_initial_texture_upload_bytes\":16,\"cases\":[\n";
        bool first = true;
        for (auto count : {1u, 1000u, 10000u, 100000u}) {
            for (bool overlap : {false, true}) {
                auto input = fixture(count, overlap);
                auto scene = Scene::compile(input);
                Native native(input);
                renderer.clearCounters();
                renderer.attach(scene);
                const auto initialBytes = renderer.counters().initialUploadBytes;
                for (unsigned warmup = 0; warmup < 16; ++warmup) {
                    gl.Clear(GL_COLOR_BUFFER_BIT);
                    renderer.render(scene, mvp, materials);
                }
                gl.Finish();
                scene.clearCounters();
                renderer.clearCounters();
                std::vector<double> cpu;
                cpu.reserve(frames);
                std::array<GLuint, frames> queries{};
                gl.GenQueries(frames, queries.data());
                std::uint64_t order = 0;
                for (unsigned frame = 0; frame < frames; ++frame) {
                    gl.Clear(GL_COLOR_BUFFER_BIT);
                    gl.BeginQuery(timeElapsed, queries[frame]);
                    const auto started = std::chrono::steady_clock::now();
                    scene.translate(7, {.125, 0}, ++order, native);
                    if (overlap) scene.translate(2, {0, .0625}, ++order, native);
                    renderer.render(scene, mvp, materials);
                    cpu.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count());
                    gl.EndQuery(timeElapsed);
                }
                gl.Finish();
                std::vector<double> gpu;
                for (auto query : queries) {
                    std::uint64_t ns = 0;
                    gl.GetQueryObjectui64v(query, queryResult, &ns);
                    gpu.push_back(static_cast<double>(ns) / 1e6);
                }
                gl.DeleteQueries(frames, queries.data());
                const auto actualImage = image(gl);
                const auto colored = coloredPixels(actualImage);
                require(colored > 0, "Empty GPU output cannot validate transformations");
                auto reference = input;
                for (unsigned frame = 0; frame < frames; ++frame) {
                    moveReference(reference, 7, {.125, 0});
                    if (overlap) moveReference(reference, 2, {0, .0625});
                }
                double maxPositionError = 0;
                for (ObjectId id = 0; id < reference.size(); ++id) {
                    const auto actual = scene.position(scene.handle(id));
                    maxPositionError = std::max({maxPositionError, std::abs(actual.x - reference[id].position.x),
                        std::abs(actual.y - reference[id].position.y)});
                }
                materializeReference(reference, input);
                auto oracle = Scene::compile(reference);
                oracleRenderer.attach(oracle);
                gl.Clear(GL_COLOR_BUFFER_BIT);
                oracleRenderer.render(oracle, mvp, materials);
                const auto mismatches = compare(actualImage, image(gl));
                require(mismatches == 0 && maxPositionError == 0, "Shared draw differs from native-ordered CPU reference");
                require(scene.counters().drawListObjectVisits == 0 && native.visits == 0, "Routine object traversal");
                const auto counters = renderer.counters();
                require(counters.paletteUploadBytes == frames * scene.chainCount() * sizeof(PaletteEntry), "Unexpected routine palette upload");
                require(counters.uploadBytes == counters.paletteUploadBytes + counters.drawUniformUploadBytes,
                    "Unaccounted renderer upload");
                std::sort(cpu.begin(), cpu.end());
                std::sort(gpu.begin(), gpu.end());
                if (!first) std::cout << ",\n";
                first = false;
                std::cout << std::setprecision(9) << "{\"objects\":" << count << ",\"overlap\":" << (overlap ? "true" : "false")
                    << ",\"chains\":" << scene.chainCount() << ",\"frames\":" << frames
                    << ",\"cpu_submission_p50_us\":" << cpu[frames / 2] << ",\"cpu_submission_p95_us\":" << cpu[frames * 95 / 100]
                    << ",\"gpu_p50_ms\":" << gpu[frames / 2] << ",\"gpu_p95_ms\":" << gpu[frames * 95 / 100]
                    << ",\"initial_upload_bytes\":" << initialBytes << ",\"routine_upload_bytes\":" << counters.uploadBytes
                    << ",\"routine_palette_upload_bytes\":" << counters.paletteUploadBytes
                    << ",\"routine_draw_uniform_upload_bytes\":" << counters.drawUniformUploadBytes
                    << ",\"draw_count\":" << counters.drawCalls << ",\"dispatch_count\":" << counters.dispatches
                    << ",\"chain_evaluations\":" << scene.counters().chainEvaluations
                    << ",\"routine_object_visits\":0,\"routine_transform_readback_bytes\":0,\"cpu_transform_commits\":0"
                    << ",\"max_position_error\":" << maxPositionError << ",\"image_mismatched_channels\":" << mismatches
                    << ",\"colored_pixels\":" << colored << '}';
            }
        }
        auto pagedInput = fixture(1000, false);
        for (unsigned id = 0; id < pagedInput.size(); ++id) pagedInput[id].groups = {id};
        auto paged = Scene::compile(pagedInput);
        Native pagedNative(pagedInput);
        renderer.attach(paged);
        renderer.clearCounters();
        paged.translate(999, {4, 8}, 1, pagedNative);
        gl.Clear(GL_COLOR_BUFFER_BIT);
        renderer.render(paged, mvp, materials);
        const auto pagedImage = image(gl);
        auto pagedReference = pagedInput;
        moveReference(pagedReference, 999, {4, 8});
        materializeReference(pagedReference, pagedInput);
        auto pagedOracle = Scene::compile(pagedReference);
        oracleRenderer.attach(pagedOracle);
        gl.Clear(GL_COLOR_BUFFER_BIT);
        oracleRenderer.render(pagedOracle, mvp, materials);
        require(compare(pagedImage, image(gl)) == 0 && renderer.counters().drawCalls == 4 &&
            renderer.counters().paletteUploadBytes == 16, "Palette page rendering or active-chain upload mismatch");

        // Ordered overlapping materials, dynamic visibility, reset and promotion.
        std::vector<Decoration> visual;
        for (unsigned i = 0; i < 6; ++i) {
            auto object = decoration(80 + i * 4, 80, {7});
            object.material = i % 2;
            for (auto& corner : object.quad) {
                corner.x = 80 + i * 4 + (corner.u * 32);
                corner.y = 80 + (corner.v * 32);
                corner.color = {static_cast<std::uint8_t>(255 - i * 20), 180, 220, 128};
            }
            visual.push_back(object);
        }
        auto scene = Scene::compile(visual);
        Native native(visual);
        renderer.attach(scene);
        scene.translate(7, {4, 8}, 1, native);
        scene.setVisible(scene.handle(2), false, native);
        gl.Clear(GL_COLOR_BUFFER_BIT);
        renderer.render(scene, mvp, materials);
        const auto actual = image(gl);
        auto reference = visual;
        moveReference(reference, 7, {4, 8});
        reference[2].visible = false;
        materializeReference(reference, visual);
        auto oracle = Scene::compile(reference);
        oracleRenderer.attach(oracle);
        gl.Clear(GL_COLOR_BUFFER_BIT);
        oracleRenderer.render(oracle, mvp, materials);
        require(compare(actual, image(gl)) == 0, "Ordered blend/visibility reference mismatch");
        scene.reset(native);
        gl.Clear(GL_COLOR_BUFFER_BIT);
        renderer.render(scene, mvp, materials);
        const auto resetImage = image(gl);
        oracle = Scene::compile(visual);
        oracleRenderer.attach(oracle);
        gl.Clear(GL_COLOR_BUFFER_BIT);
        oracleRenderer.render(oracle, mvp, materials);
        require(compare(resetImage, image(gl)) == 0, "Reset image differs");
        // Materialize the promoted object's CPU geometry before its first native
        // draw, then submit it at the original place between shared ranges.
        scene.translate(7, {4, 8}, 1, native);
        scene.acquireNative(scene.handle(3), native);
        auto nativeReference = visual;
        nativeReference[3].position = native.positions[3];
        materializeReference(nativeReference, visual);
        auto nativeScene = Scene::compile(std::span<const Decoration>(nativeReference).subspan(3, 1));
        GlRenderer nativeRenderer([&](const char* name) { return context.resolve(name); });
        nativeRenderer.attach(nativeScene);
        materials.nativeDraw = [&](ObjectId id) {
            require(id == 3, "Unexpected native draw owner");
            nativeRenderer.render(nativeScene, mvp, materials);
        };
        materials.nativeDraws.clear();
        gl.Clear(GL_COLOR_BUFFER_BIT);
        renderer.render(scene, mvp, materials);
        require(materials.nativeDraws == std::vector<ObjectId>{3}, "Native draw routed more than once");
        const auto mixedImage = image(gl);
        reference = visual;
        moveReference(reference, 7, {4, 8});
        materializeReference(reference, visual);
        oracle = Scene::compile(reference);
        oracleRenderer.attach(oracle);
        gl.Clear(GL_COLOR_BUFFER_BIT);
        oracleRenderer.render(oracle, mvp, materials);
        require(compare(mixedImage, image(gl)) == 0, "Mixed native/shared ordered image mismatch");
        materials.nativeDraw = {};
        const auto old = scene.handle(0);
        scene = Scene::compile(visual);
        rejects([&] { renderer.render(scene, mvp, materials); }, "Stale GPU scene accepted");
        rejects([&] { scene.path(old); }, "Stale CPU handle accepted");
        std::cout << "\n],\"ordered_material_visibility_reset_tests\":\"pass\",\"fallback_lifetime_tests\":\"pass\","
            << "\"palette_paging_test\":\"pass\",\"validation_readbacks_outside_timing\":true,\"query_results_collected_after_batch\":true}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2_GL_FAILED " << error.what() << " Win32=" << GetLastError() << '\n';
        return 1;
    }
}
