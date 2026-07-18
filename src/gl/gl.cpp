#include <ace/base/base.hpp>
#include <ace/gl/gl.hpp>

#include <GLES3/gl3.h>

namespace ace::gl {

const char* name() { return "gl"; }

namespace {

// Pin the unpack pixel-store state to a tightly-packed, zero-origin source before every
// texture upload. GL_UNPACK_* is GLOBAL context state, and ImGui's OpenGL3 backend
// leaves GL_UNPACK_ROW_LENGTH set (non-zero) from its own uploads. A stale ROW_LENGTH
// makes the driver compute the source row stride as ROW_LENGTH*4 instead of width*4, so
// it reads far past the end of our exactly-sized w*h*4 buffer (a heap-buffer-overflow
// ASan traps and a release-build SIGSEGV under Mesa's llvmpipe when the over-read hits an
// unmapped page) AND shears the image. Resetting these here — not once at init — is the
// only robust fix, since ImGui re-dirties the state every frame it renders.
void set_tight_unpack() {
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
}

} // namespace

void set_viewport(int width, int height) { glViewport(0, 0, width, height); }

void clear(float r, float g, float b, float a) {
  glClearColor(r, g, b, a);
  glClear(GL_COLOR_BUFFER_BIT);
}

unsigned int upload_rgba8(const void* pixels, int width, int height) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  set_tight_unpack();
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  return texture;
}

void update_rgba8(unsigned int texture, const void* pixels, int width, int height) {
  glBindTexture(GL_TEXTURE_2D, texture);
  set_tight_unpack();
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void destroy_texture(unsigned int texture) {
  const GLuint handle = texture;
  glDeleteTextures(1, &handle);
}

} // namespace ace::gl
