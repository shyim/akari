FROM golang:1.26-bookworm AS builder

WORKDIR /app
COPY go.mod go.sum ./
RUN go mod download

COPY . ./
RUN CGO_ENABLED=0 go build -o /akari-forwarder ./cmd/akari-forwarder

FROM gcr.io/distroless/static-debian13:latest
COPY --from=builder /akari-forwarder /usr/local/bin/akari-forwarder
ENTRYPOINT ["akari-forwarder"]
