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

} // namespace ace::gl
