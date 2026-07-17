#include <ace/base/base.hpp>
#include <ace/gl/gl.hpp>

#include <GLES3/gl3.h>

namespace ace::gl {

const char* name() { return "gl"; }

void set_viewport(int width, int height) { glViewport(0, 0, width, height); }

void clear(float r, float g, float b, float a) {
  glClearColor(r, g, b, a);
  glClear(GL_COLOR_BUFFER_BIT);
}

unsigned int upload_rgba8(const void* pixels, int width, int height) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  // Tightly packed RGBA8, no row padding (the surface span has no stride).
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  return texture;
}

void destroy_texture(unsigned int texture) {
  const GLuint handle = texture;
  glDeleteTextures(1, &handle);
}

} // namespace ace::gl
