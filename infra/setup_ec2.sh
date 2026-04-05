#!/bin/bash
# =============================================================================
# Módulo 5 — Script de configuración para instancia EC2 (Ubuntu 22.04)
#
# Instala Docker, Docker Compose y las dependencias necesarias para ejecutar
# el sistema IoT Monitoring.
#
# Uso:
#   chmod +x setup_ec2.sh
#   sudo ./setup_ec2.sh
# =============================================================================

set -euo pipefail

echo "============================================"
echo "  Configuracion de instancia EC2"
echo "  IoT Monitoring System"
echo "============================================"

# --- 1. Actualizar el sistema ---
echo ""
echo "[1/5] Actualizando paquetes del sistema..."
apt-get update -y
apt-get upgrade -y

# --- 2. Instalar dependencias necesarias ---
echo ""
echo "[2/5] Instalando dependencias..."
apt-get install -y \
    apt-transport-https \
    ca-certificates \
    curl \
    gnupg \
    lsb-release \
    git

# --- 3. Instalar Docker ---
echo ""
echo "[3/5] Instalando Docker..."

# Agregar clave GPG oficial de Docker
install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
chmod a+r /etc/apt/keyrings/docker.asc

# Agregar repositorio de Docker
echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] \
  https://download.docker.com/linux/ubuntu \
  $(. /etc/os-release && echo "$VERSION_CODENAME") stable" | \
  tee /etc/apt/sources.list.d/docker.list > /dev/null

apt-get update -y
apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

# --- 4. Configurar Docker para el usuario ubuntu ---
echo ""
echo "[4/5] Configurando permisos de Docker..."
usermod -aG docker ubuntu

# Habilitar Docker para que inicie con el sistema
systemctl enable docker
systemctl start docker

# --- 5. Verificar instalación ---
echo ""
echo "[5/5] Verificando instalacion..."
docker --version
docker compose version

echo ""
echo "============================================"
echo "  Instalacion completada exitosamente"
echo ""
echo "  IMPORTANTE: Cierre la sesion SSH y vuelva"
echo "  a conectarse para que los permisos de"
echo "  Docker surtan efecto."
echo ""
echo "  Siguientes pasos:"
echo "    1. Cerrar y reconectar SSH"
echo "    2. git clone <repo> && cd <repo>"
echo "    3. docker compose up -d"
echo "    4. Verificar: curl http://localhost:8080"
echo "============================================"
