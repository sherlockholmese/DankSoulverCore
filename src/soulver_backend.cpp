#include "backend.hpp"

#include <dlfcn.h>

#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

bool looksLikeResourceDirectory(const fs::path &path) {
  std::error_code error;
  if (!fs::is_directory(path, error))
    return false;

  return fs::is_regular_file(path / "Info.plist", error) ||
         fs::is_regular_file(path / "en.lproj" / "Version.json", error);
}

void addCandidate(std::vector<fs::path> &candidates, const fs::path &candidate) {
  if (!candidate.empty())
    candidates.push_back(candidate);
}

std::vector<std::string> splitColonList(const char *value) {
  std::vector<std::string> parts;
  if (!value)
    return parts;

  std::stringstream stream(value);
  std::string part;
  while (std::getline(stream, part, ':')) {
    if (!part.empty())
      parts.push_back(part);
  }
  return parts;
}

std::optional<fs::path> executablePath() {
  std::error_code error;
  auto path = fs::read_symlink("/proc/self/exe", error);
  if (error)
    return std::nullopt;
  return path;
}

std::optional<fs::path> locateResources(const std::optional<fs::path> &overridePath) {
  std::vector<fs::path> candidates;
  if (overridePath)
    addCandidate(candidates, *overridePath);

  if (const char *environmentPath = std::getenv("SOULVER_RESOURCES"))
    addCandidate(candidates, environmentPath);

  if (auto executable = executablePath()) {
    const auto prefix = executable->parent_path().parent_path();
    addCandidate(candidates, prefix / "share" / "soulver-core" / "resources");
    addCandidate(candidates, prefix / "share" / "soulver-cpp" / "resources");
    addCandidate(candidates, executable->parent_path() / "resources");
  }

  auto dataDirectories = splitColonList(std::getenv("XDG_DATA_DIRS"));
  if (dataDirectories.empty())
    dataDirectories = {"/usr/local/share", "/usr/share"};

  for (const auto &directory : dataDirectories) {
    addCandidate(candidates, fs::path(directory) / "soulver-core" / "resources");
    addCandidate(candidates, fs::path(directory) / "soulver-cpp" / "resources");
  }

  // Keep standard locations even when XDG_DATA_DIRS has been customized.
  addCandidate(candidates, "/usr/local/share/soulver-core/resources");
  addCandidate(candidates, "/usr/share/soulver-core/resources");
  addCandidate(candidates, "/usr/local/share/soulver-cpp/resources");
  addCandidate(candidates, "/usr/share/soulver-cpp/resources");

  for (const auto &candidate : candidates) {
    if (looksLikeResourceDirectory(candidate))
      return candidate;
  }

  return std::nullopt;
}

template <typename Function> Function requireSymbol(void *library, const char *name) {
  auto *symbol = reinterpret_cast<Function>(dlsym(library, name));
  if (!symbol)
    throw std::runtime_error(std::string("soulver-cpp is missing ") + name);
  return symbol;
}

// The wrapper is loaded at runtime so that a Numen-only install needs neither soulver-cpp nor the
// Swift runtime, and so that Numen startup never pays for loading them.
class SoulverBackend final : public Backend {
public:
  explicit SoulverBackend(const std::optional<fs::path> &resourcesOverride) {
    const char *configuredLibrary = std::getenv("SOULVER_WRAPPER_LIBRARY");
    const char *library =
        configuredLibrary && *configuredLibrary ? configuredLibrary : "libSoulverWrapper.so";

    // Deliberately never closed: the Swift runtime does not support being unloaded.
    void *handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
      throw std::runtime_error(std::string("Could not load soulver-cpp (") + dlerror() +
                               "). Install it, or switch the backend to Numen");
    }

    const auto initialize = requireSymbol<bool (*)(const char *)>(handle, "soulver_initialize");
    const auto isInitialized = requireSymbol<bool (*)()>(handle, "soulver_is_initialized");
    evaluate_ = requireSymbol<char *(*)(const char *)>(handle, "soulver_evaluate");

    const auto resources = locateResources(resourcesOverride);
    if (!resources) {
      throw std::runtime_error(
          "SoulverCore resources not found; pass --resources or set SOULVER_RESOURCES");
    }

    if (!isInitialized() && !initialize(resources->c_str()))
      throw std::runtime_error("SoulverCore initialization failed for " + resources->string());
  }

  json evaluate(const std::string &expression) override {
    char *rawResult = evaluate_(expression.c_str());
    if (!rawResult)
      throw std::runtime_error("SoulverCore returned no response");

    const std::string encodedResult(rawResult);
    std::free(rawResult);

    auto result = json::parse(encodedResult);
    if (!result.contains("error"))
      result["error"] = nullptr;
    return result;
  }

private:
  char *(*evaluate_)(const char *) = nullptr;
};

} // namespace

std::unique_ptr<Backend> createSoulverBackend(const std::optional<fs::path> &resources) {
  return std::make_unique<SoulverBackend>(resources);
}
