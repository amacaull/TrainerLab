#include "engine/model/field.hpp"

#include <stdexcept>

namespace engine {

const char *weatherToString(Weather w) {
  switch (w) {
  case Weather::None:
    return "None";
  case Weather::Rain:
    return "Rain";
  case Weather::Sun:
    return "Sun";
  case Weather::Sand:
    return "Sand";
  case Weather::Snow:
    return "Snow";
  default:
    return "?";
  }
}

Weather weatherFromString(const std::string &s) {
  if (s == "Rain")
    return Weather::Rain;
  if (s == "Sun")
    return Weather::Sun;
  if (s == "Sand")
    return Weather::Sand;
  if (s == "Snow")
    return Weather::Snow;
  throw std::invalid_argument("weatherFromString: unknown weather '" + s + "'");
}

const char *hazardToString(HazardKind h) {
  switch (h) {
  case HazardKind::StealthRock:
    return "StealthRock";
  case HazardKind::Spikes:
    return "Spikes";
  case HazardKind::ToxicSpikes:
    return "ToxicSpikes";
  default:
    return "?";
  }
}

HazardKind hazardFromString(const std::string &s) {
  if (s == "StealthRock")
    return HazardKind::StealthRock;
  if (s == "Spikes")
    return HazardKind::Spikes;
  if (s == "ToxicSpikes")
    return HazardKind::ToxicSpikes;
  throw std::invalid_argument("hazardFromString: unknown hazard '" + s + "'");
}

const char *terrainName(Terrain t) {
  switch (t) {
  case Terrain::Electric:
    return "Electric";
  default:
    return "None";
  }
}

Terrain terrainFromString(const std::string &s) {
  if (s == "Electric")
    return Terrain::Electric;
  if (s == "None")
    return Terrain::None;
  throw std::invalid_argument("Unknown terrain: " + s);
}

} // namespace engine
