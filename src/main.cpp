#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

extern "C" {
bool soulver_initialize(const char *resourcesPath);
bool soulver_is_initialized();
char *soulver_evaluate(const char *expression);
}

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr std::string_view kVersion = "0.1.0";
constexpr std::size_t kMaximumRequestBytes = 64 * 1024;

struct Options {
  bool server = false;
  bool jsonOutput = false;
  bool help = false;
  bool version = false;
  std::optional<fs::path> resources;
  std::string expression;
};

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

json errorPayload(std::string message, int id = 0, std::string expression = {}) {
  return {
      {"id", id},
      {"expression", std::move(expression)},
      {"value", ""},
      {"type", "error"},
      {"error", std::move(message)},
  };
}

json calculate(const std::string &expression, int id) {
  char *rawResult = soulver_evaluate(expression.c_str());
  if (!rawResult)
    return errorPayload("SoulverCore returned no response", id, expression);

  std::string encodedResult(rawResult);
  std::free(rawResult);

  try {
    auto result = json::parse(encodedResult);
    result["id"] = id;
    result["expression"] = expression;
    if (!result.contains("error"))
      result["error"] = nullptr;
    return result;
  } catch (const std::exception &error) {
    return errorPayload(std::string("Invalid SoulverCore response: ") + error.what(), id,
                        expression);
  }
}

void writeJson(const json &value) {
  std::cout << value.dump() << '\n' << std::flush;
}

int runServer(const fs::path &resources) {
  writeJson({{"event", "ready"}, {"resource_path", resources.string()}});

  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty())
      continue;

    if (line.size() > kMaximumRequestBytes) {
      writeJson(errorPayload("Request is too large"));
      continue;
    }

    try {
      const auto request = json::parse(line);
      const int id = request.value("id", 0);
      const std::string expression = request.value("expression", std::string{});
      writeJson(calculate(expression, id));
    } catch (const std::exception &error) {
      writeJson(errorPayload(std::string("Invalid request: ") + error.what()));
    }
  }

  return 0;
}

std::string joinArguments(const std::vector<std::string> &arguments) {
  std::ostringstream result;
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    if (index)
      result << ' ';
    result << arguments[index];
  }
  return result.str();
}

Options parseOptions(int argc, char **argv) {
  Options options;
  std::vector<std::string> expressionParts;

  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--server") {
      options.server = true;
    } else if (argument == "--json") {
      options.jsonOutput = true;
    } else if (argument == "--resources") {
      if (++index >= argc)
        throw std::runtime_error("--resources requires a path");
      options.resources = fs::path(argv[index]);
    } else if (argument == "--help" || argument == "-h") {
      options.help = true;
    } else if (argument == "--version") {
      options.version = true;
    } else if (argument.starts_with('-')) {
      throw std::runtime_error("unknown option: " + argument);
    } else {
      expressionParts.push_back(argument);
    }
  }

  options.expression = joinArguments(expressionParts);
  return options;
}

void printHelp() {
  std::cout
      << "Usage: dank-soulver-core [OPTIONS] [EXPRESSION]\n\n"
      << "Evaluate natural-language calculations with SoulverCore.\n\n"
      << "  --server          read JSON requests from stdin, one per line\n"
      << "  --json            print the one-shot result as JSON\n"
      << "  --resources PATH  override SoulverCore's resource directory\n"
      << "  --version         print version information\n"
      << "  -h, --help        show this help\n";
}

} // namespace

int main(int argc, char **argv) {
  std::signal(SIGPIPE, SIG_IGN);

  Options options;
  try {
    options = parseOptions(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "dank-soulver-core: " << error.what() << '\n';
    return 2;
  }

  if (options.help) {
    printHelp();
    return 0;
  }
  if (options.version) {
    std::cout << "dank-soulver-core " << kVersion << '\n';
    return 0;
  }

  const auto resources = locateResources(options.resources);
  if (!resources) {
    const std::string message =
        "SoulverCore resources not found; pass --resources or set SOULVER_RESOURCES";
    if (options.server)
      writeJson({{"event", "error"}, {"error", message}});
    else
      std::cerr << "dank-soulver-core: " << message << '\n';
    return 2;
  }

  if (!soulver_is_initialized() && !soulver_initialize(resources->c_str())) {
    const std::string message = "SoulverCore initialization failed for " + resources->string();
    if (options.server)
      writeJson({{"event", "error"}, {"error", message}});
    else
      std::cerr << "dank-soulver-core: " << message << '\n';
    return 2;
  }

  if (options.server)
    return runServer(*resources);

  if (options.expression.empty()) {
    std::cerr << "dank-soulver-core: missing expression\n";
    return 2;
  }

  const auto result = calculate(options.expression, 1);
  if (options.jsonOutput) {
    writeJson(result);
  } else if (!result.contains("error") || result["error"].is_null() ||
             (result["error"].is_string() && result["error"].get<std::string>().empty())) {
    std::cout << result.value("value", std::string{}) << '\n';
  } else {
    const std::string message = result["error"].is_string()
                                    ? result["error"].get<std::string>()
                                    : "evaluation failed";
    std::cerr << "dank-soulver-core: " << message << '\n';
    return 1;
  }

  return 0;
}
