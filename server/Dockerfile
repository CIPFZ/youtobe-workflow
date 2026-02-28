FROM ubuntu:22.04 AS build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY server /app
RUN cmake -S . -B build && cmake --build build -j"$(nproc)"

FROM ubuntu:22.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /app/build/av_service /app/av_service

ENTRYPOINT ["/app/av_service"]
