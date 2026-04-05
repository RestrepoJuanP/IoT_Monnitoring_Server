# =============================================================================
# Módulo 5 — Dockerfile multi-stage para el servidor IOTP + HTTP
#
# Stage 1: compilación con gcc
# Stage 2: imagen slim solo con el binario + archivos web
#
# Uso:
#   docker build -t iot-server .
#   docker run -p 9000:9000 -p 8080:8080 iot-server
#
# Invariantes:
#   - Autosuficiente: docker build && docker run en máquina limpia funciona
#   - Imagen final < 500 MB
#   - Sin estado persistente en el filesystem del contenedor
# =============================================================================

# --- Stage 1: compilación ---
FROM gcc:13 AS builder

WORKDIR /build

# Copiar Makefile y código fuente
COPY Makefile .
COPY src/ src/

# Compilar el servidor
RUN make clean && make

# --- Stage 2: imagen de ejecución ---
FROM debian:bookworm-slim

# Instalar solo las dependencias mínimas de runtime
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        libc6 \
        libpthread-stubs0-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copiar binario compilado desde el stage de build
COPY --from=builder /build/server .

# Copiar archivos HTML estáticos para el servidor HTTP
COPY web/ ./web/

# Crear directorio para logs (montable como volumen)
RUN mkdir -p /app/logs

# Exponer puertos: IOTP (9000) y HTTP (8080)
EXPOSE 9000
EXPOSE 8080

# Punto de entrada: el servidor requiere <puerto_iotp> <archivo_log>
# Los argumentos opcionales (--http-port, --auth-host, --auth-port) se pasan
# via CMD o docker-compose
ENTRYPOINT ["./server"]
CMD ["9000", "/app/logs/server.log", "--http-port", "8080", "--auth-host", "auth", "--auth-port", "9001"]
