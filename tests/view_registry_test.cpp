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
using ace::dockmodel::view_id_less;
using ace::dockmodel::ViewRegistry;
using ace::dockmodel::ViewType;
namespace dm = ace::dockmodel;

namespace {
bool contains(const std::vector<std::string>& v, const std::string& id) {
  return std::find(v.begin(), v.end(), id) != v.end();
}

// Sort a copy through the comparator under test — the whole-set form of every ordering
// assertion below, so a case pins the ORDER rather than one lucky pair.
std::vector<std::string> sorted(std::vector<std::string> ids) {
  std::sort(ids.begin(), ids.end(),
            [](const std::string& a, const std::string& b) { return view_id_less(a, b); });
  return ids;
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

// --- editor.canvas.view_id_natural_order -------------------------------------------------
// `view_id_less`, the canonical total order over view ids (D-view_id_natural_order-1/-3). It
// is the third projection of the grammar `mint_id` writes and `parse_view_id` reads, so it is
// tested beside them — headless, no shell, no ImGui.

TEST_CASE(
    "view_registry: view_id_less orders instance indices numerically, not lexicographically") {
  // THE headline. Every pair here sorts the other way under `std::string`'s byte order, which
  // is exactly what `std::map<std::string, …>` hands `CanvasView::pane_rows()`.
  CHECK(view_id_less("canvas#2", "canvas#10"));
  CHECK_FALSE(view_id_less("canvas#10", "canvas#2"));
  CHECK(view_id_less("canvas#9", "canvas#10"));
  CHECK_FALSE(view_id_less("canvas#10", "canvas#9"));
  CHECK(view_id_less("canvas#1", "canvas#2"));
  CHECK_FALSE(view_id_less("canvas#2", "canvas#1"));
  CHECK(view_id_less("canvas#99", "canvas#100"));
  CHECK_FALSE(view_id_less("canvas#100", "canvas#99"));
  // Irreflexive on the ids that matter most.
  CHECK_FALSE(view_id_less("canvas#10", "canvas#10"));

  // The whole-set form: a comparator that only special-cases one pair cannot satisfy this.
  const std::vector<std::string> want{"canvas#1",  "canvas#2",  "canvas#3",
                                      "canvas#10", "canvas#11", "canvas#21"};
  CHECK(sorted({"canvas#10", "canvas#3", "canvas#1", "canvas#21", "canvas#2", "canvas#11"}) ==
        want);
}

TEST_CASE("view_registry: view_id_less groups by view type in catalog order") {
  // Pinned against `view_catalog()` itself rather than a hand-copied list, so a future catalog
  // reordering moves the expectation with the code it describes. One id per type: `slug#1` for
  // the multi-instance type (a bare "canvas" is not a view id at all — see the next case),
  // the bare slug for each singleton.
  std::vector<std::string> catalog_order;
  for (const dm::ViewDescriptor& d : dm::view_catalog()) {
    catalog_order.push_back(d.multi_instance ? std::string(d.slug) + "#1" : std::string(d.slug));
  }
  REQUIRE(catalog_order.size() == 8);

  std::vector<std::string> shuffled = catalog_order;
  std::reverse(shuffled.begin(), shuffled.end());
  CHECK(sorted(shuffled) != shuffled);      // anti-vacuity: the input was NOT already sorted
  CHECK(sorted(shuffled) == catalog_order); // …and sorting recovers the catalog sequence
  CHECK(sorted(catalog_order) == catalog_order);

  // Canvas is catalog entry 0, so EVERY canvas#N precedes every singleton — including one whose
  // slug sorts before "canvas" by bytes ("assets"), which is what makes this a catalog-order
  // assertion rather than an alphabetical one.
  for (const std::string& id :
       {std::string("canvas#1"), std::string("canvas#10"), std::string("canvas#100")}) {
    for (const dm::ViewDescriptor& d : dm::view_catalog()) {
      if (d.multi_instance) {
        continue;
      }
      INFO(id << " vs " << d.slug);
      CHECK(view_id_less(id, d.slug));
      CHECK_FALSE(view_id_less(d.slug, id));
    }
  }
  CHECK(view_id_less("canvas#1", "assets"));
}

TEST_CASE("view_registry: view_id_less puts unparseable ids after every parseable one, "
          "ordered by bytes") {
  // The four rejection classes `parse_view_id` already defines: a bare multi-instance slug, a
  // `#` suffix on a singleton, a bad index (zero / leading zero / non-numeric), and an unknown
  // slug (the empty string included). A `Compare` handed to `std::sort` may not have a
  // precondition its caller could violate, and `pane_rows()`'s keys come from a dock layout
  // that can be restored from a file on disk (Constraint 9).
  const std::vector<std::string> bad{"canvas",   "layers#2", "canvas#0", "canvas#01",
                                     "canvas#x", "bogus#1",  ""};
  for (const std::string& u : bad) {
    INFO(u);
    REQUIRE_FALSE(parse_view_id(u).has_value()); // the premise: these really are unparseable
    // "export" is the LAST catalog slug, so beating it means beating every parseable id.
    CHECK(view_id_less("export", u));
    CHECK_FALSE(view_id_less(u, "export"));
    CHECK(view_id_less("canvas#1", u));
    CHECK_FALSE(view_id_less(u, "canvas#1"));
  }

  // Among themselves: plain byte order (`std::string_view::operator<`).
  const std::vector<std::string> want{"",          "bogus#1",  "canvas",  "canvas#0",
                                      "canvas#01", "canvas#x", "layers#2"};
  CHECK(sorted(bad) == want);
  CHECK(view_id_less("canvas#0", "canvas#01")); // NOT the numeric reading: 0 and 01 are not ids
  CHECK(view_id_less("canvas#01", "canvas#x"));
}

TEST_CASE("view_registry: view_id_less is a strict weak ordering") {
  // Constraint 5 made mechanical. A non-SWO `Compare` is undefined behaviour inside
  // `std::sort`, i.e. a worse failure than the ordering bug this comparator fixes — so the
  // property is checked exhaustively over a corpus spanning both parseable and unparseable
  // ids (plus a duplicate) rather than argued from the implementation.
  const std::vector<std::string> corpus{
      "canvas#1",   "canvas#2",  "canvas#3", "canvas#9",  "canvas#10", "canvas#11", "canvas#21",
      "canvas#100", "canvas#2", // deliberate duplicate
      "layers",     "inspector", "overview", "color",     "history",   "assets",    "export",
      "canvas",     "layers#2",  "canvas#0", "canvas#01", "canvas#x",  "bogus#1",   ""};

  for (const std::string& a : corpus) {
    INFO(a);
    CHECK_FALSE(view_id_less(a, a)); // irreflexivity
  }
  for (const std::string& a : corpus) {
    for (const std::string& b : corpus) {
      INFO(a << " / " << b);
      if (view_id_less(a, b)) {
        CHECK_FALSE(view_id_less(b, a)); // asymmetry
      }
    }
  }
  auto incomparable = [](const std::string& a, const std::string& b) {
    return !view_id_less(a, b) && !view_id_less(b, a);
  };
  for (const std::string& a : corpus) {
    for (const std::string& b : corpus) {
      for (const std::string& c : corpus) {
        INFO(a << " / " << b << " / " << c);
        if (view_id_less(a, b) && view_id_less(b, c)) {
          CHECK(view_id_less(a, c)); // transitivity of <
        }
        if (incomparable(a, b) && incomparable(b, c)) {
          CHECK(incomparable(a, c)); // transitivity of incomparability
        }
      }
    }
  }

  // Determinism: two different permutations of the same multiset sort to the identical
  // sequence — the property `std::sort` may not deliver at all under a broken comparator.
  std::vector<std::string> reversed = corpus;
  std::reverse(reversed.begin(), reversed.end());
  CHECK(reversed != corpus); // the two inputs really do differ
  CHECK(sorted(corpus) == sorted(reversed));
  // …and the sorted sequence is itself ordered: no adjacent pair is out of order.
  const std::vector<std::string> out = sorted(corpus);
  for (std::size_t i = 1; i < out.size(); ++i) {
    INFO(out[i - 1] << " then " << out[i]);
    CHECK_FALSE(view_id_less(out[i], out[i - 1]));
  }
  CHECK(out.front() == "canvas#1");
  CHECK(out.back() == "layers#2");
}
