#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "backend.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr std::string_view kVersion = DANK_SOULVER_CORE_VERSION;
constexpr std::size_t kMaximumRequestBytes = 64 * 1024;

struct Options {
  bool server = false;
  bool jsonOutput = false;
  bool help = false;
  bool version = false;
  std::string backend = "soulver";
  std::optional<fs::path> resources;
  std::string expression;
};

json errorPayload(std::string message, int id = 0, std::string expression = {}) {
  return {
      {"id", id},
      {"expression", std::move(expression)},
      {"value", ""},
      {"type", "error"},
      {"error", std::move(message)},
  };
}

json calculate(Backend &backend, std::string_view backendName, const std::string &expression,
               int id) {
  json result;
  try {
    result = backend.evaluate(expression);
  } catch (const std::exception &error) {
    result = errorPayload(std::string("Evaluation failed: ") + error.what());
  }

  result["id"] = id;
  result["expression"] = expression;
  result["backend"] = backendName;
  return result;
}

void writeJson(const json &value) {
  std::cout << value.dump() << '\n' << std::flush;
}

int runServer(Backend &backend, std::string_view backendName) {
  writeJson({{"event", "ready"}, {"backend", backendName}});

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
      writeJson(calculate(backend, backendName, expression, id));
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
    } else if (argument == "--backend") {
      if (++index >= argc)
        throw std::runtime_error("--backend requires a name");
      options.backend = argv[index];
      if (options.backend != "soulver" && options.backend != "numen")
        throw std::runtime_error("unknown backend: " + options.backend);
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
      << "Evaluate natural-language calculations with SoulverCore or Numen.\n\n"
      << "  --backend NAME    calculation engine: soulver (default) or numen\n"
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

  if (!options.server && options.expression.empty()) {
    std::cerr << "dank-soulver-core: missing expression\n";
    return 2;
  }

  std::unique_ptr<Backend> backend;
  try {
    backend = options.backend == "numen" ? createNumenBackend(!options.server)
                                         : createSoulverBackend(options.resources);
  } catch (const std::exception &error) {
    if (options.server)
      writeJson({{"event", "error"}, {"error", error.what()}});
    else
      std::cerr << "dank-soulver-core: " << error.what() << '\n';
    return 2;
  }

  if (options.server)
    return runServer(*backend, options.backend);

  const auto result = calculate(*backend, options.backend, options.expression, 1);
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
