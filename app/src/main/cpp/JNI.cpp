#include "AndroidGame.hpp"
#include "GLESRenderer.hpp"
#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <sstream>

namespace {
jclass rasterizerClass{};
jmethodID rasterizeMethod{};
struct EglFailure : std::runtime_error {
    EGLint code;
    EglFailure(const std::string &text, EGLint value) : std::runtime_error(text), code(value) {}
};
void fail(JNIEnv *env, const std::exception &e) {
    __android_log_print(ANDROID_LOG_ERROR, "G1Awakening", "%s", e.what());
    if (!env->ExceptionCheck()) {
        auto c = env->FindClass("java/lang/IllegalStateException");
        if (c)
            env->ThrowNew(c, e.what());
    }
}
std::string stringValue(JNIEnv *env, jstring value) {
    if (!value)
        throw std::invalid_argument("Required path missing");
    const char *p = env->GetStringUTFChars(value, nullptr);
    if (!p)
        throw std::runtime_error("Unable to read Java path");
    std::string s(p);
    env->ReleaseStringUTFChars(value, p);
    return s;
}
g1::RgbaImage rasterize(JNIEnv *env, const std::string &text, float pixels) {
    jstring input = env->NewStringUTF(text.c_str());
    if (!input)
        throw std::runtime_error("Unable to allocate platform text");
    auto array = static_cast<jintArray>(
        env->CallStaticObjectMethod(rasterizerClass, rasterizeMethod, input, pixels));
    env->DeleteLocalRef(input);
    if (env->ExceptionCheck() || !array)
        throw std::runtime_error("Android text rasterization failed");
    const auto count = env->GetArrayLength(array);
    if (count < 3) {
        env->DeleteLocalRef(array);
        throw std::runtime_error("Invalid text raster payload");
    }
    jint size[2];
    env->GetIntArrayRegion(array, 0, 2, size);
    if (size[0] <= 0 || size[1] <= 0 || size[0] > 16384 || size[1] > 4096 ||
        std::int64_t(size[0]) * size[1] + 2 != count) {
        env->DeleteLocalRef(array);
        throw std::runtime_error("Unbounded text raster payload");
    }
    std::vector<jint> argb(count - 2);
    env->GetIntArrayRegion(array, 2, count - 2, argb.data());
    env->DeleteLocalRef(array);
    g1::RgbaImage image{std::uint32_t(size[0]), std::uint32_t(size[1]), {}};
    image.pixels.resize(argb.size() * 4);
    for (std::size_t i = 0; i < argb.size(); ++i) {
        auto c = std::uint32_t(argb[i]);
        image.pixels[4 * i] = (c >> 16) & 255;
        image.pixels[4 * i + 1] = (c >> 8) & 255;
        image.pixels[4 * i + 2] = c & 255;
        image.pixels[4 * i + 3] = c >> 24;
    }
    return image;
}
struct Host {
    std::unique_ptr<g1::android::AndroidGame> game;
    EGLDisplay display{EGL_NO_DISPLAY};
    EGLContext context{EGL_NO_CONTEXT};
    EGLSurface surface{EGL_NO_SURFACE};
    EGLConfig config{};
    ANativeWindow *window{};
    bool graphics{};
    unsigned consecutiveRecoveries{};
    ~Host() {
        if (game && graphics) {
            const bool current =
                surface != EGL_NO_SURFACE && eglMakeCurrent(display, surface, surface, context);
            try {
                game->detachGraphics(!current);
            } catch (...) {
            }
            graphics = false;
        }
        game.reset();
        destroySurface();
        if (context != EGL_NO_CONTEXT)
            eglDestroyContext(display, context);
        if (display != EGL_NO_DISPLAY)
            eglTerminate(display);
    }
    EglFailure eglError(const char *label) {
        const auto error = eglGetError();
        std::ostringstream out;
        out << label << " (EGL 0x" << std::hex << error << ')';
        return EglFailure(out.str(), error);
    }
    void init() {
        if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
            return;
        try {
            display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
            if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr))
                throw eglError("Cannot initialize Android EGL");
            if (!eglBindAPI(EGL_OPENGL_ES_API))
                throw eglError("Cannot bind GLES");
            const EGLint attributes[] = {EGL_SURFACE_TYPE,
                                         EGL_WINDOW_BIT,
                                         EGL_RENDERABLE_TYPE,
                                         EGL_OPENGL_ES3_BIT_KHR,
                                         EGL_RED_SIZE,
                                         8,
                                         EGL_GREEN_SIZE,
                                         8,
                                         EGL_BLUE_SIZE,
                                         8,
                                         EGL_ALPHA_SIZE,
                                         8,
                                         EGL_DEPTH_SIZE,
                                         24,
                                         EGL_NONE};
            EGLint n = 0;
            if (!eglChooseConfig(display, attributes, &config, 1, &n) || n != 1)
                throw eglError("No GLES3 framebuffer configuration");
            const EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
            context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx);
            if (context == EGL_NO_CONTEXT)
                throw eglError("Cannot create GLES3 context");
        } catch (...) {
            if (context != EGL_NO_CONTEXT)
                eglDestroyContext(display, context);
            if (display != EGL_NO_DISPLAY)
                eglTerminate(display);
            context = EGL_NO_CONTEXT;
            display = EGL_NO_DISPLAY;
            config = nullptr;
            throw;
        }
    }
    void destroySurface() {
        if (display != EGL_NO_DISPLAY) {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (surface != EGL_NO_SURFACE)
                eglDestroySurface(display, surface);
        }
        surface = EGL_NO_SURFACE;
        if (window)
            ANativeWindow_release(window);
        window = nullptr;
    }
    void createSurface() {
        if (!window)
            throw std::runtime_error("Android surface has no native window");
        EGLint format = 0;
        if (!eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format))
            throw eglError("Cannot read native surface format");
        if (ANativeWindow_setBuffersGeometry(window, 0, 0, format) != 0)
            throw eglError("Cannot configure native surface");
        surface = eglCreateWindowSurface(display, config, window, nullptr);
        if (surface == EGL_NO_SURFACE || !eglMakeCurrent(display, surface, surface, context))
            throw eglError("Cannot attach Android game surface");
        eglSwapInterval(display, 1);
        if (!graphics) {
            try {
                game->attachGraphics();
                graphics = true;
            } catch (...) {
                game->detachGraphics(false);
                throw;
            }
        }
    }
    void recoverContext() {
        game->presented(false);
        if (++consecutiveRecoveries > 2)
            throw std::runtime_error("Android graphics context repeatedly lost during recovery");
        // Preserve the CPU game/session and its pending presentation gates. Every
        // GL object is rebuilt from the pinned assets; a lost frame receives no ack.
        if (graphics) {
            game->detachGraphics(true);
            graphics = false;
        }
        ANativeWindow *retained = window;
        if (retained)
            ANativeWindow_acquire(retained);
        destroySurface();
        if (context != EGL_NO_CONTEXT)
            eglDestroyContext(display, context);
        context = EGL_NO_CONTEXT;
        if (display != EGL_NO_DISPLAY)
            eglTerminate(display);
        display = EGL_NO_DISPLAY;
        config = nullptr;
        window = retained;
        init();
        createSurface();
        __android_log_print(ANDROID_LOG_INFO, "G1Awakening",
                            "Recreated lost GLES context; game state retained");
    }
    void attach(JNIEnv *env, jobject value) {
        destroySurface();
        init();
        window = ANativeWindow_fromSurface(env, value);
        try {
            createSurface();
        } catch (const EglFailure &error) {
            if (error.code == EGL_CONTEXT_LOST)
                recoverContext();
            else
                throw;
        }
    }
    void render(std::uint64_t delta) {
        if (surface == EGL_NO_SURFACE)
            return;
        if (!eglMakeCurrent(display, surface, surface, context)) {
            const auto error = eglGetError();
            if (error == EGL_CONTEXT_LOST) {
                recoverContext();
                return;
            }
            throw std::runtime_error("Cannot make game surface current: " + std::to_string(error));
        }
        try {
            game->draw(delta);
        } catch (const g1::GLContextLost &) {
            recoverContext();
            return;
        }
        // Fence completion plus successful queueing precede core presentation
        // receipts. Pause/surface boundaries invalidate their epoch in the game.
        auto fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (!fence) {
            game->presented(false);
            if (glGetError() == 0x0507) {
                recoverContext();
                return;
            }
            throw std::runtime_error("GPU completion fence unavailable");
        }
        glFlush();
        const auto waited = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ULL);
        glDeleteSync(fence);
        const auto err = glGetError();
        if (err == 0x0507) {
            recoverContext();
            return;
        }
        if (waited != GL_ALREADY_SIGNALED && waited != GL_CONDITION_SATISFIED) {
            game->presented(false);
            throw std::runtime_error("GPU frame did not complete");
        }
        if (err != GL_NO_ERROR) {
            game->presented(false);
            throw std::runtime_error("GLES frame failed: " + std::to_string(err));
        }
        if (!eglSwapBuffers(display, surface)) {
            const auto error = eglGetError();
            game->presented(false);
            if (error == EGL_CONTEXT_LOST) {
                recoverContext();
                return;
            }
            throw std::runtime_error("Android buffer swap failed: " + std::to_string(error));
        }
        game->presented(true);
        consecutiveRecoveries = 0;
    }
};
Host &host(jlong handle) {
    if (!handle)
        throw std::invalid_argument("Native game handle missing");
    return *reinterpret_cast<Host *>(handle);
}
} // namespace
extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *) {
    JNIEnv *env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;
    auto local = env->FindClass("com/iammrmikeman/g1awakening/TextRasterizer");
    if (!local)
        return JNI_ERR;
    rasterizerClass = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    rasterizeMethod =
        env->GetStaticMethodID(rasterizerClass, "rasterizeText", "(Ljava/lang/String;F)[I");
    return rasterizeMethod ? JNI_VERSION_1_6 : JNI_ERR;
}
#define G1_JNI(name) Java_com_iammrmikeman_g1awakening_NativeBridge_##name
extern "C" JNIEXPORT jlong JNICALL G1_JNI(nativeCreate)(JNIEnv *env, jclass, jstring assets,
                                                        jstring saves, jboolean modern) {
    try {
        auto h = std::make_unique<Host>();
        h->game = std::make_unique<g1::android::AndroidGame>(stringValue(env, assets),
                                                             stringValue(env, saves), modern);
        h->game->setTextRasterizer(
            [env](const std::string &s, float px) { return rasterize(env, s, px); });
        return reinterpret_cast<jlong>(h.release());
    } catch (const std::exception &e) {
        fail(env, e);
        return 0;
    }
}
extern "C" JNIEXPORT void JNICALL G1_JNI(nativeSurfaceCreated)(JNIEnv *env, jclass, jlong h,
                                                               jobject surface) {
    try {
        host(h).attach(env, surface);
    } catch (const std::exception &e) {
        fail(env, e);
    }
}
extern "C" JNIEXPORT void JNICALL G1_JNI(nativeSurfaceDestroyed)(JNIEnv *env, jclass, jlong h) {
    try {
        host(h).game->pause();
        host(h).destroySurface();
    } catch (const std::exception &e) {
        fail(env, e);
    }
}
extern "C" JNIEXPORT void JNICALL G1_JNI(nativeResize)(JNIEnv *env, jclass, jlong h, jint w,
                                                       jint height, jint l, jint t, jint r,
                                                       jint b) {
    try {
        host(h).game->resize(w, height, l, t, r, b);
    } catch (const std::exception &e) {
        fail(env, e);
    }
}
extern "C" JNIEXPORT void JNICALL G1_JNI(nativeRender)(JNIEnv *env, jclass, jlong h,
                                                       jlong elapsed) {
    try {
        host(h).render(elapsed > 0 ? std::uint64_t(elapsed) : 0);
    } catch (const std::exception &e) {
        fail(env, e);
    }
}
extern "C" JNIEXPORT void JNICALL G1_JNI(nativeTouch)(JNIEnv *env, jclass, jlong h, jint action,
                                                      jfloat x, jfloat y, jfloat dx, jfloat dy,
                                                      jfloat scale) {
    try {
        host(h).game->touch(action, x, y, dx, dy, scale);
    } catch (const std::exception &e) {
        fail(env, e);
    }
}
extern "C" JNIEXPORT jboolean JNICALL G1_JNI(nativeBack)(JNIEnv *env, jclass, jlong h) {
    try {
        return host(h).game->back();
    } catch (const std::exception &e) {
        fail(env, e);
        return true;
    }
}
extern "C" JNIEXPORT void JNICALL G1_JNI(nativePause)(JNIEnv *env, jclass, jlong h) {
    try {
        host(h).game->pause();
    } catch (const std::exception &e) {
        fail(env, e);
    }
}
extern "C" JNIEXPORT void JNICALL G1_JNI(nativeResume)(JNIEnv *env, jclass, jlong h) {
    try {
        host(h).game->resume();
    } catch (const std::exception &e) {
        fail(env, e);
    }
}
extern "C" JNIEXPORT void JNICALL G1_JNI(nativeAudioFocus)(JNIEnv *env, jclass, jlong h,
                                                           jboolean focused) {
    try {
        host(h).game->audioFocus(focused);
    } catch (const std::exception &e) {
        fail(env, e);
    }
}
extern "C" JNIEXPORT void JNICALL G1_JNI(nativeResourcePressure)(JNIEnv *env, jclass, jlong h,
                                                                 jboolean lowPower,
                                                                 jboolean thermalSerious,
                                                                 jboolean memoryPressure) {
    try {
        host(h).game->resourcePressure(lowPower, thermalSerious, memoryPressure);
    } catch (const std::exception &e) {
        fail(env, e);
    }
}
extern "C" JNIEXPORT jstring JNICALL G1_JNI(nativeAccessibilitySnapshot)(JNIEnv *env, jclass,
                                                                         jlong h, jboolean force) {
    try {
        const auto snapshot = host(h).game->accessibilitySnapshot(force);
        return snapshot ? env->NewStringUTF(snapshot->c_str()) : nullptr;
    } catch (const std::exception &e) {
        fail(env, e);
        return nullptr;
    }
}
extern "C" JNIEXPORT void JNICALL G1_JNI(nativeDestroy)(JNIEnv *env, jclass, jlong h) {
    try {
        delete reinterpret_cast<Host *>(h);
    } catch (const std::exception &e) {
        fail(env, e);
    }
}
