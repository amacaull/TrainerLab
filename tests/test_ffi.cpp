#include "engine/core/data_loader.hpp"
#include "engine/ffi/ffi.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <iomanip>
#include <set>
#include <stdexcept>
#include <string>

using namespace engine::ffi;

namespace {

// engine_init installs a process-wide singleton (D7) and Catch2 randomises
// test order, so every case warms it up itself. That is exactly what the
// idempotence contract is for.
void ensureInit() { engine_init(BATTLE_ENGINE_DATA_DIR); }

// Returns what() instead of the exception so the prefix can be asserted
// without pulling in the string matchers.
template <typename F> std::string messageOf(F &&f) {
  try {
    f();
  } catch (const std::exception &e) {
    return e.what();
  }
  return {};
}

bool startsWith(const std::string &s, const char *prefix) {
  return s.rfind(prefix, 0) == 0;
}

} // namespace

TEST_CASE("engine_init is idempotent and rejects a second data dir", "[ffi][init]") {
  ensureInit();
  REQUIRE(engine_is_initialised());
  REQUIRE(engine_data_dir() == std::string(BATTLE_ENGINE_DATA_DIR));

  SECTION("re-initialising with the same path is a no-op") {
    REQUIRE_NOTHROW(engine_init(BATTLE_ENGINE_DATA_DIR));
    REQUIRE_NOTHROW(engine_init(BATTLE_ENGINE_DATA_DIR));
    REQUIRE(engine_data_dir() == std::string(BATTLE_ENGINE_DATA_DIR));
  }

  SECTION("a different path is refused, and the loaded catalog survives") {
    const std::string msg = messageOf([] { engine_init("/definitely/not/a/data/dir"); });
    INFO(msg);
    REQUIRE(startsWith(msg, "E_INIT:"));
    REQUIRE(engine_data_dir() == std::string(BATTLE_ENGINE_DATA_DIR));
    REQUIRE(species_count() > 0);
  }
}

TEST_CASE("a missing data dir is reported, not crashed on", "[ffi][init]") {
  // engine_init's E_DATA path cannot be exercised once the singleton is warm,
  // and Catch2 randomises order - so the wrapped behaviour is asserted on the
  // loader itself. The E_DATA prefix is covered by the Rust integration tests.
  engine::DataLoader local;
  REQUIRE_THROWS_AS(local.loadAll("/definitely/not/a/data/dir"), std::runtime_error);
}

TEST_CASE("catalog sizes match the shipped content", "[ffi][catalog]") {
  ensureInit();
  // Fixture moves live outside data/ (ADR #49): engine_init never loads them,
  // so these are the numbers Rust will cache.
  REQUIRE(species_count() == 49);
  REQUIRE(move_count() == 95);
  REQUIRE(item_count() == 13);   // ADR #45
  REQUIRE(ability_count() == 45); // ADR #51
}

TEST_CASE("every id round-trips through its name", "[ffi][catalog]") {
  ensureInit();

  SECTION("species, by id_string and not by display name") {
    for (int i = 0; i < species_count(); ++i) {
      const std::string id_string = species_id_string(i);
      INFO("species " << i << " = " << id_string);
      REQUIRE_FALSE(id_string.empty());
      REQUIRE(find_species_id(id_string) == i);
      REQUIRE_FALSE(species_display_name(i).empty());
    }
  }

  SECTION("moves") {
    for (int i = 0; i < move_count(); ++i) {
      const std::string name = move_name(i);
      INFO("move " << i << " = " << name);
      REQUIRE(find_move_id(name) == i);
    }
  }

  SECTION("items") {
    for (int i = 0; i < item_count(); ++i) {
      const std::string name = item_name(i);
      INFO("item " << i << " = " << name);
      REQUIRE(find_item_id(name) == i);
    }
  }

  SECTION("abilities") {
    for (int i = 0; i < ability_count(); ++i) {
      const std::string name = ability_name(i);
      INFO("ability " << i << " = " << name);
      REQUIRE(find_ability_id(name) == i);
    }
  }
}

TEST_CASE("lookups return -1 rather than throwing", "[ffi][catalog]") {
  ensureInit();
  // A miss is -1, not an exception. The Result<i32> these get in the bridge
  // is for E_INIT only.
  REQUIRE(find_species_id("NoSuchPokemon") == -1);
  REQUIRE(find_move_id("NoSuchMove") == -1);
  REQUIRE(find_item_id("NoSuchItem") == -1);
  REQUIRE(find_ability_id("NoSuchAbility") == -1);
  REQUIRE(find_species_id("") == -1);
  REQUIRE(find_move_id("") == -1);
}

TEST_CASE("an out-of-range id is a caller bug and throws E_ARG", "[ffi][catalog]") {
  ensureInit();
  for (const std::string &msg : {
           messageOf([] { return species_id_string(-1); }),
           messageOf([] { return species_id_string(species_count()); }),
           messageOf([] { return species_display_name(species_count()); }),
           messageOf([] { return move_name(-1); }),
           messageOf([] { return move_name(move_count()); }),
           messageOf([] { return item_name(item_count()); }),
           messageOf([] { return ability_name(ability_count()); }),
       }) {
    INFO(msg);
    REQUIRE(startsWith(msg, "E_ARG:"));
  }
}

TEST_CASE("ids stay dense and stable across the boundary", "[ffi][catalog]") {
  ensureInit();
  // Sanity anchors: if data/ gains or loses a file, these move. That is the
  // whole point of ADR #58 (catalog fingerprint) - the ids are only valid for
  // one shape of data/.
  REQUIRE(find_species_id(species_id_string(0)) == 0);
  REQUIRE(find_species_id(species_id_string(species_count() - 1)) == species_count() - 1);
  REQUIRE(find_ability_id("Blaze") == 0);
  REQUIRE(find_item_id("LifeOrb") == 0);
}

TEST_CASE("the Struggle sentinels are distinct from 'no name'", "[ffi][catalog]") {
  ensureInit();
  // Struggle is hardcoded (ADR #35): it has no catalog entry, so a lookup
  // legitimately misses and the flattener must not treat that as an error.
  REQUIRE(find_move_id("Struggle") == -1);
  REQUIRE(find_ability_id("StruggleRecoil") == -1);

  REQUIRE(struggle_move_id() == kFfiStruggle);
  REQUIRE(struggle_recoil_ability_id() == kFfiStruggleRecoil);

  std::set<int> reserved{kFfiNoName, kFfiStruggle, kFfiStruggleRecoil};
  REQUIRE(reserved.size() == 3);
  REQUIRE(kFfiStruggle < kFfiNoName);
  REQUIRE(kFfiStruggleRecoil < kFfiStruggle);
}

TEST_CASE("the catalog fingerprint is stable and content-sensitive", "[ffi][catalog]") {
  ensureInit();

  SECTION("it does not vary between calls") {
    REQUIRE(catalog_fingerprint() == catalog_fingerprint());
    REQUIRE(catalog_fingerprint() != 0);
  }

  SECTION("it locks the shipped catalog") {
    // 49 species / 95 moves / 13 items / 45 abilities.
    //
    // Updating this constant must be a deliberate act: when it moves, every
    // BattleState already in the database is reinterpreted (ADR #58, ADR #12).
    // If this fails after adding content, that is the question to answer
    // before touching the number - not a value to refresh on reflex.
    constexpr uint64_t kShippedCatalog = 0x9848D2D3F76497E5ULL;
    INFO("actual fingerprint: 0x" << std::hex << catalog_fingerprint());
    REQUIRE(catalog_fingerprint() == kShippedCatalog);
  }
}

TEST_CASE("the flat structs default to 'absent' rather than to zero", "[ffi][convert]") {
  // A default-constructed FfiEvent must not look like "side 0, slot 0, move 0".
  FfiEvent e{};
  REQUIRE(e.side == -1);
  REQUIRE(e.slot == -1);
  REQUIRE(e.name_id == kFfiNoName);
  REQUIRE(e.i0 == 0);
  REQUIRE(e.flags == 0);

  FfiAction a{};
  REQUIRE(a.pivot_target == -1); // -1 = auto, never slot 0

  REQUIRE(kFfiFlagStab == 1);
  REQUIRE(kFfiFlagCrit == 2);
}
