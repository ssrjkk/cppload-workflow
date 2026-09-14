// Self-contained gtest main: replaces the system-provided gtest_main so
// test runners do not depend on a separately shipped libgtest_main DLL.
#include <gtest/gtest.h>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}