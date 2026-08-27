POLICY_BUILD_DIR ?= src/nccl-policy-plugin/build
NET_BUILD_DIR ?= src/nccl-net-ebpf-plugin/build

.PHONY: build clean install

build:
	cmake -S src/nccl-policy-plugin -B $(POLICY_BUILD_DIR)
	cmake --build $(POLICY_BUILD_DIR) --parallel
	cmake -S src/nccl-net-ebpf-plugin -B $(NET_BUILD_DIR)
	cmake --build $(NET_BUILD_DIR) --parallel

clean:
	@if [ -d "$(POLICY_BUILD_DIR)" ]; then \
		cmake --build $(POLICY_BUILD_DIR) --target clean; \
	fi
	@if [ -d "$(NET_BUILD_DIR)" ]; then \
		cmake --build $(NET_BUILD_DIR) --target clean; \
	fi

install:
	sudo apt update
	sudo apt-get install -y --no-install-recommends \
        libelf1 libelf-dev zlib1g-dev \
        make clang llvm
