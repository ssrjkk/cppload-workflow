// @author ssrjkk | cppload
#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#if defined(_WIN32)
#include <cstdlib>
#include <process.h>
#else
#include <sys/wait.h>
#include <cstdlib>
#include <unistd.h>
#endif

#ifdef CPLOAD_TEST_CLI_PATH
static const char* const kCliPath = CPLOAD_TEST_CLI_PATH;
#else
static const char* const kCliPath = "python3 tools/mock_cli.py";
#endif

namespace {

// Runs the CLI with the given args, returns the exit code and captured stdout.
// kCliPath is expected to arrive pre-quoted when it may contain spaces (see
// tests/CMakeLists.txt); do not add quotes here or a multi-token fallback
// like "python3 tools/mock_cli.py" would be treated as a single command name.
int run_cli(const std::string& args, std::string& output) {
    std::string cmd = kCliPath + std::string(" ") + args + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r"); // NOLINT(cert-env33-c)
    if (!pipe) return -999;
    char buf[4096];
    output.clear();
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0) { // NOLINT
        output.append(buf, n);
    }
    int st = pclose(pipe);
#if defined(_WIN32)
    return st;
#else
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return -1;
#endif
}

} // namespace

TEST(CliArgs, VersionExitsZero) {
    std::string out;
    int rc = run_cli("--version", out);
    if (rc == -999) {
        GTEST_SKIP() << "CLI executable not found";
    }
    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.find("cppload-pro"), std::string::npos);
    EXPECT_NE(out.find("1.1.0"), std::string::npos);
}

TEST(CliArgs, HelpExitsZeroAndListsOptions) {
    std::string out;
    int rc = run_cli("--help", out);
    if (rc == -999) {
        GTEST_SKIP() << "CLI executable not found";
    }
    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.find("--json"), std::string::npos);
    EXPECT_NE(out.find("--dry-run"), std::string::npos);
    EXPECT_NE(out.find("--init"), std::string::npos);
    EXPECT_NE(out.find("--log-level"), std::string::npos);
}

TEST(CliArgs, InvalidRpsReportsUsageError) {
    std::string out;
    int rc = run_cli("--rps=0 --duration=1 --target=http://127.0.0.1:1", out);
    EXPECT_EQ(rc, 1); // ConfigOrUsageError
}

TEST(CliArgs, InvalidJsonPathExitsError) {
    std::string out;
    int rc = run_cli("--config=definitely-missing.yaml", out);
    EXPECT_EQ(rc, 1);
}

TEST(CliArgs, NoArgsExitsUsageError) {
    std::string out;
    int rc = run_cli("", out);
    EXPECT_EQ(rc, 1);
}

TEST(CliArgs, UnknownFlagExitsUsageError) {
    std::string out;
    int rc = run_cli("--definitely-not-a-flag", out);
    EXPECT_EQ(rc, 1);
}