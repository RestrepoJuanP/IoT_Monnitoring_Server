#!/usr/bin/env python3
"""
Módulo 3 — Simulador de sensores IoT.

Crea N hilos, cada uno simula un sensor que se conecta al servidor central
por TCP, se registra con REGISTER, y envía mediciones periódicas con DATA.
Modo caos: inyecta valores fuera de rango para disparar alertas.
"""

import argparse
import json
import logging
import random
import signal
import socket
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

# --- Logging global ---
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] [%(threadName)s] %(message)s",
    datefmt="%Y-%m-%dT%H:%M:%SZ",
    handlers=[logging.StreamHandler(sys.stdout)],
)
logger = logging.getLogger("sensor-simulator")

# Señal global para detener todos los hilos limpiamente
detener = threading.Event()

# Tipos de sensor disponibles (round-robin)
TIPOS_SENSOR = ["temperatura", "vibracion", "energia"]


def cargar_config(ruta: str) -> dict:
    """Carga la configuración de rangos y credenciales desde config.json."""
    path = Path(ruta)
    if not path.exists():
        logger.warning(f"Config no encontrado en {ruta}, usando valores por defecto")
        return {
            "tipos_sensor": {
                "temperatura": {"rango_min": 15.0, "rango_max": 90.0, "caos_min": -10.0, "caos_max": 120.0},
                "vibracion":   {"rango_min": 0.0,  "rango_max": 50.0, "caos_min": 0.0,   "caos_max": 100.0},
                "energia":     {"rango_min": 0.0,  "rango_max": 500.0, "caos_min": 0.0,   "caos_max": 800.0},
            },
            "credenciales": {"password": "sensor123"},
        }

    with open(path, encoding="utf-8") as f:
        return json.load(f)


def generar_id(tipo: str, indice: int) -> str:
    """Genera un ID único para el sensor según su tipo e índice."""
    prefijos = {"temperatura": "temp", "vibracion": "vib", "energia": "energy"}
    prefijo = prefijos.get(tipo, "sensor")
    return f"{prefijo}_{indice:03d}"


def generar_valor(config_tipo: dict, modo_caos: bool, contador_envios: int) -> float:
    """
    Genera un valor de medición.
    En modo caos, cada ~10 envíos genera un valor fuera del rango normal.
    """
    if modo_caos and contador_envios > 0 and contador_envios % 10 == 0:
        # Valor fuera de rango para disparar alerta
        return round(random.uniform(config_tipo["caos_min"], config_tipo["caos_max"]), 2)

    # Valor normal dentro del rango
    return round(random.uniform(config_tipo["rango_min"], config_tipo["rango_max"]), 2)


def timestamp_iso8601() -> str:
    """Retorna el timestamp actual en formato ISO-8601 UTC."""
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def conectar_con_backoff(host: str, port: int, sensor_id: str) -> socket.socket:
    """
    Conecta al servidor resolviendo hostname por DNS con getaddrinfo().
    Reintenta con backoff exponencial (máximo 5 intentos).
    Retorna el socket conectado o lanza excepción si falla.
    """
    max_intentos = 5

    for intento in range(1, max_intentos + 1):
        if detener.is_set():
            raise ConnectionError("Simulador detenido")

        try:
            # Resolver hostname por DNS — NUNCA IP literal directa
            resultados = socket.getaddrinfo(
                host, port,
                socket.AF_UNSPEC,
                socket.SOCK_STREAM,
            )

            if not resultados:
                raise socket.gaierror(f"No se pudo resolver {host}")

            # Intentar con cada resultado de DNS
            for familia, tipo, proto, _canonname, direccion in resultados:
                try:
                    sock = socket.socket(familia, tipo, proto)
                    sock.settimeout(30.0)
                    sock.connect(direccion)
                    logger.info(f"[{sensor_id}] Conectado a {host}:{port}")
                    return sock
                except OSError:
                    sock.close()
                    continue

            raise ConnectionError(f"No se pudo conectar a ninguna direccion de {host}")

        except (OSError, ConnectionError) as e:
            if intento == max_intentos:
                logger.error(f"[{sensor_id}] Fallo tras {max_intentos} intentos: {e}")
                raise

            espera = 2 ** (intento - 1)  # 1, 2, 4, 8, 16 segundos
            logger.warning(
                f"[{sensor_id}] Intento {intento}/{max_intentos} fallido: {e}. "
                f"Reintentando en {espera}s..."
            )
            # Esperar con verificación periódica de la señal de detención
            for _ in range(espera):
                if detener.is_set():
                    raise ConnectionError("Simulador detenido")
                time.sleep(1)

    raise ConnectionError("Agotados los reintentos")


def enviar_mensaje(sock: socket.socket, mensaje: str) -> None:
    """Envía un mensaje IOTP completo (con \\r\\n) por el socket."""
    sock.sendall(mensaje.encode("ascii"))


def recibir_respuesta(sock: socket.socket) -> str:
    """
    Recibe una respuesta IOTP del servidor (hasta \\r\\n).
    Retorna la línea sin el terminador.
    """
    datos = b""
    while b"\r\n" not in datos:
        fragmento = sock.recv(4096)
        if not fragmento:
            raise ConnectionResetError("Servidor cerro la conexion")
        datos += fragmento

    respuesta = datos.decode("ascii").split("\r\n")[0]
    return respuesta


def hilo_sensor(
    sensor_id: str,
    tipo: str,
    host: str,
    port: int,
    password: str,
    config_tipo: dict,
    interval: float,
    modo_caos: bool,
) -> None:
    """
    Función principal de cada hilo sensor.
    Flujo: conectar → REGISTER → loop(DATA) → DISCONNECT.
    """
    sock = None

    try:
        # 1. Conectar con backoff exponencial
        sock = conectar_con_backoff(host, port, sensor_id)

        # 2. Enviar REGISTER
        msg_register = f"REGISTER|sensor|{sensor_id}|{password}\r\n"
        enviar_mensaje(sock, msg_register)
        logger.info(f"[{sensor_id}] Enviado REGISTER como sensor")

        # 3. Esperar ACK
        respuesta = recibir_respuesta(sock)
        if respuesta.startswith("ACK|"):
            logger.info(f"[{sensor_id}] Registrado exitosamente: {respuesta}")
        elif respuesta.startswith("ERROR|"):
            logger.error(f"[{sensor_id}] Registro rechazado: {respuesta}")
            return
        else:
            logger.error(f"[{sensor_id}] Respuesta inesperada: {respuesta}")
            return

        # 4. Loop de envío de mediciones
        contador = 0
        while not detener.is_set():
            contador += 1

            # Generar valor (con posible inyección de caos)
            valor = generar_valor(config_tipo, modo_caos, contador)
            ts = timestamp_iso8601()

            msg_data = f"DATA|{sensor_id}|{tipo}|{valor:.2f}|{ts}\r\n"
            enviar_mensaje(sock, msg_data)

            # Esperar ACK
            respuesta = recibir_respuesta(sock)
            if respuesta.startswith("ACK|"):
                logger.info(
                    f"[{sensor_id}] Envio #{contador}: {tipo}={valor:.2f} -> {respuesta}"
                )
            else:
                logger.warning(f"[{sensor_id}] Respuesta inesperada: {respuesta}")

            # Modo caos: avisar cuando se inyecta valor anómalo
            if modo_caos and contador > 0 and contador % 10 == 0:
                logger.info(f"[{sensor_id}] CAOS: valor inyectado fuera de rango = {valor:.2f}")

            # Esperar el intervalo (con verificación de detención)
            detener.wait(timeout=interval)

    except ConnectionError as e:
        logger.error(f"[{sensor_id}] Error de conexion: {e}")
    except socket.timeout:
        logger.error(f"[{sensor_id}] Timeout al comunicarse con el servidor")
    except OSError as e:
        logger.error(f"[{sensor_id}] Error de socket: {e}")
    finally:
        # 5. Desconexión limpia
        if sock:
            try:
                msg_disconnect = f"DISCONNECT|{sensor_id}\r\n"
                enviar_mensaje(sock, msg_disconnect)
                logger.info(f"[{sensor_id}] Enviado DISCONNECT")
            except OSError:
                pass
            finally:
                sock.close()
                logger.info(f"[{sensor_id}] Socket cerrado")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Simulador de sensores IoT (Modulo 3)"
    )
    parser.add_argument(
        "--host",
        type=str,
        required=True,
        help="Hostname del servidor IOTP (resolver por DNS, NO usar IP)",
    )
    parser.add_argument(
        "--port",
        type=int,
        required=True,
        help="Puerto del servidor IOTP",
    )
    parser.add_argument(
        "--count",
        type=int,
        default=5,
        help="Numero de sensores a simular (default: 5)",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=3.0,
        help="Segundos entre envios de DATA (default: 3.0)",
    )
    parser.add_argument(
        "--chaos",
        action="store_true",
        help="Activar modo caos: inyectar valores fuera de rango cada ~10 envios",
    )
    parser.add_argument(
        "--config",
        type=str,
        default="config.json",
        help="Ruta al archivo de configuracion (default: config.json)",
    )
    args = parser.parse_args()

    # Cargar configuración
    config = cargar_config(args.config)
    tipos_config = config["tipos_sensor"]
    password = config["credenciales"]["password"]

    logger.info(
        f"Iniciando simulador: host={args.host}, port={args.port}, "
        f"sensores={args.count}, intervalo={args.interval}s, caos={args.chaos}"
    )

    # Manejar Ctrl+C para detención limpia
    def manejador_senal(sig, frame):
        logger.info("Senal de detencion recibida, cerrando sensores...")
        detener.set()

    signal.signal(signal.SIGINT, manejador_senal)
    signal.signal(signal.SIGTERM, manejador_senal)

    # Crear hilos de sensores (round-robin por tipo)
    hilos: list[threading.Thread] = []
    for i in range(args.count):
        tipo = TIPOS_SENSOR[i % len(TIPOS_SENSOR)]
        sensor_id = generar_id(tipo, i + 1)
        config_tipo = tipos_config[tipo]

        hilo = threading.Thread(
            target=hilo_sensor,
            args=(sensor_id, tipo, args.host, args.port, password,
                  config_tipo, args.interval, args.chaos),
            name=sensor_id,
            daemon=True,
        )
        hilos.append(hilo)
        logger.info(f"Creado sensor: id={sensor_id}, tipo={tipo}")

    # Arrancar todos los hilos
    for hilo in hilos:
        hilo.start()

    # Esperar a que todos terminen (o señal de detención)
    try:
        while not detener.is_set():
            # Verificar que al menos un hilo siga vivo
            vivos = [h for h in hilos if h.is_alive()]
            if not vivos:
                logger.info("Todos los sensores han terminado")
                break
            detener.wait(timeout=1.0)
    except KeyboardInterrupt:
        logger.info("KeyboardInterrupt recibido")
        detener.set()

    # Esperar a que los hilos terminen limpiamente
    logger.info("Esperando a que los sensores se desconecten...")
    for hilo in hilos:
        hilo.join(timeout=5.0)

    logger.info("Simulador finalizado")


if __name__ == "__main__":
    main()
