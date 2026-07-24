<!-- @author ssrjkk | cppload -->
# Contributing to cppload-pro

Thank you for your interest in contributing to cppload-pro!

## Development Setup

### Prerequisites
- C++20 compatible compiler (GCC 13+, Clang 15+, MSVC 2022+)
- CMake 3.20+
- Python 3.9+
- Conan 2.0+ (C++ dependency manager)
- Docker (for testing demo environment)

### Building from Source
```bash
git clone https://github.com/ssrjkk/cppload-workflow.git
cd cppload-workflow

# Install C++ dependencies
conan install . --output-folder=build --build=missing

# Configure
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCPLOAD_BUILD_TESTS=ON \
  -DCPLOAD_BUILD_PYTHON=ON

# Build
cmake --build build

# Run tests
cd build && ctest --output-on-failure
```

## Code Style
- C++: Follow the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- Python: Use Black formatter (`black python/`)
- Commit messages: Use [Conventional Commits](https://www.conventionalcommits.org/)

## Pull Request Process
1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'feat: add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## Reporting Issues
Use the [GitHub Issues](https://github.com/ssrjkk/cppload-workflow/issues) page to report bugs or suggest features.

## Contact
- Telegram: [@ssrjkk](https://t.me/ssrjkk)
- Email: ray013lefe@gmail.com
