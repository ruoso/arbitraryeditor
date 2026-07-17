#include <ace/interact/interact.hpp>
#include <ace/scene/scene.hpp>

namespace ace::interact {

const char* name() { return "interact"; }

double brush_units(double view_fraction, double view_short_edge_units) {
  return view_fraction * view_short_edge_units;
}

} // namespace ace::interact
