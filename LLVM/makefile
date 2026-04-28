.PHONY: train build test

BUILD_DIR ?= build/main
TEST_BUILD_DIR ?= build/test
PYTHON ?= $(shell if command -v python3 >/dev/null 2>&1; then command -v python3; elif command -v python >/dev/null 2>&1; then command -v python; fi)

train:
	@if [ -d ignore/linguist/.git ]; then git -C ignore/linguist pull --ff-only; else git clone --depth 1 https://github.com/github-linguist/linguist.git ignore/linguist; fi
	@if [ -z "$(PYTHON)" ]; then echo "Error: no Python interpreter found in PATH (need Python 3.8+)"; exit 1; fi
	@"$(PYTHON)" -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 8) else "Python 3.8+ is required for train/generate_model.py")'
	@"$(PYTHON)" train/generate_model.py ignore/linguist/samples -o include/LanguageModel.generated.hpp

build:
	@cmake -B $(BUILD_DIR) -S .
	@cmake --build $(BUILD_DIR) --target LLVM_MinML_Parser

test:
	@cmake -B $(TEST_BUILD_DIR) -S .
	@cmake --build $(TEST_BUILD_DIR) --target LanguageClassifierTests ParserFixtureTests DebugLanguageOutputTests
	@ctest --test-dir $(TEST_BUILD_DIR) --output-on-failure
