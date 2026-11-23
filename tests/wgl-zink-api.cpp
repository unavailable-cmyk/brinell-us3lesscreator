#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
template<class T> T resolve(HMODULE module, char const* name) {
    auto function = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!function) throw std::runtime_error(std::string("Missing Mesa export ") + name);
    return function;
}
void require(bool condition, char const* operation) {
    if (!condition) throw std::runtime_error(std::string(operation) + " failed, Win32=" + std::to_string(GetLastError()));
}

struct WglCheck {
    HMODULE library = nullptr;
    HINSTANCE module = GetModuleHandleW(nullptr);
    HWND window = nullptr;
    HDC dc = nullptr;
    HGLRC context = nullptr;
    bool registered = false;
    GLuint texture = 0;
    decltype(&::wglCreateContext) create = nullptr;
    decltype(&::wglMakeCurrent) makeCurrent = nullptr;
    decltype(&::wglDeleteContext) deleteContext = nullptr;
    decltype(&::glDeleteTextures) deleteTextures = nullptr;

    void initialize(wchar_t const* provider) {
        // Match GD's provider name, then use the same GDI pixel-format APIs.
        library = LoadLibraryExW(provider, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        require(library != nullptr, "LoadLibraryExW(Mesa)");
        create = resolve<decltype(create)>(library, "wglCreateContext");
        makeCurrent = resolve<decltype(makeCurrent)>(library, "wglMakeCurrent");
        deleteContext = resolve<decltype(deleteContext)>(library, "wglDeleteContext");
        deleteTextures = resolve<decltype(deleteTextures)>(library, "glDeleteTextures");
        WNDCLASSW klass{};
        klass.style = CS_OWNDC;
        klass.lpfnWndProc = DefWindowProcW;
        klass.hInstance = module;
        klass.lpszClassName = L"VulkanizerWglApiCheck";
        require(RegisterClassW(&klass) != 0, "RegisterClassW");
        registered = true;
        window = CreateWindowExW(0, klass.lpszClassName, L"Vulkanizer hidden WGL API check",
            WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, module, nullptr);
        require(window != nullptr, "CreateWindowExW");
        dc = GetDC(window);
        require(dc != nullptr, "GetDC");
        PIXELFORMATDESCRIPTOR format{};
        format.nSize = sizeof(format);
        format.nVersion = 1;
        format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        format.iPixelType = PFD_TYPE_RGBA;
        format.cColorBits = 32;
        format.cDepthBits = 24;
        format.cStencilBits = 8;
        auto index = ChoosePixelFormat(dc, &format);
        require(index != 0, "GDI ChoosePixelFormat");
        require(SetPixelFormat(dc, index, &format) != FALSE, "GDI SetPixelFormat");
        context = create(dc);
        require(context != nullptr, "Mesa wglCreateContext");
        require(makeCurrent(dc, context) != FALSE, "Mesa wglMakeCurrent");
    }

    bool release() {
        bool success = true;
        if (context) {
            if (texture) { deleteTextures(1, &texture); texture = 0; }
            success = makeCurrent(nullptr, nullptr) != FALSE;
            success = (deleteContext(context) != FALSE) && success;
            context = nullptr;
        }
        if (dc && window) { success = (ReleaseDC(window, dc) != 0) && success; dc = nullptr; }
        if (window) { success = (DestroyWindow(window) != FALSE) && success; window = nullptr; }
        if (registered) { success = (UnregisterClassW(L"VulkanizerWglApiCheck", module) != FALSE) && success; registered = false; }
        if (library) { success = (FreeLibrary(library) != FALSE) && success; library = nullptr; }
        return success;
    }
    ~WglCheck() { release(); }
};
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "Usage: wgl-zink-api ABSOLUTE_PATH_TO_OPENGL32_DLL\n"; return 2; }
    try {
        WglCheck check;
        check.initialize(argv[1]);
#define GL_FUNCTION(name) auto name = resolve<decltype(&::gl##name)>(check.library, "gl" #name)
        GL_FUNCTION(GetString); GL_FUNCTION(GetError); GL_FUNCTION(Viewport);
        GL_FUNCTION(MatrixMode); GL_FUNCTION(LoadIdentity); GL_FUNCTION(ClearColor); GL_FUNCTION(Clear);
        GL_FUNCTION(GenTextures); GL_FUNCTION(BindTexture); GL_FUNCTION(TexImage2D); GL_FUNCTION(TexParameteri);
        GL_FUNCTION(Enable); GL_FUNCTION(Color4f); GL_FUNCTION(EnableClientState);
        GL_FUNCTION(VertexPointer); GL_FUNCTION(TexCoordPointer); GL_FUNCTION(DrawArrays);
        GL_FUNCTION(ReadBuffer); GL_FUNCTION(ReadPixels);
#undef GL_FUNCTION
        auto* rendererBytes = GetString(GL_RENDERER);
        if (!rendererBytes) throw std::runtime_error("GL_RENDERER is unavailable");
        std::string renderer(reinterpret_cast<char const*>(rendererBytes));
        auto lower = renderer;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.find("zink") == std::string::npos) throw std::runtime_error("WGL context did not select Zink: " + renderer);
        auto getProcAddress = resolve<decltype(&::wglGetProcAddress)>(check.library, "wglGetProcAddress");
        for (auto name : {"glCreateShader", "glShaderSource", "glCompileShader", "glCreateProgram", "glLinkProgram",
                         "glUseProgram", "glBindBuffer", "glBufferData", "glVertexAttribPointer",
                         "glEnableVertexAttribArray", "glUniformMatrix4fv", "glBindFramebuffer",
                         "glCheckFramebufferStatus", "glGenerateMipmap"}) {
            auto function = reinterpret_cast<uintptr_t>(getProcAddress(name));
            if (function <= 3 || function == static_cast<uintptr_t>(-1))
                throw std::runtime_error(std::string("Missing Cocos/GLEW entry point ") + name);
        }
        Viewport(0, 0, 64, 64);
        MatrixMode(GL_PROJECTION); LoadIdentity();
        MatrixMode(GL_MODELVIEW); LoadIdentity();
        GenTextures(1, &check.texture);
        BindTexture(GL_TEXTURE_2D, check.texture);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        std::array<unsigned char, 4> texel{230, 40, 80, 255};
        TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel.data());
        Enable(GL_TEXTURE_2D);
        Color4f(1, 1, 1, 1);
        std::array<GLfloat, 8> vertices{-.8f, -.8f, .8f, -.8f, -.8f, .8f, .8f, .8f};
        std::array<GLfloat, 8> coordinates{0, 0, 1, 0, 0, 1, 1, 1};
        EnableClientState(GL_VERTEX_ARRAY);
        EnableClientState(GL_TEXTURE_COORD_ARRAY);
        VertexPointer(2, GL_FLOAT, 0, vertices.data());
        TexCoordPointer(2, GL_FLOAT, 0, coordinates.data());
        ClearColor(.02f, .03f, .04f, 1);
        ReadBuffer(GL_BACK);
        std::array<unsigned char, 4> pixel{};
        for (unsigned frame = 0; frame < 120; ++frame) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            Clear(GL_COLOR_BUFFER_BIT);
            DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            // Readbacks are confined to this API correctness test.
            if (frame == 0 || frame == 119) {
                ReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
                for (size_t i = 0; i < 4; ++i)
                    if (std::abs(static_cast<int>(pixel[i]) - texel[i]) > 2)
                        throw std::runtime_error("Textured GL draw readback differs from the uploaded texel");
            }
            auto error = GetError();
            if (error != GL_NO_ERROR) throw std::runtime_error("GL error " + std::to_string(error));
            require(SwapBuffers(check.dc) != FALSE, "GDI SwapBuffers");
        }
        std::cout << "WGL_API_PROBE pid=" << GetCurrentProcessId() << " hwnd=0x" << std::hex
            << reinterpret_cast<uintptr_t>(check.window) << std::dec << " frames=120 renderer=" << renderer
            << " readback=" << static_cast<unsigned>(pixel[0]) << ',' << static_cast<unsigned>(pixel[1]) << ','
            << static_cast<unsigned>(pixel[2]) << " GL_error=0\n";
        require(check.release(), "WGL resource cleanup");
        return 0;
    } catch (std::exception const& error) {
        std::cerr << "WGL_API_PROBE_FAILED " << error.what() << '\n';
        return 1;
    }
}
