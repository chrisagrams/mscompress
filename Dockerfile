# Build stage
FROM alpine:3.20 AS build
RUN apk add --no-cache git cmake build-base
WORKDIR /app
COPY src ./src
COPY cli ./cli
COPY vendor ./vendor
RUN cmake -S cli -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS="-static" \
 && cmake --build build -j
RUN strip build/mscompress

# Minimal runtime stage
FROM scratch
COPY --from=build /app/build/mscompress /usr/local/bin/mscompress
ENTRYPOINT [ "/usr/local/bin/mscompress" ]