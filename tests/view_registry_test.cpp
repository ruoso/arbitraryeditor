#include <ace/dockmodel/dockmodel.hpp>
#include <ace/dockmodel/view_registry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

// The L1 view-registry headless unit suite (docs/01-architecture.md §9 :187):
// the fixed eight-type catalog, deterministic instance-id minting + parse
// round-trip, and open/close/reopen over DockLayout (D-view-registry-2/-3/-4/-6).
// UI-agnostic — no ImGui (A8 / refinement Constraint 2).
using ace::dockmodel::DockLayout;
using ace::dockmodel::parse_view_id;
using ace::dockmodel::ParsedViewId;
using ace::dockmodel::ViewRegistry;
using ace::dockmodel::ViewType;
namespace dm = ace::dockmodel;

namespace {
bool contains(const std::vector<std::string>& v, const std::string& id) {
  return std::find(v.begin(), v.end(), id) != v.end();
}
} // namespace

TEST_CASE("view_registry: the catalog is exactly the eight note-named types") {
  const auto& catalog = dm::view_catalog();
  REQUIRE(catalog.size() == 8);
  REQUIRE(dm::k_view_type_count == 8);

  // Every entry's `type` field indexes its own catalog slot (enum == order).
  for (std::size_t i = 0; i < catalog.size(); ++i) {
    CHECK(static_cast<std::size_t>(catalog[i].type) == i);
  }

  struct Expected {
    ViewType type;
    const char* slug;
    const char* title;
    bool multi;
  };
  const Expected expected[] = {
      {ViewType::Canvas, "canvas", "Canvas", true},
      {ViewType::Layers, "layers", "Layers", false},
      {ViewType::Inspector, "inspector", "Inspector", false},
      {ViewType::Overview, "overview", "Overview", false},
      {ViewType::Color, "color", "Color", false},
      {ViewType::History, "history", "History", false},
      {ViewType::Assets, "assets", "Assets", false},
      {ViewType::Export, "export", "Export", false},
  };
  for (const Expected& e : expected) {
    const dm::ViewDescriptor& d = dm::view_descriptor(e.type);
    CHECK(d.slug == e.slug);
    CHECK(d.title == e.title);
    CHECK(d.multi_instance == e.multi);
    // slug→type and type→slug round-trip.
    CHECK(dm::view_type_for_slug(e.slug) == e.type);
    CHECK(dm::view_slug(e.type) == e.slug);
    CHECK(dm::view_title(e.type) == e.title);
  }

  // Canvas is the only multi-instance type (D-view-registry-3).
  int multi_count = 0;
  for (const dm::ViewDescriptor& d : catalog) {
    multi_count += d.multi_instance ? 1 : 0;
  }
  CHECK(multi_count == 1);

  CHECK_FALSE(dm::view_type_for_slug("cameras").has_value()); // no standalone Cameras (D6)
  CHECK_FALSE(dm::view_type_for_slug("").has_value());
}

TEST_CASE("view_registry: singleton mints a bare slug, multi-instance mints monotonic slug#N") {
  ViewRegistry reg;
  // Singletons mint their bare slug, repeatably (no counter).
  CHECK(reg.mint_id(ViewType::Inspector) == "inspector");
  CHECK(reg.mint_id(ViewType::Inspector) == "inspector");
  CHECK(reg.mint_id(ViewType::Layers) == "layers");

  // Multi-instance mints a monotonic, non-recycling index.
  CHECK(reg.mint_id(ViewType::Canvas) == "canvas#1");
  CHECK(reg.mint_id(ViewType::Canvas) == "canvas#2");
  CHECK(reg.mint_id(ViewType::Canvas) == "canvas#3");
}

TEST_CASE("view_registry: parse_view_id round-trips and rejects malformed ids") {
  // Singleton: bare slug → index 0.
  CHECK(parse_view_id("inspector") == ParsedViewId{ViewType::Inspector, 0});
  CHECK(parse_view_id("export") == ParsedViewId{ViewType::Export, 0});
  // Multi-instance: slug#N → index N.
  CHECK(parse_view_id("canvas#1") == ParsedViewId{ViewType::Canvas, 1});
  CHECK(parse_view_id("canvas#42") == ParsedViewId{ViewType::Canvas, 42});

  // Minted ids round-trip through parse.
  ViewRegistry reg;
  const std::string a = reg.mint_id(ViewType::Canvas);
  const std::string b = reg.mint_id(ViewType::Canvas);
  CHECK(parse_view_id(a) == ParsedViewId{ViewType::Canvas, 1});
  CHECK(parse_view_id(b) == ParsedViewId{ViewType::Canvas, 2});

  // Malformed: unknown slug, a # on a singleton, a bare multi slug, a bad index.
  CHECK_FALSE(parse_view_id("nope").has_value());
  CHECK_FALSE(parse_view_id("inspector#1").has_value()); // # on a singleton
  CHECK_FALSE(parse_view_id("canvas").has_value());      // bare multi slug
  CHECK_FALSE(parse_view_id("canvas#0").has_value());    // zero index
  CHECK_FALSE(parse_view_id("canvas#01").has_value());   // leading zero
  CHECK_FALSE(parse_view_id("canvas#x").has_value());    // non-numeric
  CHECK_FALSE(parse_view_id("canvas#").has_value());     // missing index
  CHECK_FALSE(parse_view_id("").has_value());
}

TEST_CASE("view_registry: open seeds an empty layout and tabs into a non-empty one") {
  ViewRegistry reg;
  DockLayout layout;

  // Into an empty layout: seed a fresh root leaf holding the new id.
  const std::string first = reg.open(layout, ViewType::Inspector);
  CHECK(first == "inspector");
  REQUIRE_FALSE(layout.empty());
  CHECK(layout.valid());
  CHECK(layout.view_ids() == std::vector<std::string>{"inspector"});

  // Into a non-empty layout with no named target: tab into the last leaf.
  const std::string second = reg.open(layout, ViewType::Layers);
  CHECK(second == "layers");
  CHECK(layout.valid());
  CHECK(contains(layout.view_ids(), "layers"));
  CHECK(layout.view_ids().size() == 2);

  // A named target inserts into that view's leaf.
  const std::string third = reg.open(layout, ViewType::Overview, "inspector");
  CHECK(third == "overview");
  CHECK(layout.valid());
  CHECK(layout.contains("overview"));
}

TEST_CASE("view_registry: a singleton open is idempotent; a multi-instance open is distinct") {
  ViewRegistry reg;
  DockLayout layout;

  const std::string a = reg.open(layout, ViewType::Inspector);
  const std::string b = reg.open(layout, ViewType::Inspector); // already present
  CHECK(a == b);
  CHECK(a == "inspector");
  // No duplicate — the id appears exactly once.
  const std::vector<std::string> ids = layout.view_ids();
  CHECK(std::count(ids.begin(), ids.end(), std::string("inspector")) == 1);
  CHECK(layout.valid());

  // A multi-instance open yields a distinct new id each call — both coexist.
  const std::string c1 = reg.open(layout, ViewType::Canvas);
  const std::string c2 = reg.open(layout, ViewType::Canvas);
  CHECK(c1 == "canvas#1");
  CHECK(c2 == "canvas#2");
  CHECK(c1 != c2);
  CHECK(layout.contains("canvas#1"));
  CHECK(layout.contains("canvas#2"));
  CHECK(layout.valid());
}

TEST_CASE("view_registry: multi-instance ids never recycle across a close/open") {
  ViewRegistry reg;
  DockLayout layout;

  const std::string c1 = reg.open(layout, ViewType::Canvas); // canvas#1
  const std::string c2 = reg.open(layout, ViewType::Canvas); // canvas#2
  CHECK(reg.close(layout, c1));                              // free canvas#1
  CHECK_FALSE(layout.contains("canvas#1"));

  // Opening again mints canvas#3 — never a reused id (D-view-registry-4).
  const std::string c3 = reg.open(layout, ViewType::Canvas);
  CHECK(c3 == "canvas#3");
  CHECK(c3 != c1);
  CHECK(layout.contains("canvas#2"));
  CHECK(layout.contains("canvas#3"));
  CHECK(layout.valid());

  // Closing an absent id is a no-op false.
  CHECK_FALSE(reg.close(layout, "canvas#1"));
}

TEST_CASE("view_registry: close-everything then reopen round-trips to a valid layout") {
  ViewRegistry reg;
  DockLayout layout;
  reg.open(layout, ViewType::Inspector);
  reg.open(layout, ViewType::Layers);
  reg.open(layout, ViewType::Overview);
  REQUIRE(layout.valid());

  // Close every view — the layout empties (D18 "anything can be closed"). The
  // id list is snapshotted first (close mutates the tree it walks).
  const std::vector<std::string> all = layout.view_ids();
  for (const std::string& id : all) {
    CHECK(reg.close(layout, id));
    CHECK(layout.valid());
  }
  CHECK(layout.empty());

  // Reopen from empty seeds a fresh root leaf under the stable slug.
  const std::string re = reg.reopen(layout, ViewType::Inspector);
  CHECK(re == "inspector");
  REQUIRE_FALSE(layout.empty());
  CHECK(layout.contains("inspector"));
  CHECK(layout.valid());

  // Reopen the rest — the round-trip restores a valid multi-view layout.
  reg.reopen(layout, ViewType::Layers);
  reg.reopen(layout, ViewType::Overview);
  CHECK(layout.valid());
  CHECK(layout.contains("layers"));
  CHECK(layout.contains("overview"));
  CHECK(layout.view_ids().size() == 3);
}
