// Headless GLES loop: one clear+draw batch and one draw-only batch per iteration, flushed.
// Prints which libgallium is loaded, the renderer, RSS growth per batch, and checks the rendered pixel.
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static long rss_kb(void) { long a, b; FILE *f = fopen("/proc/self/statm", "r"); fscanf(f, "%ld %ld", &a, &b); fclose(f); return b * sysconf(_SC_PAGESIZE) / 1024; }
static GLuint sh(GLenum t, const char *s) { GLuint x = glCreateShader(t); glShaderSource(x, 1, &s, 0); glCompileShader(x); return x; }
int main(int c, char **v) {
   long n = c > 1 ? atol(v[1]) : 100000;
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
   if (!eglInitialize(d, 0, 0)) { puts("egl init failed"); return 2; }
   eglBindAPI(EGL_OPENGL_ES_API);
   EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
   EGLContext ctx = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
   if (!eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { puts("make current failed"); return 2; }
   char line[512]; FILE *m = fopen("/proc/self/maps", "r");
   while (fgets(line, sizeof line, m)) if (strstr(line, "libgallium") && strstr(line, " 00000000 ")) { printf("lib: %s", strchr(line, '/')); break; }
   fclose(m);
   printf("renderer: %s\n", glGetString(GL_RENDERER));
   GLuint tex, fb; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
   glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { puts("fbo incomplete"); return 2; }
   GLuint p = glCreateProgram();
   glAttachShader(p, sh(GL_VERTEX_SHADER, "attribute vec2 a; void main(){gl_Position=vec4(a,0.,1.);}"));
   glAttachShader(p, sh(GL_FRAGMENT_SHADER, "precision mediump float; void main(){gl_FragColor=vec4(0.,1.,0.,1.);}"));
   glBindAttribLocation(p, 0, "a"); glLinkProgram(p); glUseProgram(p);
   static const float tri[] = {-1, -1, 3, -1, -1, 3};
   glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, tri); glEnableVertexAttribArray(0);
   glViewport(0, 0, 256, 256);
   for (long i = 0; i < 2000; i++) { glClear(GL_COLOR_BUFFER_BIT); glDrawArrays(GL_TRIANGLES, 0, 3); glFlush(); }  // warm up
   glFinish(); long r0 = rss_kb();
   for (long i = 0; i < n; i++) {
      glClearColor(1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); glDrawArrays(GL_TRIANGLES, 0, 3); glFlush();
      glDrawArrays(GL_TRIANGLES, 0, 3); glFlush();
      if (i % 2000 == 1999) glFinish();
   }
   glFinish(); long r1 = rss_kb();
   unsigned char px[4] = {0}; glReadPixels(128, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
   printf("batches: %ld  rss: %ld -> %ld kB  growth: %.1f bytes/batch\n", 2 * n, r0, r1, (r1 - r0) * 1024.0 / (2 * n));
   printf("pixel: %d,%d,%d,%d (%s)\n", px[0], px[1], px[2], px[3], px[1] == 255 && px[0] == 0 ? "correct" : "WRONG");
   return 0;
}
