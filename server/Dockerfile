FROM ubuntu:22.04 AS build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config git ca-certificates \
    libavformat-dev libavcodec-dev libavutil-dev \
    libssl-dev zlib1g-dev uuid-dev && \
    rm -rf /var/lib/apt/lists/*

# build and install sogou/workflow
RUN git clone --depth 1 https://github.com/sogou/workflow.git /tmp/workflow && \
    make -C /tmp/workflow -j"$(nproc)" && \
    make -C /tmp/workflow install

# build and install whisper.cpp (C API shared libs + header)
RUN git clone --depth 1 https://github.com/ggml-org/whisper.cpp.git /tmp/whisper.cpp && \
    cmake -S /tmp/whisper.cpp -B /tmp/whisper.cpp/build \
      -DBUILD_SHARED_LIBS=ON \
      -DWHISPER_BUILD_TESTS=OFF \
      -DWHISPER_BUILD_EXAMPLES=OFF && \
    cmake --build /tmp/whisper.cpp/build -j"$(nproc)" && \
    cp -a /tmp/whisper.cpp/include/whisper.h /usr/local/include/ && \
    cp -a /tmp/whisper.cpp/build/src/libwhisper.so* /usr/local/lib/ && \
    cp -a /tmp/whisper.cpp/build/ggml/src/libggml*.so* /usr/local/lib/ && \
    ldconfig

WORKDIR /app
COPY server /app
RUN cmake -S . -B build \
      -DENABLE_WORKFLOW=ON \
      -DENABLE_FFMPEG=ON \
      -DENABLE_WHISPERCPP=ON && \
    cmake --build build -j"$(nproc)"

FROM ubuntu:22.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 ca-certificates \
    libavformat58 libavcodec58 libavutil56 \
    libssl3 zlib1g libuuid1 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /app/build/av_service /app/av_service
COPY --from=build /usr/local/lib/libworkflow.so* /usr/local/lib/
COPY --from=build /usr/local/lib/libwhisper.so* /usr/local/lib/
COPY --from=build /usr/local/lib/libggml*.so* /usr/local/lib/

ENV LD_LIBRARY_PATH=/usr/local/lib:${LD_LIBRARY_PATH}
EXPOSE 8888

ENTRYPOINT ["/app/av_service"]
