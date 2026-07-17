#pragma once

namespace ace::gl {

// The gl component. See docs/01-architecture.md §8 (component levelization).
const char* name();

// Minimal GLES3/WebGL2-subset draw calls the shell issues directly (A2/A3).
// Kept behind the GL seam so raw GL stays out of the L1 core; ImGui owns its own
// backend GL. `clear` sets the clear colour and clears the colour buffer.
void set_viewport(int width, int height);
void clear(float r, float g, float b, float a);

} // namespace ace::gl
