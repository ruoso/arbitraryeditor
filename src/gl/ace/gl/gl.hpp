#pragma once

namespace ace::gl {

// The gl component. See docs/01-architecture.md §8 (component levelization).
const char* name();

// Minimal GLES3/WebGL2-subset draw calls the shell issues directly (A2/A3).
// Kept behind the GL seam so raw GL stays out of the L1 core; ImGui owns its own
// backend GL. `clear` sets the clear colour and clears the colour buffer.
void set_viewport(int width, int height);
void clear(float r, float g, float b, float a);

// Create a GLES3 texture from tightly-packed straight-alpha RGBA8 pixels
// (w*h*4 bytes, no stride) and return its handle (a GLuint). This is A6's
// tile→GL step — editor.canvas.view reuses the identical primitive for settled
// tiles. A GL context must be current. `destroy_texture` releases the handle.
unsigned int upload_rgba8(const void* pixels, int width, int height);

// Re-upload tightly-packed straight-alpha RGBA8 pixels into an EXISTING texture
// of the SAME dimensions (glTexSubImage2D). editor.canvas.view uses this so a
// canvas that re-renders (a later camera nudge, an edit) reuses its texture
// object instead of churning glGen/glDelete every changed frame — reallocating
// (a fresh upload_rgba8) only on a genuine pane resize. A GL context must be
// current and `texture` must be a live upload_rgba8 handle of size w*h.
void update_rgba8(unsigned int texture, const void* pixels, int width, int height);

void destroy_texture(unsigned int texture);

} // namespace ace::gl
