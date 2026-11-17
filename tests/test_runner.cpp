// SPDX-License-Identifier: GPL-2.0-or-later
// The test runner itself (round 492, task 0468): `blendity_tests --group a,b,c` and `--list-groups` as CI's shard jobs
// see them - this program started as a child.
#include "test_common.h"

using namespace bl;

namespace {

/* The children here run no test of this group: the failing ones and --list-groups return before any group runs, and
 * the passing runs name the hci and board groups with a filter that matches no test. BLENDITY_TEST_CHILD is a second
 * lock against a child that would start children of its own. */
bool in_child() {
  const char *c = std::getenv("BLENDITY_TEST_CHILD");
  return c && *c;
}

int run_self(const std::vector<std::string> &args, std::string &out) {
  std::vector<std::string> argv;
  argv.push_back(test_program());
  for (const std::string &a : args) argv.push_back(a);
  out.clear();
  return sys::run_capture(argv, out, 120);
}

std::vector<std::string> split_lines(const std::string &text) {
  std::vector<std::string> lines;
  std::string line;
  for (char c : text) {
    if (c == '\n') {
      lines.push_back(line);
      line.clear();
    } else if (c != '\r') {
      line += c;
    }
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

bool has(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }

void tests_round492() {
  /* An unknown name runs nothing and says which. */
  test("tests: --group nosuch prints `no such group: nosuch` and exits 2 without running a test (round 492)", [] {
    if (in_child()) return;
    std::string out;
    const int code = run_self({"--group", "nosuch"}, out);
    CHECK(code == 2);
    CHECK(has(out, "no such group: nosuch (--list-groups names them)"));
    CHECK(!has(out, "[ ok ]") && !has(out, "[FAIL]") && !has(out, " checks, "));
  });

  /* A list with one bad name, an empty name or a trailing comma runs none of its good names either. */
  test("tests: --group board,nosuch / board,,gpu / board, / ,board each exit 2, name the offender (empty for the last three) and run no test (round 492)", [] {
    if (in_child()) return;
    struct Case {
      const char *list;
      const char *offender;
    };
    const Case cases[] = {{"board,nosuch", "nosuch"}, {"board,,gpu", ""}, {"board,", ""}, {",board", ""}, {"board,nosuch,gpu", "nosuch"}};
    for (const Case &c : cases) {
      std::string out;
      const int code = run_self({"--group", c.list}, out);
      std::printf("    --group %-18s exit %d\n", c.list, code);
      CHECK(code == 2);
      CHECK(has(out, std::string("no such group: ") + c.offender + " (--list-groups names them)"));
      CHECK(!has(out, "[ ok ]") && !has(out, "[FAIL]") && !has(out, " checks, "));
    }
  });

  /* The table the shard tool (tools/dev/test_shards.py --check) compares against. */
  test("tests: --list-groups names every test group once, one a line: the 17 of the table, in its order, nothing empty (round 492)", [] {
    if (in_child()) return;
    std::string out;
    const int code = run_self({"--list-groups"}, out);
    CHECK(code == 0);
    const std::vector<std::string> lines = split_lines(out);
    const std::set<std::string> unique(lines.begin(), lines.end());
    CHECK(unique.size() == lines.size());  // (each once)
    CHECK(!unique.count(""));
    const std::vector<std::string> table = {"modeling", "render", "lighting", "gpu", "assets", "animation", "physics", "editor", "livelink",
                                            "input", "sketch", "board", "sculpt", "hci", "vr", "blockout", "runner"};
    for (const std::string &name : table) CHECK(unique.count(name) == 1);
    /* A later round may add a group: these stay, in the table's order (a new one may stand anywhere among them). */
    size_t at = 0;
    for (const std::string &line : lines)
      if (at < table.size() && line == table[at]) at++;
    CHECK(at == table.size());
    CHECK(!has(out, "[ ok ]") && !has(out, " checks, "));
  });

  /* A good list runs and succeeds. A filter that matches no test keeps the two groups' tests from running (and this
   * group is not among them). Not when this run has a filter of its own: test() holds the pointer it read at start. */
  test("tests: --group hci,board with a filter that matches no test runs no test and exits 0; the same groups named the other way round, twice, run the same (round 492)", [] {
    if (in_child() || std::getenv("BLENDITY_TEST_FILTER")) return;
    set_env("BLENDITY_TEST_CHILD", "1");
    set_env("BLENDITY_TEST_FILTER", "zz no test has this in its name zz");
    const std::vector<std::vector<std::string>> runs = {
        {"--group", "hci,board"}, {"--group", "board,hci"}, {"--group", "hci,board,hci,board"}, {"--group", "board"}};
    std::vector<int> codes;
    std::vector<std::string> outs;
    for (const std::vector<std::string> &args : runs) {
      std::string out;
      codes.push_back(run_self(args, out));
      outs.push_back(out);
    }
    set_env("BLENDITY_TEST_FILTER", "");  // (empty: every test, as before)
    set_env("BLENDITY_TEST_CHILD", "");
    for (size_t i = 0; i < runs.size(); i++) {
      CHECK(codes[i] == 0);
      CHECK(!has(outs[i], "[ ok ]") && !has(outs[i], "[FAIL]"));
      CHECK(has(outs[i], "\n0 checks, 0 failed"));
      CHECK(!has(outs[i], "no such group"));
    }
  });
}

/* Round 550 (task 0530): a run without BLENDITY_TEST_DIR gets a scratch folder of its own; old runs' folders go. */
void tests_round550() {
  test("a test run without BLENDITY_TEST_DIR works in a folder of its own, run-<stamp>-<n> under the shared one, and removes run folders begun more than a day ago - another run's files never reach its scratch project (round 550)", [] {
    const std::string base = fs::join(test_dir(), "r550_runs");
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::u8path(base), ec);
    fs::make_dirs(base);
    const unsigned long long now = test_run_stamp();
    const double tick_s = (double)std::chrono::system_clock::period::num / (double)std::chrono::system_clock::period::den;
    const unsigned long long two_days = (unsigned long long)(48.0 * 3600.0 / tick_s), an_hour = (unsigned long long)(3600.0 / tick_s);
    const std::string old_run = test_run_dir(base, now - two_days, 1), new_run = test_run_dir(base, now - an_hour, 2), other = fs::join(base, "keep-me");
    for (const std::string &d : {old_run, new_run, other}) fs::make_dirs(fs::join(d, "Assets/Materials"));
    prune_test_runs(base, 24.0 * 3600.0, now);
    CHECK(!fs::exists(old_run));  // (begun two days ago: gone, whatever its folder's time says)
    CHECK(fs::exists(new_run) && fs::exists(other));  // (begun an hour ago; not a run folder at all)
    /* Two runs begun on the same tick get different folders. */
    CHECK(test_run_dir(base, now, 1) != test_run_dir(base, now, 2));
    CHECK(fs::filename(test_run_dir(base, now, 7)).rfind("run-", 0) == 0);
    /* This run's own folder: BLENDITY_TEST_DIR when set (run_suites.py, these runs), a run- folder when not. */
    const char *own = std::getenv("BLENDITY_TEST_DIR");
    CHECK((own && *own) ? test_dir() == std::string(own) : fs::filename(test_dir()).rfind("run-", 0) == 0);
    std::filesystem::remove_all(std::filesystem::u8path(base), ec);
  });
}

}  // namespace

void run_runner_tests() { tests_round492(); tests_round550(); }
