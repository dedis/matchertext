.PHONY: train build

train:
	@if [ -d ignore/linguist/.git ]; then git -C ignore/linguist pull --ff-only; else git clone --depth 1 https://github.com/github-linguist/linguist.git ignore/linguist; fi
	@python3 train/generate_model.py ignore/linguist/samples -o include/LanguageModel.generated.hpp
	@make build

build:
	@cmake -B build -S .
	@cmake --build build --clean-first