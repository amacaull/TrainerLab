#include "engine/model/status.hpp"

#include "engine/model/pokemon.hpp"
#include "engine/model/types.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace engine {

namespace {
constexpr std::array<std::string_view, StatusCount> kStatusNames = {
    "None", "Burn", "Poison", "Toxic", "Paralysis", "Sleep", "Freeze"};
} // namespace

std::string_view statusName(Status s) {
  int idx = static_cast<int>(s);
  if (idx < 0 || idx >= StatusCount) {
    throw std::out_of_range("statusName: index out of range");
  }
  return kStatusNames[static_cast<size_t>(idx)];
}

Status statusFromString(std::string_view s) {
  for (int i = 0; i < StatusCount; ++i) {
    if (kStatusNames[static_cast<size_t>(i)] == s) {
      return static_cast<Status>(i);
    }
  }
  throw std::invalid_argument("statusFromString: unknown status '" + std::string(s) + "'");
}

bool typeImmuneToStatus(Status s, const Species &sp) {
  auto hasType = [&sp](Type t) { return sp.type1 == t || sp.type2 == t; };
  switch (s) {
  case Status::Burn:
    return hasType(Type::Fire);
  case Status::Paralysis:
    return hasType(Type::Electric);
  case Status::Poison:
  case Status::Toxic:
    return hasType(Type::Poison) || hasType(Type::Steel);
  case Status::Freeze:
    return hasType(Type::Ice);
  default:
    return false;
  }
}

} // namespace engine
