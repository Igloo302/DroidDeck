#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <string>
#include <cstdio>
#include <vector>
#include <unistd.h>
#include "../../xr-core/src/main/cpp/GpuFrames.h"

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(display, nullptr, nullptr)) return 3;
    const EGLint attrs[]{EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config{}; EGLint count{};
    if (!eglChooseConfig(display, attrs, &config, 1, &count) || !count) return 4;
    const EGLint contextAttrs[]{EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttrs);
    const EGLint surfaceAttrs[]{EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttrs);
    if (!eglMakeCurrent(display, surface, surface, context)) return 5;
    std::printf("renderer=%s\n", glGetString(GL_RENDERER)); std::fflush(stdout);
    XrGpuFrames frames;
    if (!frames.Start(argv[1], display)) return 6;
    GLuint fbo{}; glGenFramebuffers(1, &fbo);
    std::printf("asyncCompletion=%d\n", frames.AsyncCompletion()); std::fflush(stdout);
    unsigned received = 0; const auto deadline = xr_frame_now() + 30000000000LL;
    while (received < 24 && xr_frame_now() < deadline) {
        DroidDeckXrFrameHeader header{}; GLuint texture{};
        if (!frames.Take(header, texture)) { usleep(1000); continue; }
        std::vector<uint32_t> pixels(header.width * header.height);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 7;
        glReadPixels(0, 0, header.width, header.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (glGetError() != GL_NO_ERROR) return 8;
        unsigned errors[2]{};
        for (unsigned y=0; y<header.height; y++) for (unsigned x=0; x<header.width; x++) {
            uint32_t n=x*13+y*37+uint32_t(header.frame_id)*17;
            uint32_t expected=0xff000000u | ((n&255u)<<16) | (((n>>3)&255u)<<8) | ((n>>7)&255u);
            if (pixels[y*header.width+x] != expected) errors[x >= header.width/2]++;
        }
        std::printf("FRAME %llu %ux%u leftErrors=%u rightErrors=%u\n", (unsigned long long)header.frame_id,header.width,header.height,errors[0],errors[1]); std::fflush(stdout);
        if (errors[0] || errors[1]) return 9;
        received++;
    }
    frames.Stop(); glDeleteFramebuffers(1,&fbo);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroySurface(display,surface); eglDestroyContext(display,context); eglTerminate(display);
    std::printf("TRANSFER_RESULT received=%u passed=%d\n",received,received==24);
    return received==24 ? 0 : 10;
}
