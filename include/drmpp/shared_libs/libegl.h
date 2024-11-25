/*
 * Copyright (c) 2024 The drmpp Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef INCLUDE_DRMPP_SHARED_LIBS_LIBEGL_H
#define INCLUDE_DRMPP_SHARED_LIBS_LIBEGL_H

#include <EGL/egl.h>

struct LibEglExports {
    LibEglExports() = default;

    explicit LibEglExports(void *lib);

    typedef void * (*EglGetProcAddress)(const char *);

    typedef EGLBoolean (*EglInitialize)(EGLDisplay dpy,
                                        EGLint *major,
                                        EGLint *minor);

    typedef EGLDisplay (*EglGetDisplay)(EGLNativeDisplayType display_id);

    typedef EGLBoolean (*EglBindAPI)(EGLenum api);

    typedef EGLBoolean (*EglGetConfigs)(EGLDisplay dpy,
                                        EGLConfig *configs,
                                        EGLint config_size,
                                        EGLint *num_config);

    typedef EGLBoolean (*EglGetConfigAttrib)(EGLDisplay dpy,
                                             EGLConfig config,
                                             EGLint attribute,
                                             EGLint *value);

    typedef EGLBoolean (*EglChooseConfig)(EGLDisplay dpy,
                                          const EGLint *attrib_list,
                                          EGLConfig *configs,
                                          EGLint config_size,
                                          EGLint *num_config);

    typedef EGLContext (*EglCreateContext)(EGLDisplay dpy,
                                           EGLConfig config,
                                           EGLContext share_context,
                                           const EGLint *attrib_list);

    typedef EGLSurface (*EglCreateWindowSurface)(EGLDisplay dpy,
                                                 EGLConfig config,
                                                 EGLNativeWindowType win,
                                                 const EGLint *attrib_list);

    typedef EGLBoolean (*EglMakeCurrent)(EGLDisplay dpy,
                                         EGLSurface draw,
                                         EGLSurface read,
                                         EGLContext ctx);

    typedef EGLBoolean (*EglDestroySurface)(EGLDisplay dpy, EGLSurface surface);

    typedef EGLBoolean (*EglDestroyContext)(EGLDisplay dpy, EGLContext ctx);

    typedef EGLBoolean (*EglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);

    typedef EGLBoolean (*EglTerminate)(EGLDisplay dpy);

    EglGetProcAddress GetProcAddress = nullptr;
    EglInitialize Initialize = nullptr;
    EglGetDisplay GetDisplay = nullptr;
    EglBindAPI BindAPI = nullptr;
    EglGetConfigs GetConfigs = nullptr;
    EglGetConfigAttrib GetConfigAttrib = nullptr;
    EglChooseConfig ChooseConfig = nullptr;
    EglCreateContext CreateContext = nullptr;
    EglCreateWindowSurface CreateWindowSurface = nullptr;
    EglMakeCurrent MakeCurrent = nullptr;
    EglDestroySurface DestroySurface = nullptr;
    EglDestroyContext DestroyContext = nullptr;
    EglSwapBuffers SwapBuffers = nullptr;
    EglTerminate Terminate = nullptr;
};

class egl {
public:
    static bool IsPresent(const char *library_path = nullptr) {
        return loadExports(library_path) != nullptr;
    }

    LibEglExports *operator->() const;

private:
    static LibEglExports *loadExports(const char *library_path);
};

extern egl egl;

#endif  // INCLUDE_DRMPP_SHARED_LIBS_LIBEGL_H
