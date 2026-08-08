# @author ssrjkk | cppload
# Multi-stage build for minimal image size
# Stage 1: Builder
FROM ubuntu:26.04@sha256:678c6550cc43645e08669028bc177f50be4e7c5b8cca677067b1914d4afc7a03 AS builder

ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    gcc-13 \
    g++-13 \
    libboost-all-dev \
    libssl-dev \
    git \
    wget \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

# Set compiler
ENV CC=gcc-13
ENV CXX=g++-13

# Install Conan for dependency management
RUN pip3 install conan==2.0.14

# Create conan default profile
RUN conan profile detect

# Copy source code
WORKDIR /build
COPY . .

# Install C++ dependencies via Conan
RUN conan install . --output-folder=. --build=missing \
    -s build_type=Release

# Configure and build
RUN cmake -B build-release -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCPLOAD_BUILD_PYTHON=OFF \
    -DCPLOAD_BUILD_TESTS=OFF \
    -DCMAKE_INSTALL_PREFIX=/install

RUN cmake --build build-release --config Release -j$(nproc)

# Install to staging directory
RUN cmake --install build-release

# Stage 2: Runtime image (minimal)
FROM ubuntu:26.04@sha256:678c6550cc43645e08669028bc177f50be4e7c5b8cca677067b1914d4afc7a03 AS runtime

ARG DEBIAN_FRONTEND=noninteractive

# Install only runtime dependencies
RUN apt-get update && apt-get install -y \
    libssl3 \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN useradd -m -u 1000 cppload && \
    mkdir -p /var/log/cppload && \
    chown -R cppload:cppload /var/log/cppload

# Copy binaries from builder
COPY --from=builder /install/bin/* /usr/local/bin/
COPY --from=builder /install/lib/* /usr/local/lib/

# Update library cache
RUN ldconfig

# Copy default configs
COPY scenarios /etc/cppload/scenarios

WORKDIR /home/cppload

USER cppload

# Expose metrics port
EXPOSE 9090

# Health check
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:9090/metrics || exit 1

ENTRYPOINT ["cppload-cli"]
CMD ["--help"]

# Labels for metadata
LABEL org.opencontainers.image.title="cppload-pro" \
      org.opencontainers.image.description="Enterprise Load Testing Platform" \
      org.opencontainers.image.authors="cppload-pro contributors" \
      org.opencontainers.image.source="https://github.com/ssrjkk/cppload-workflow" \
      org.opencontainers.image.version="1.0.0" \
      org.opencontainers.image.licenses="Apache-2.0"
