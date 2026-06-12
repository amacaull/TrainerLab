#include "engine/status.hpp"

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

} // namespace engine
