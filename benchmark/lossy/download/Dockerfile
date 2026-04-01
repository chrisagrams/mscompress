FROM alpine:3.20
RUN apk add --no-cache curl gzip
COPY download.sh /app/download.sh
RUN chmod +x /app/download.sh
ENTRYPOINT ["/app/download.sh"]
