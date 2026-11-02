"""End-to-end tests for the dank-soulver-core helper binary.

Usage: test_helper.py HELPER VERSION

Nothing here touches the network: each Numen test gets a private cache directory, seeded with
rates or pointed at a local server. SoulverCore tests skip when soulver-cpp is not installed.
"""

import http.server
import json
import os
import resource
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

HELPER = ""
VERSION = ""

RATES = {
    "base": "USD",
    "fiat": {"rates": {"USD": 1, "EUR": 0.5, "SGD": 2, "JPY": 100}},
    "crypto": {"prices": {"BTC": 50000, "ZERO": 0, "LINK": 10}},
}
SOULVER_INSTALLED = any(
    Path(directory, "libSoulverWrapper.so").exists()
    for directory in ("/usr/lib", "/usr/local/lib", "/usr/lib64")
)


class HelperCase(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.mkdtemp(prefix="dank-soulver-test-")
        self.addCleanup(shutil.rmtree, self.directory, True)
        self.rates_path = Path(self.directory, "dank-soulver-core", "numen-rates.json")
        self.environment = {
            **os.environ,
            "XDG_CACHE_HOME": self.directory,
            "DANK_SOULVER_RATES_URL": "http://127.0.0.1:9/unreachable",
            "LC_ALL": "C",
            "TZ": "UTC",
        }

    def seed_rates(self, payload=None, age_seconds=0):
        self.rates_path.parent.mkdir(parents=True, exist_ok=True)
        self.rates_path.write_text(
            payload if isinstance(payload, str) else json.dumps(payload or RATES)
        )
        modified = time.time() - age_seconds
        os.utime(self.rates_path, (modified, modified))

    def run_helper(self, *arguments):
        return subprocess.run(
            [HELPER, *arguments],
            env=self.environment,
            capture_output=True,
            text=True,
            timeout=60,
        )

    def numen(self, expression):
        process = self.run_helper("--backend", "numen", "--json", expression)
        self.assertEqual(process.returncode, 0, process.stderr)
        return json.loads(process.stdout)


class CommandLine(HelperCase):
    def test_version(self):
        self.assertEqual(self.run_helper("--version").stdout.strip(), f"dank-soulver-core {VERSION}")

    def test_help_lists_backends(self):
        self.assertIn("--backend NAME", self.run_helper("--help").stdout)

    def test_unknown_backend(self):
        process = self.run_helper("--backend", "qalculate", "1+1")
        self.assertEqual(process.returncode, 2)
        self.assertIn("unknown backend", process.stderr)

    def test_backend_requires_name(self):
        self.assertEqual(self.run_helper("--backend").returncode, 2)

    def test_missing_expression(self):
        process = self.run_helper("--backend", "numen")
        self.assertEqual(process.returncode, 2)
        self.assertIn("missing expression", process.stderr)

    def test_numen_plain_output(self):
        self.seed_rates()
        process = self.run_helper("--backend", "numen", "2", "+", "3")
        self.assertEqual((process.returncode, process.stdout), (0, "5\n"))

    def test_numen_plain_error_exit_code(self):
        self.seed_rates()
        process = self.run_helper("--backend", "numen", "1/0")
        self.assertEqual(process.returncode, 1)
        self.assertIn("Division by zero", process.stderr)


class NumenEvaluation(HelperCase):
    def setUp(self):
        super().setUp()
        self.seed_rates()

    def assert_answer(self, expression, value, result_type):
        result = self.numen(expression)
        self.assertEqual(
            (result["value"], result["type"], result["error"]),
            (value, result_type, None),
            expression,
        )
        self.assertEqual(result["backend"], "numen")
        self.assertEqual(result["expression"], expression)

    def assert_silent(self, expression):
        result = self.numen(expression)
        self.assertEqual(result["value"], "", expression)
        self.assertIn(result["type"], ("none", "error"), expression)

    def test_numbers(self):
        self.assert_answer("(2+3)*4", "20", "Number")
        self.assert_answer("sqrt(16)", "4", "Number")
        self.assert_answer("10 mod 3", "1", "Number")
        self.assert_answer("0xff", "255", "Number")
        self.assert_answer("pi", "3.141593", "Number")

    def test_percentages(self):
        self.assert_answer("15% of 80", "12", "Number")
        self.assert_answer("100 + 20%", "120", "Number")
        self.assert_answer("50%", "0.5", "Percentage")

    def test_units(self):
        self.assert_answer("12 km in miles", "7.456454mi", "Unit")
        self.assert_answer("1 gb to mb", "1,000MB", "Unit")
        self.assert_answer("km to m", "1,000m", "Unit")

    def test_currency(self):
        self.assert_answer("100 usd to eur", "€50", "Currency")
        self.assert_answer("40 SGD in USD", "$20", "Currency")
        self.assert_answer("1 btc to usd", "$50,000", "Currency")

    def test_dates(self):
        self.assertEqual(self.numen("tomorrow")["type"], "Date & Time")
        self.assertEqual(self.numen("time in tokyo")["type"], "Date & Time")
        self.assertIn("Asia/Tokyo", self.numen("time in tokyo")["value"])

    def test_other_value_types(self):
        self.assert_answer("5 > 3", "true", "Boolean")
        self.assert_answer("1 day + 2 days", "3 days", "Duration")

    def test_application_searches_stay_silent(self):
        for search in ("firefox", "visual studio code", "gimp 2", "firefox 3", "3d printer",
                       "7zip", "note 7", "new york", "paris"):
            self.assert_silent(search)

    def test_bare_units_stay_silent(self):
        for search in ("kb", "day", "week", "mile", "usd", "link", "btc"):
            self.assert_silent(search)

    def test_infinite_answers_stay_silent(self):
        for search in ("9^9^9^9", "1" * 400):
            self.assertEqual(self.numen(search)["value"], "", search)

    def test_incomplete_input_reports_error(self):
        for search in ("5 +", "(2+", "1/0"):
            result = self.numen(search)
            self.assertEqual((result["value"], result["type"]), ("", "error"), search)
            self.assertTrue(result["error"], search)


class NumenRates(HelperCase):
    def serve(self, body=None, status=200):
        requests = []

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                requests.append(self.path)
                encoded = body if isinstance(body, bytes) else json.dumps(body or RATES).encode()
                self.send_response(status)
                self.send_header("Content-Length", str(len(encoded)))
                self.end_headers()
                self.wfile.write(encoded)

            def log_message(self, *_):
                pass

        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        self.environment["DANK_SOULVER_RATES_URL"] = f"http://127.0.0.1:{server.server_port}/rates"
        return requests

    def test_downloads_and_caches_when_nothing_is_cached(self):
        requests = self.serve()
        self.assertEqual(self.numen("100 usd to eur")["value"], "€50")
        self.assertEqual(requests, ["/rates"])
        self.assertEqual(json.loads(self.rates_path.read_text()), RATES)

    def test_fresh_cache_skips_download(self):
        self.seed_rates()
        requests = self.serve()
        self.assertEqual(self.numen("100 usd to eur")["value"], "€50")
        self.assertEqual(requests, [])

    def test_expired_cache_is_refreshed(self):
        self.seed_rates({**RATES, "fiat": {"rates": {"USD": 1, "EUR": 0.25}}}, age_seconds=7200)
        requests = self.serve()
        self.numen("1 + 1")
        self.assertEqual(requests, ["/rates"])
        self.assertEqual(self.numen("100 usd to eur")["value"], "€50")

    def test_expired_cache_survives_being_offline(self):
        self.seed_rates(age_seconds=7200)
        self.assertEqual(self.numen("100 usd to eur")["value"], "€50")

    def test_bad_responses_keep_working_rates(self):
        for body, status in ((b"<html>oops", 200), (b"{}", 200), (b"nope", 500),
                             (json.dumps({"fiat": {"rates": {}}, "crypto": {"prices": {}}}).encode(), 200)):
            self.seed_rates(age_seconds=7200)
            before = self.rates_path.read_text()
            self.serve(body, status)
            self.assertEqual(self.numen("100 usd to eur")["value"], "€50", body)
            self.assertEqual(self.rates_path.read_text(), before, body)

    def test_corrupt_cache_is_replaced(self):
        self.seed_rates("{ truncated")
        self.serve()
        self.assertEqual(self.numen("100 usd to eur")["value"], "€50")

    def test_no_rates_at_all_is_a_quiet_error(self):
        result = self.numen("100 usd to eur")
        self.assertEqual((result["value"], result["type"]), ("", "error"))
        self.assertEqual(self.numen("1 + 1")["value"], "2")

    def test_server_exits_promptly_during_a_stalled_download(self):
        listener = http.server.ThreadingHTTPServer(("127.0.0.1", 0), http.server.BaseHTTPRequestHandler)
        self.addCleanup(listener.server_close)  # Accepts in the backlog, then never replies.
        self.environment["DANK_SOULVER_RATES_URL"] = f"http://127.0.0.1:{listener.server_port}/"
        started = time.monotonic()
        process = subprocess.run(
            [HELPER, "--backend", "numen", "--server"],
            input=json.dumps({"id": 1, "expression": "1 + 1"}) + "\n",
            env=self.environment, capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(process.returncode, 0)
        self.assertLess(time.monotonic() - started, 5)
        self.assertIn('"value":"2"', process.stdout)


class Server(HelperCase):
    def converse(self, *lines, backend="numen"):
        process = subprocess.run(
            [HELPER, "--backend", backend, "--server"],
            input="\n".join(lines) + "\n",
            env=self.environment,
            capture_output=True,
            text=True,
            timeout=60,
        )
        self.assertEqual(process.returncode, 0, process.stderr)
        return [json.loads(line) for line in process.stdout.splitlines()]

    def test_protocol(self):
        self.seed_rates()
        ready, first, invalid, oversize, last = self.converse(
            json.dumps({"id": 7, "expression": "2 * 21"}),
            "this is not json",
            "x" * (64 * 1024 + 1),
            json.dumps({"id": 8, "expression": "nonsense"}),
        )
        self.assertEqual(ready, {"event": "ready", "backend": "numen"})
        self.assertEqual((first["id"], first["value"], first["backend"]), (7, "42", "numen"))
        self.assertIn("Invalid request", invalid["error"])
        self.assertIn("too large", oversize["error"])
        self.assertEqual((last["id"], last["type"]), (8, "error"))

    def test_survives_hostile_expressions(self):
        self.seed_rates()
        expressions = ["(" * 30000, "-" * 30000, "1" * 30000, "9^9^9^9", "é世\U0001f600", ""]
        lines = [json.dumps({"id": i + 1, "expression": e}) for i, e in enumerate(expressions)]
        lines.append(json.dumps({"id": 99, "expression": "1 + 1"}))
        responses = self.converse(*lines)
        self.assertEqual((responses[-1]["id"], responses[-1]["value"]), (99, "2"))

    def test_startup_is_fast(self):
        self.seed_rates()
        started = time.monotonic()
        self.converse()
        self.assertLess(time.monotonic() - started, 1)


class SoulverMissing(HelperCase):
    def setUp(self):
        super().setUp()
        self.environment["SOULVER_WRAPPER_LIBRARY"] = "/nonexistent/libSoulverWrapper.so"

    def test_server_reports_event(self):
        process = self.run_helper("--server")
        self.assertEqual(process.returncode, 2)
        event = json.loads(process.stdout)
        self.assertEqual(event["event"], "error")
        self.assertIn("switch the backend to Numen", event["error"])

    def test_one_shot_reports_on_stderr(self):
        process = self.run_helper("1 + 1")
        self.assertEqual(process.returncode, 2)
        self.assertIn("Could not load soulver-cpp", process.stderr)

    def test_numen_does_not_need_it(self):
        self.seed_rates()
        self.assertEqual(self.numen("1 + 1")["value"], "2")


@unittest.skipUnless(SOULVER_INSTALLED, "soulver-cpp is not installed")
class Soulver(HelperCase):
    def soulver(self, expression):
        process = self.run_helper("--json", expression)
        self.assertEqual(process.returncode, 0, process.stderr)
        return json.loads(process.stdout)

    def test_is_the_default_backend(self):
        result = self.soulver("forty two plus eight")
        self.assertEqual((result["value"], result["backend"]), ("50", "soulver"))

    def test_percentage(self):
        self.assertEqual(self.soulver("15% of 80")["value"], "12")

    def test_application_search_stays_silent(self):
        self.assertEqual(self.soulver("firefox")["type"], "none")

    def test_server(self):
        process = subprocess.run(
            [HELPER, "--backend", "soulver", "--server"],
            input=json.dumps({"id": 3, "expression": "6 * 7"}) + "\n",
            env=self.environment, capture_output=True, text=True, timeout=60,
        )
        ready, result = [json.loads(line) for line in process.stdout.splitlines()]
        self.assertEqual(ready, {"event": "ready", "backend": "soulver"})
        self.assertEqual((result["id"], result["value"], result["backend"]), (3, "42", "soulver"))


if __name__ == "__main__":
    HELPER, VERSION = sys.argv[1], sys.argv[2]
    unittest.main(argv=sys.argv[:1], verbosity=1)
