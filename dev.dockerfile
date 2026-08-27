FROM ubuntu:22.04

WORKDIR /root/
COPY . /root/

RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
      libelf1 libelf-dev zlib1g-dev libzstd-dev \
      cmake make git clang-15 llvm-15 llvm-15-dev libclang-15-dev \
      pkg-config build-essential && \
    apt-get install -y --no-install-recommends ca-certificates	&& \
	  update-ca-certificates	&& \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

CMD ["sleep", "infinity"]
