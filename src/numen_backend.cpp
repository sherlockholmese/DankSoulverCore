#include "backend.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <unistd.h>

#include "numen/numen.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;
using Clock = std::chrono::system_clock;
using Rates = std::unordered_map<std::string, double>;

namespace {

constexpr auto kDefaultRatesUrl = "https://api.vicinae.com/v1/currencies";
constexpr auto kRatesLifetime = std::chrono::minutes{30};
constexpr auto kFailedFetchRetry = std::chrono::minutes{5};
constexpr long kFetchTimeoutSeconds = 15;
constexpr std::size_t kMaximumRatesBytes = 4 * 1024 * 1024;

// Libnumen's parser recurses per nesting level with no limit of its own, so roughly 10,000 opening
// parentheses overflow the stack. This stays well clear of that on a default 8 MiB stack, and is
// still far beyond anything typed into a launcher.
constexpr std::size_t kMaximumExpressionBytes = 1024;

std::string lowercase(std::string_view text) {
  std::string result(text);
  std::ranges::transform(result, result.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

fs::path ratesCachePath() {
  fs::path cacheHome;
  if (const char *configured = std::getenv("XDG_CACHE_HOME"); configured && *configured)
    cacheHome = configured;
  else
    cacheHome = fs::path(std::getenv("HOME") ? std::getenv("HOME") : ".") / ".cache";
  return cacheHome / "dank-soulver-core" / "numen-rates.json";
}

void addRate(Rates &rates, std::string_view code, double rate) {
  if (std::isfinite(rate) && rate > 0)
    rates[lowercase(code)] = rate;
}

// Rates are units per US dollar. A payload that yields no fiat rates is rejected whole, so a bad
// response can never replace working rates.
std::optional<Rates> parseRates(std::string_view payload) {
  try {
    const auto document = json::parse(payload);
    Rates rates;

    for (const auto &[code, price] : document.at("crypto").at("prices").items()) {
      if (price.is_number() && price.get<double>() > 0)
        addRate(rates, code, 1 / price.get<double>());
    }

    // Fiat wins where a crypto ticker shares its code.
    const auto &fiat = document.at("fiat").at("rates");
    for (const auto &[code, rate] : fiat.items()) {
      if (rate.is_number())
        addRate(rates, code, rate.get<double>());
    }

    if (fiat.empty())
      return std::nullopt;
    return rates;
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

struct Download {
  std::string body;
  std::stop_token stop;
};

std::size_t receiveBody(char *data, std::size_t size, std::size_t count, void *userData) {
  auto &body = static_cast<Download *>(userData)->body;
  const auto bytes = size * count;
  if (body.size() + bytes > kMaximumRatesBytes)
    return 0;
  body.append(data, bytes);
  return bytes;
}

int checkCancelled(void *userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  return static_cast<Download *>(userData)->stop.stop_requested() ? 1 : 0;
}

std::optional<std::string> download(const std::string &url, std::stop_token stop) {
  const std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl{curl_easy_init(),
                                                                 curl_easy_cleanup};
  if (!curl)
    return std::nullopt;

  Download transfer{.body = {}, .stop = std::move(stop)};
  curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "http,https");
  curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
  curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 5L);
  curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, kFetchTimeoutSeconds);
  curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, receiveBody);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &transfer);
  curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, checkCancelled);
  curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &transfer);
  curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);

  if (const auto code = curl_easy_perform(curl.get()); code != CURLE_OK) {
    if (!transfer.stop.stop_requested())
      std::cerr << "Numen rates: " << curl_easy_strerror(code) << '\n';
    return std::nullopt;
  }

  long status = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
  if (status < 200 || status >= 300) {
    std::cerr << "Numen rates: HTTP status " << status << '\n';
    return std::nullopt;
  }
  return std::move(transfer.body);
}

void writeCache(const fs::path &path, std::string_view payload) {
  std::error_code error;
  fs::create_directories(path.parent_path(), error);

  // Written beside the target and renamed in, so a crash cannot leave a truncated cache.
  auto temporary = path;
  temporary += "." + std::to_string(getpid());
  std::ofstream(temporary, std::ios::binary) << payload;
  fs::rename(temporary, path, error);
  if (error) {
    fs::remove(temporary, error);
    std::cerr << "Numen rates: could not write " << path << '\n';
  }
}

// Libnumen calls getRate() while parsing, so it can only ever read what is already in memory.
// Rates start from the disk cache, even an expired one, and refresh on a worker thread.
class CachedRatesProvider final : public numen::AbstractCurrencyProvider {
public:
  CachedRatesProvider() : cachePath_(ratesCachePath()) {
    const char *configuredUrl = std::getenv("DANK_SOULVER_RATES_URL");
    url_ = configuredUrl && *configuredUrl ? configuredUrl : kDefaultRatesUrl;

    std::ifstream cache(cachePath_, std::ios::binary);
    const std::string payload(std::istreambuf_iterator<char>(cache), {});
    if (auto cached = parseRates(payload)) {
      rates_ = std::move(*cached);

      std::error_code error;
      const auto modified = fs::last_write_time(cachePath_, error);
      if (!error)
        nextFetch_ = std::chrono::clock_cast<Clock>(modified) + kRatesLifetime;
    }
  }

  std::optional<numen::ExchangeRate> getRate(std::string_view code) const override {
    if (code.starts_with('$'))
      code.remove_prefix(1);

    const std::scoped_lock lock(mutex_);
    if (const auto rate = rates_.find(std::string(code)); rate != rates_.end())
      return numen::ExchangeRate{.rate = rate->second, .lastUpdatedAt = std::nullopt};
    return std::nullopt;
  }

  // Never blocks: at most one download runs, and it is rate limited by nextFetch_.
  void updateRates() override {
    const std::scoped_lock lock(mutex_);
    const auto now = Clock::now();
    if (fetching_ || now < nextFetch_)
      return;

    fetching_ = true;
    nextFetch_ = now + kFailedFetchRetry;
    if (worker_.joinable())
      worker_.join(); // Already past its last use of mutex_: fetching_ was cleared under the lock.
    worker_ = std::jthread([this](std::stop_token stop) { fetch(std::move(stop)); });
  }

  void waitForFetch() {
    std::unique_lock lock(mutex_);
    fetchFinished_.wait(lock, [this] { return !fetching_; });
  }

private:
  void fetch(std::stop_token stop) {
    std::optional<Rates> fresh;
    auto payload = download(url_, std::move(stop));
    if (payload)
      fresh = parseRates(*payload);

    if (fresh)
      writeCache(cachePath_, *payload);
    else if (payload)
      std::cerr << "Numen rates: ignoring malformed response\n";

    {
      const std::scoped_lock lock(mutex_);
      if (fresh) {
        rates_ = std::move(*fresh);
        nextFetch_ = Clock::now() + kRatesLifetime;
      }
      fetching_ = false;
    }
    fetchFinished_.notify_all();
  }

  fs::path cachePath_;
  std::string url_;

  mutable std::mutex mutex_;
  std::condition_variable fetchFinished_;
  Rates rates_;
  Clock::time_point nextFetch_{};
  bool fetching_ = false;

  std::jthread worker_; // Last member: stops and joins before anything it touches is destroyed.
};

std::string typeOfNumber(const numen::Number &number) {
  if (number.isPercentage)
    return "Percentage";
  if (!number.unit)
    return "Number";

  const auto &unit = number.unit->resolved;
  if (unit && unit->dimension().currency != 0)
    return "Currency";
  return "Unit";
}

std::string typeOfResult(const numen::ComputedValue &result) {
  if (const auto *number = result.asNumber())
    return typeOfNumber(*number);

  if (const auto *dateTime = result.asDateTime()) {
    switch (dateTime->format) {
    case numen::DateTimePrecision::Time:
      return "Time";
    case numen::DateTimePrecision::DateTime:
      return "Date & Time";
    default:
      return "Date";
    }
  }

  return std::string(result.valueTypeName());
}

// Libnumen reads a lone unit as one of that unit, so "kb" or "day" would answer "1kB" or "1 day",
// and with live rates so would any word that is a crypto ticker. Launcher searches hit these all
// the time, so they get SoulverCore's treatment. An explicit "km to miles" still answers.
bool isBareUnit(const std::string &expression, const numen::ComputedValue &result) {
  const bool hasDigit = std::ranges::any_of(
      expression, [](unsigned char c) { return std::isdigit(c) != 0; });
  if (hasDigit)
    return false;
  if (result.conversion && !result.conversion->implicit)
    return false;

  const auto *number = result.asNumber();
  return result.asDuration() || (number && number->unit);
}

class NumenBackend final : public Backend {
public:
  explicit NumenBackend(bool waitForRates) : waitForRates_(waitForRates) {
    // Without strict mode, unknown words are skipped: "gimp 2" answers 2 and "3d printer" 3 days.
    options_.parseOptions.strict = true;

    auto provider = std::make_unique<CachedRatesProvider>();
    rates_ = provider.get();
    calculator_.setCurrencyProvider(std::move(provider));
  }

  json evaluate(const std::string &expression) override {
    if (expression.size() > kMaximumExpressionBytes)
      return {{"value", ""}, {"type", "error"}, {"error", "Expression is too long"}};

    rates_->updateRates();
    if (waitForRates_)
      rates_->waitForFetch();

    const auto result = calculator_.compute(expression, options_);
    if (!result)
      return {{"value", ""}, {"type", "error"}, {"error", result.error()}};

    const auto *number = result->asNumber();
    if (isBareUnit(expression, *result) || (number && !std::isfinite(number->n)))
      return {{"value", ""}, {"type", "none"}, {"error", nullptr}};

    return {{"value", result->toString(options_.effectiveDateTimeFormat())},
            {"type", typeOfResult(*result)},
            {"error", nullptr}};
  }

private:
  bool waitForRates_;
  numen::EvalOptions options_{};
  numen::Numen calculator_;
  CachedRatesProvider *rates_ = nullptr; // Owned by calculator_.
};

} // namespace

std::unique_ptr<Backend> createNumenBackend(bool waitForRates) {
  return std::make_unique<NumenBackend>(waitForRates);
}
