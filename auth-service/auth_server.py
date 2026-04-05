#!/usr/bin/env python3
"""
Módulo 4 — Servicio de autenticación para el sistema IoT.

Servidor TCP que valida credenciales contra users.json.
Protocolo: recibe AUTH|username|password\r\n, responde OK|rol\r\n o FAIL|reason\r\n.
"""

import argparse
import json
import logging
import socket
import sys
import threading
from pathlib import Path

# Configuración de logging — formato ISO-8601, NUNCA loguear contraseñas
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%dT%H:%M:%SZ",
    handlers=[logging.StreamHandler(sys.stdout)],
)
logger = logging.getLogger("auth-service")


def cargar_usuarios(ruta: str) -> dict:
    """Carga la base de datos de usuarios desde un archivo JSON."""
    path = Path(ruta)
    if not path.exists():
        logger.error(f"Archivo de usuarios no encontrado: {ruta}")
        sys.exit(1)

    with open(path, encoding="utf-8") as f:
        usuarios = json.load(f)

    logger.info(f"Cargados {len(usuarios)} usuarios desde {ruta}")
    return usuarios


def procesar_mensaje(mensaje: str, usuarios: dict) -> str:
    """
    Parsea un mensaje AUTH|username|password y retorna la respuesta.
    Retorna OK|rol o FAIL|reason.
    """
    # Eliminar \r\n del final
    mensaje = mensaje.strip()

    if not mensaje:
        return "FAIL|empty_message"

    campos = mensaje.split("|")

    # Validar formato: AUTH|username|password
    if len(campos) != 3:
        return f"FAIL|malformed_message"

    opcode, username, password = campos

    if opcode != "AUTH":
        return f"FAIL|unknown_opcode"

    if not username:
        return "FAIL|empty_username"

    # Buscar usuario en la base de datos
    if username not in usuarios:
        return "FAIL|user_not_found"

    entrada = usuarios[username]

    if entrada["password"] != password:
        return "FAIL|invalid_credentials"

    rol = entrada["role"]
    return f"OK|{rol}"


def manejar_cliente(conn: socket.socket, addr: tuple, usuarios: dict) -> None:
    """Maneja una conexión de un cliente. Un mensaje por conexión."""
    ip_origen = f"{addr[0]}:{addr[1]}"

    try:
        # Timeout de lectura: 10 segundos
        conn.settimeout(10.0)

        datos = b""
        while b"\r\n" not in datos:
            fragmento = conn.recv(1024)
            if not fragmento:
                logger.warning(f"[{ip_origen}] Conexion cerrada sin enviar datos")
                return
            datos += fragmento
            # Protección contra mensajes demasiado largos
            if len(datos) > 4096:
                logger.warning(f"[{ip_origen}] Mensaje demasiado largo, descartando")
                conn.sendall(b"FAIL|message_too_long\r\n")
                return

        mensaje = datos.decode("ascii", errors="replace")
        # Extraer solo hasta el primer \r\n
        linea = mensaje.split("\r\n")[0]

        # Procesar el mensaje
        respuesta = procesar_mensaje(linea, usuarios)

        # Loguear SIN la contraseña
        campos = linea.split("|")
        if len(campos) >= 2:
            usuario_log = campos[1]
        else:
            usuario_log = "(desconocido)"

        resultado = "OK" if respuesta.startswith("OK|") else "FAIL"
        logger.info(f"[{ip_origen}] AUTH usuario={usuario_log} resultado={resultado}")

        # Enviar respuesta
        conn.sendall((respuesta + "\r\n").encode("ascii"))

    except socket.timeout:
        logger.warning(f"[{ip_origen}] Timeout de lectura")
    except ConnectionResetError:
        logger.warning(f"[{ip_origen}] Conexion reseteada por el cliente")
    except UnicodeDecodeError:
        logger.warning(f"[{ip_origen}] Mensaje con caracteres no ASCII")
        try:
            conn.sendall(b"FAIL|invalid_encoding\r\n")
        except OSError:
            pass
    except OSError as e:
        logger.error(f"[{ip_origen}] Error de socket: {e}")
    finally:
        conn.close()


def iniciar_servidor(port: int, usuarios: dict) -> None:
    """Inicia el servidor TCP de autenticación."""
    servidor = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    servidor.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    servidor.bind(("", port))
    servidor.listen(10)

    logger.info(f"Servicio de autenticacion escuchando en puerto {port}")

    try:
        while True:
            conn, addr = servidor.accept()
            hilo = threading.Thread(
                target=manejar_cliente,
                args=(conn, addr, usuarios),
                daemon=True,
            )
            hilo.start()
    except KeyboardInterrupt:
        logger.info("Servidor detenido por el usuario")
    finally:
        servidor.close()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Servicio de autenticacion IoT (Modulo 4)"
    )
    parser.add_argument(
        "--port",
        type=int,
        default=9001,
        help="Puerto TCP para escuchar (default: 9001)",
    )
    parser.add_argument(
        "--users-file",
        type=str,
        default="users.json",
        help="Ruta al archivo de usuarios (default: users.json)",
    )
    args = parser.parse_args()

    usuarios = cargar_usuarios(args.users_file)
    iniciar_servidor(args.port, usuarios)


if __name__ == "__main__":
    main()
