FROM alpine:latest

RUN apk add --no-cache git make cmake gcc build-base

WORKDIR /app

COPY src ./src
COPY cli ./cli
COPY vendor ./vendor

WORKDIR /app/cli

RUN cmake .

RUN cmake --build .

CMD ["/app/cli/mscompress"]
