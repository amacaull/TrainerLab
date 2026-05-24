#include "engine/move.hpp"

#include <stdexcept>
#include <string>

namespace engine {

MoveCategory categoryFromString(std::string_view s) {
  if (s == "Physical")
    return MoveCategory::Physical;
  if (s == "Special")
    return MoveCategory::Special;
  if (s == "Status")
    return MoveCategory::Status;
  throw std::invalid_argument("categoryFromString: unknown category '" + std::string(s) + "'");
}

std::string_view categoryName(MoveCategory c) {
  switch (c) {
  case MoveCategory::Physical:
    return "Physical";
  case MoveCategory::Special:
    return "Special";
  case MoveCategory::Status:
    return "Status";
  }
  return "?";
}

} // namespace engine
