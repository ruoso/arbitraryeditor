#include <ace/app/export_wiring.hpp>
#include <ace/commands/app_state.hpp>
#include <ace/render/render.hpp>

#include <optional>

namespace ace::app {

commands::RenderFn make_export_renderer(commands::AppState& app_state) {
  return [&app_state](const arbc::DocStatePtr& pin, const arbc::Affine& camera, int width,
                      int height,
                      const std::optional<commands::Rgba8>& background) -> render::Srgb8Image {
    if (!background) {
      return render::render_document_srgb8_pinned(app_state.document(), pin, width, height, camera);
    }
    return render::render_document_srgb8_over_pinned(
        app_state.document(), pin, width, height, camera,
        {background->r, background->g, background->b, background->a});
  };
}

} // namespace ace::app
