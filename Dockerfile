FROM alpine:latest AS builder

RUN apk add --no-cache \
    bash \
    binutils \
    bison \
    bsd-compat-headers \
    build-base \
    cmake \
    elfutils-dev \
    flex \
    fortify-headers \
    g++ \
    gcc \
    git \
    iptables-dev \
    libatomic \
    libcap-dev \
    libgomp \
    libgphobos \
    linux-headers \
    make \
    musl-dev \
    ninja

ADD . /autogit

RUN cmake -B /autogit/build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS="-static" \
    -G Ninja && \
    cmake --build /autogit/build

FROM alpine:latest

RUN apk add --no-cache ca-certificates bash

COPY --from=builder /autogit/build/bin/autogit /usr/local/bin/

WORKDIR /root
ENTRYPOINT ["autogit"]
