# mscompress image with a shell, for running benchmark scripts.
# The root Dockerfile uses FROM scratch (no shell), so we need this variant.
FROM alpine:3.20 AS build
RUN apk add --no-cache git cmake build-base
WORKDIR /app
COPY .cz.toml ./.cz.toml
COPY src ./src
COPY cli ./cli
COPY vendor ./vendor
RUN cmake -S cli -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS="-static" \
 && cmake --build build -j
RUN strip build/mscompress

FROM alpine:3.20
COPY --from=build /app/build/mscompress /usr/local/bin/mscompress
COPY benchmark/lossy/compress/compress.sh /scripts/compress.sh
ENTRYPOINT ["/bin/sh", "/scripts/compress.sh"]
