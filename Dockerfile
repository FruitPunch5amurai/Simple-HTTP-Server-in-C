FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    VCPKG_ROOT=/opt/vcpkg \
    VCPKG_DISABLE_METRICS=1

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        curl \
        git \
        pkg-config \
        tar \
        unzip \
        zip \
    && rm -rf /var/lib/apt/lists/*

# your_program.sh configures CMake with ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
RUN git clone --depth 1 https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}" \
    && "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics

WORKDIR /tmp

COPY . .

RUN printf 'HELLO WORLD!' > foo

# Windows checkouts use CRLF; /bin/sh rejects those scripts.
RUN sed -i 's/\r$//' your_program.sh .codecrafters/*.sh \
    && chmod +x your_program.sh .codecrafters/*.sh

EXPOSE 4221

ENTRYPOINT ["./your_program.sh", "--directory", "/tmp/"]
