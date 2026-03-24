.PHONY: train build test

BUILD_DIR ?= build
TEST_BUILD_DIR ?= build-test

train:
	@if [ -d ignore/linguist/.git ]; then git -C ignore/linguist pull --ff-only; else git clone --depth 1 https://github.com/github-linguist/linguist.git ignore/linguist; fi
	@python3 train/generate_model.py ignore/linguist/samples -o include/LanguageModel.generated.hpp
	@make build

build:
	@cmake -B $(BUILD_DIR) -S .
	@cmake --build $(BUILD_DIR) --clean-first

test:
	@cmake -B $(TEST_BUILD_DIR) -S .
	@cmake --build $(TEST_BUILD_DIR) --target LanguageClassifierTests ParserFixtureTests
	@ctest --test-dir $(TEST_BUILD_DIR) --output-on-failure
