#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

class Backend {
public:
  virtual ~Backend() = default;

  // Returns an object holding `value`, `type` and `error`. A `type` of "none" means the expression
  // was understood but has no useful answer. Throws if the engine itself misbehaves.
  virtual nlohmann::json evaluate(const std::string &expression) = 0;
};

// Both factories throw std::runtime_error, with a user-facing message, when the engine is unusable.
std::unique_ptr<Backend> createSoulverBackend(const std::optional<std::filesystem::path> &resources);

// With waitForRates, evaluation blocks on any rates download it starts. One-shot runs need this:
// they exit straight after answering, which would otherwise abandon the download.
std::unique_ptr<Backend> createNumenBackend(bool waitForRates);
