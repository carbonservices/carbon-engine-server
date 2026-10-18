# syntax=docker/dockerfile:1
# ─────────────────────────────────────────────────────────────────────────────
# Stage 1: Build Environment
# ─────────────────────────────────────────────────────────────────────────────
FROM ubuntu:rolling AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install essential compilation toolchain, CMake, Ninja, and Qt6 development packages
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    curl \
    ca-certificates \
    qt6-base-dev \
    qt6-httpserver-dev \
    qt6-websockets-dev \
    libopenblas-dev \
    liblapack-dev \
    libtesseract-dev \
    libleptonica-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# Copy source repository
COPY . /workspace/

# Configure and compile carbon-engine headless server
RUN cmake -B build -G "Ninja" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCARBON_ENGINE_DOWNLOAD_MODELS=OFF \
    && cmake --build build --config Release --parallel $(nproc)

# Locate and strip the binary to minimize container image footprint
RUN find /workspace -name "carbon-engine" -type f -exec strip {} + && \
    ldd /workspace/__Builds/x64/bin/carbon-engine

# ─────────────────────────────────────────────────────────────────────────────
# Stage 2: Minimal Production Runtime
# ─────────────────────────────────────────────────────────────────────────────
FROM ubuntu:rolling AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Install only the runtime shared libraries needed for execution
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    libqt6core6t64 \
    libqt6network6 \
    libqt6sql6 \
    libqt6sql6-sqlite \
    libqt6concurrent6 \
    libqt6httpserver6 \
    libqt6websockets6 \
    libopenblas0 \
    liblapack3 \
    libgomp1 \
    && rm -rf /var/lib/apt/lists/*

# Create application directory and non-privileged service user
RUN groupadd -g 10001 carbon && \
    useradd -u 10001 -g carbon -s /bin/false -d /app carbon && \
    mkdir -p /app/data/uploads /app/data/logs && \
    chown -R carbon:carbon /app

WORKDIR /app

# Copy executable and runtime dependencies from builder
COPY --from=builder /workspace/__Builds/x64/bin/carbon-engine /app/carbon-engine
COPY --from=builder /workspace/__Builds/x64/bin/libpdfium.so* /app/
COPY --from=builder /workspace/__Builds/x64/bin/template.html /app/template.html
COPY --from=builder /workspace/__Builds/x64/bin/logo.png /app/logo.png
COPY --from=builder /workspace/__Builds/x64/bin/eng.traineddata /app/eng.traineddata

# Ensure correct file permissions
RUN chmod +x /app/carbon-engine && \
    chown -R carbon:carbon /app

# Environment defaults for Cloud Run container contract
ENV PORT=8080 \
    CARBON_ENGINE_PORT=8080 \
    CARBON_ENGINE_BIND_ALL=1 \
    CARBON_ENGINE_DB_PATH=/app/data/ce-database.sqlite \
    CARBON_ENGINE_UPLOAD_DIR=/app/data/uploads \
    CARBON_ENGINE_LOG_DIR=/app/data/logs \
    CARBON_ENGINE_CLOUD_ENABLED=1 \
    LD_LIBRARY_PATH=/app:$LD_LIBRARY_PATH \
    TESSDATA_PREFIX=/app

EXPOSE 8080

# Health check verifies the REST API telemetry endpoint
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -f http://127.0.0.1:${PORT:-8080}/api/system || exit 1

# Run as non-root user
USER carbon

VOLUME ["/app/data"]

ENTRYPOINT ["/app/carbon-engine"]

