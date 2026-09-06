POLICY_BUILD_DIR ?= src/nccl-policy-plugin/build
NET_BUILD_DIR ?= src/nccl-net-ebpf-plugin/build

.PHONY: build test clean install

build:
	cmake -S src/nccl-policy-plugin -B $(POLICY_BUILD_DIR)
	cmake --build $(POLICY_BUILD_DIR) --parallel
	cmake -S src/nccl-net-ebpf-plugin -B $(NET_BUILD_DIR)
	cmake --build $(NET_BUILD_DIR) --parallel

test: build
	ctest --test-dir $(POLICY_BUILD_DIR) --output-on-failure
	scripts/test_nccl_bench.sh

clean:
	@if [ -d "$(POLICY_BUILD_DIR)" ]; then \
		cmake --build $(POLICY_BUILD_DIR) --target clean; \
	fi
	@if [ -d "$(NET_BUILD_DIR)" ]; then \
		cmake --build $(NET_BUILD_DIR) --target clean; \
	fi

install:
	@if [ "$$(id -u)" -eq 0 ]; then \
		apt_prefix=""; \
	elif command -v sudo >/dev/null 2>&1; then \
		apt_prefix="sudo"; \
	else \
		echo "ERROR: make install requires root or sudo" >&2; \
		exit 1; \
	fi; \
	$$apt_prefix apt-get update; \
	$$apt_prefix apt-get install -y --no-install-recommends \
		libelf1 libelf-dev zlib1g-dev libzstd-dev libboost-dev \
		cmake make git clang-15 llvm-15 llvm-15-dev libclang-15-dev \
		pkg-config build-essential
