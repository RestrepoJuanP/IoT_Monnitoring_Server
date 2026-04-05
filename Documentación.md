# CLAUDE.md — Proyecto IoT Monitoring System

> Contiene toda la especificación, arquitectura modular, invariantes y convenciones del proyecto.

---

## Visión General

Sistema distribuido de monitoreo de sensores IoT para la materia "Internet: Arquitectura y Protocolos" (Universidad EAFIT, 2026-1). El sistema simula una plataforma industrial donde sensores reportan mediciones a un servidor central, que detecta anomalías y notifica a operadores en tiempo real.

## Arquitectura

```
                    ┌──────────────────┐
                    │   AWS Route 53   │
                    │  DNS resolution  │
                    └────────┬─────────┘
                             │
                    ┌────────▼─────────┐
                    │   AWS EC2        │
                    │  ┌─────────────┐ │
                    │  │   Docker    │ │
                    │  │ ┌─────────┐ │ │
                    │  │ │Server(C)│ │ │  ← Módulo 2
                    │  │ └─────────┘ │ │
                    │  │ ┌─────────┐ │ │
                    │  │ │Auth Svc │ │ │  ← Módulo 4
                    │  │ └─────────┘ │ │
                    │  └─────────────┘ │
                    └──┬──────────┬────┘  ← Módulo 5
                       │          │
            ┌──────────▼┐   ┌────▼──────────┐
            │ Sensores   │   │  Operador GUI │
            │ (Python)   │   │  (C++)        │
            └────────────┘   └───────────────┘
                  └── Módulo 3 ──┘
```

Protocolo de comunicación: **IOTP** (Módulo 1) — basado en texto, delimitado por `|`, terminado por `\r\n`.

---

## Estructura del Repositorio (esperada)

```
iot-monitoring/
├── CLAUDE.md                  ← este archivo
├── README.md
├── DEPLOY.md                  ← instrucciones de despliegue AWS
├── docs/
│   └── protocolo_iotp.md      ← especificación formal del protocolo (Módulo 1)
├── server/                    ← Módulo 2: servidor en C
│   ├── src/
│   │   ├── main.c
│   │   ├── server.c / server.h
│   │   ├── protocol.c / protocol.h
│   │   ├── auth_client.c / auth_client.h
│   │   ├── http_handler.c / http_handler.h
│   │   ├── logger.c / logger.h
│   │   └── sensor_manager.c / sensor_manager.h
│   ├── web/                   ← HTML estático para interfaz web (Módulo 4)
│   │   ├── index.html
│   │   ├── login.html
│   │   └── dashboard.html
│   ├── Makefile
│   └── Dockerfile
├── clients/                   ← Módulo 3
│   ├── sensors/               ← sensores simulados (Python)
│   │   ├── sensor_simulator.py
│   │   ├── requirements.txt
│   │   └── config.json
│   └── operator/              ← operador GUI (C++)
│       ├── src/
│       │   ├── main.cpp
│       │   ├── network_thread.cpp / .h
│       │   └── gui.cpp / .h
│       ├── CMakeLists.txt
│       └── README.md
├── auth-service/              ← Módulo 4: servicio de autenticación (Python)
│   ├── auth_server.py
│   ├── users.json
│   ├── requirements.txt
│   └── Dockerfile
├── docker-compose.yml         ← Módulo 5
└── infra/                     ← Módulo 5: scripts de despliegue
    └── setup_ec2.sh
```

---

## Módulo 1: Protocolo IOTP

### Formato de mensaje

```
OPCODE|CAMPO1|CAMPO2|...\r\n
```

### Opcodes definidos

| Opcode     | Dirección         | Campos                                      | Descripción                        |
|------------|-------------------|---------------------------------------------|------------------------------------|
| `REGISTER` | Cliente → Servidor | `REGISTER|rol|username|password`            | Registro/autenticación             |
| `ACK`      | Servidor → Cliente | `ACK|mensaje`                               | Confirmación exitosa               |
| `ERROR`    | Servidor → Cliente | `ERROR|código|descripción`                  | Error (NOT_AUTHENTICATED, UNKNOWN_OP, AUTH_UNAVAILABLE, etc.) |
| `DATA`     | Sensor → Servidor  | `DATA|sensor_id|tipo|valor|timestamp`       | Envío de medición                  |
| `ALERT`    | Servidor → Operador| `ALERT|sensor_id|tipo|valor|umbral|timestamp` | Notificación de anomalía         |
| `QUERY`    | Operador → Servidor| `QUERY|target` (target: SENSORS, MEASUREMENTS, ALERTS) | Consulta de estado   |
| `RESULT`   | Servidor → Operador| `RESULT|tipo_query|datos_json_o_csv`        | Respuesta a QUERY                  |
| `STATUS`   | Operador → Servidor| `STATUS`                                    | Estado general del servidor        |
| `STATUSR`  | Servidor → Operador| `STATUSR|sensores_activos|operadores|uptime` | Respuesta a STATUS               |
| `DISCONNECT`| Cliente → Servidor| `DISCONNECT|id`                             | Desconexión limpia                 |

### Invariantes del protocolo

- I1: Todo mensaje es texto plano ASCII. Sin contenido binario.
- I2: Estructura fija `OPCODE|CAMPO1|CAMPO2|...\r\n`. Delimitador `|`, terminador `\r\n`.
- I3: Opcodes forman un conjunto cerrado. Opcode desconocido → `ERROR|UNKNOWN_OP|<opcode recibido>`.
- I4: Todo request recibe exactamente una response (TCP). Fire-and-forget solo para DATA sobre UDP si se implementa.
- I5: Campos numéricos usan `.` como separador decimal, nunca `,`.
- I6: Protocolo stateful: el cliente debe completar REGISTER exitoso antes de cualquier otro comando.

### Flujo de handshake

```
Cliente                          Servidor                      Auth Service
  │                                │                               │
  │─── REGISTER|sensor|temp01|pw ─▶│                               │
  │                                │── validar(temp01, pw) ───────▶│
  │                                │◀─ OK|sensor ─────────────────│
  │◀── ACK|Registered as sensor ───│                               │
  │                                │                               │
  │─── DATA|temp01|temp|72.5|ts ──▶│                               │
  │◀── ACK|Data received ─────────│                               │
```

### Umbrales de anomalía (por defecto)

| Tipo         | Rango normal       | Alerta si       |
|--------------|--------------------|-----------------|
| temperatura  | 15.0 – 90.0 °C    | > 80.0 o < 15.0 |
| vibración    | 0.0 – 50.0 mm/s   | > 40.0          |
| energía      | 0.0 – 500.0 kW    | > 450.0         |

---

## Módulo 2: Servidor Central (C)

### Restricciones técnicas

- Lenguaje: **C exclusivamente** (C11 o C17).
- API: Sockets Berkeley (`socket`, `bind`, `listen`, `accept`, `send`, `recv`).
- Concurrencia: `pthread` (un hilo por conexión aceptada).
- DNS: `getaddrinfo()` para resolver nombres. **Cero IPs literales en el código**.
- Ejecución: `./server <puerto> <archivoDeLogs>`.

### Invariantes

- I1: Nunca termina por error de red. Capturar todo error de I/O, loguear, continuar.
- I2: No almacena usuarios localmente. Delega a auth service (Módulo 4).
- I3: Log mínimo: timestamp ISO-8601, IP cliente, puerto, mensaje recibido, respuesta enviada.
- I4: Cero IPs literales. Todo se resuelve por DNS.
- I5: Soportar ≥5 sensores + ≥2 operadores simultáneos.
- I6: Liberar todos los recursos (fd, memoria, hilos) al desconectar cliente.
- I7: Compila y ejecuta dentro de Docker.

### Componentes internos

- `main.c` → parseo de args, setup de sockets, loop accept.
- `protocol.c` → parsear mensajes IOTP, generar respuestas.
- `auth_client.c` → conectar al servicio de auth por socket TCP.
- `http_handler.c` → servidor HTTP embebido (hilo dedicado en otro puerto).
- `logger.c` → escritura a stdout + archivo con formato ISO-8601.
- `sensor_manager.c` → estructura compartida de sensores activos, mediciones, detección de anomalías. Protegida con mutex.

---

## Módulo 3: Clientes

### Sensores simulados (Python)

- Argumento: `python sensor_simulator.py --host <hostname> --port <puerto> --count 5`
- Crea N hilos (≥5), cada uno con tipo de sensor, ID único, intervalo configurable.
- Rangos: temperatura [15, 90], vibración [0, 50], energía [0, 500].
- Modo "caos": inyectar valores fuera de rango periódicamente para disparar alertas.
- Flujo por hilo: conectar TCP → REGISTER → loop(DATA cada T seg) → DISCONNECT al terminar.

### Operador GUI (C++)

- Framework GUI: Qt, GTK, Dear ImGui, o FLTK.
- Hilo de red separado del hilo de GUI.
- Vistas: sensores activos (tabla), mediciones recientes, panel de alertas, indicador de conexión.
- Acciones: botón QUERY (sensores/mediciones/alertas), botón STATUS, botón desconectar.
- Nunca congelar la GUI por operación de red.

### Invariantes de ambos clientes

- I1: Resolver hostname por DNS, nunca IP literal. Backoff exponencial (max 5 reintentos).
- I2: REGISTER → ACK obligatorio antes de DATA. Si rechazado, no enviar datos.
- I3: Operador muestra estado de conexión visiblemente.
- I4: Rangos de medición predefinidos.
- I5: Formato de mensajes exacto según protocolo Módulo 1.
- I6: Timeout de lectura (30s). Si no hay respuesta, cerrar limpiamente.

---

## Módulo 4: Web + Autenticación

### Servidor HTTP (embebido en C o micro-servicio aparte)

- Solo acepta GET. Otros métodos → 405 Method Not Allowed.
- Rutas: `/` → login.html, `/dashboard` → dashboard.html, `/login?user=X&pass=Y` → validar y redirigir.
- Headers correctos: Content-Type, Content-Length.
- Puerto separado del protocolo IOTP (ej: 8080 para HTTP, 9000 para IOTP).

### Servicio de autenticación (Python)

- Proceso/contenedor independiente.
- Escucha en un puerto TCP (ej: 9001).
- Protocolo simple: recibe `AUTH|username|password\r\n`, responde `OK|rol\r\n` o `FAIL|reason\r\n`.
- Base de datos: archivo `users.json` con estructura `{ "username": {"password": "...", "role": "sensor|operator"} }`.
- Se localiza por nombre de dominio, no IP literal.

### Invariantes

- I1: HTTP solo GET.
- I2: Auth es proceso separado. Servidor central nunca toca la DB de usuarios directamente.
- I3: Contraseñas no se persisten en el servidor central ni en logs.
- I4: Fail-closed: si auth no disponible → ERROR|AUTH_UNAVAILABLE.
- I5: Content-Type correcto siempre.
- I6: Auth service localizado por DNS.

---

## Módulo 5: Infraestructura (AWS + Docker + DNS)

### Docker

- Dockerfile en raíz del directorio `server/` (o raíz del repo si es mono-contenedor).
- Multi-stage build recomendado: stage 1 compila con gcc, stage 2 copia binario a imagen slim.
- Imagen final < 500 MB.
- ENTRYPOINT: `./server <puerto> <logfile>`.
- Logs: montar volumen `-v` o capturar con `docker logs`.
- docker-compose.yml opcional para levantar server + auth juntos.

### AWS EC2

- Instancia: t2.micro o t3.micro (free tier).
- SO: Ubuntu 22.04 o Amazon Linux 2.
- IP elástica asignada.
- Security Group: solo puertos necesarios (22/tcp SSH, 8080/tcp HTTP, 9000/tcp IOTP, 9001/tcp auth).

### Route 53

- Registro A apuntando el dominio (ej: `iot-monitoring.example.com`) a la IP elástica.
- TTL bajo (300s) durante desarrollo.

### Invariantes

- I1: Dockerfile autosuficiente. `docker build && docker run` en máquina limpia levanta el servidor.
- I2: Solo puertos necesarios abiertos en Security Group.
- I3: DNS resuelve correctamente a IP pública.
- I4: Contenedor sin estado persistente en su filesystem.
- I5: DEPLOY.md con instrucciones paso a paso.
- I6: Imagen Docker < 500 MB.

---

## Convenciones de código

- **C (servidor):** indentación 4 espacios, nombres `snake_case`, constantes `UPPER_CASE`.
- **Python (sensores, auth):** PEP 8, type hints donde sea útil, f-strings.
- **C++ (operador):** estilo similar a C, clases `PascalCase`, métodos `camelCase`.
- **Commits:** mensajes descriptivos en español, prefijo con módulo: `[M2] Implementar logger`, `[M3] Agregar modo caos a sensores`.
- **Branches:** `main`, `dev`, feature branches por módulo: `feat/m2-server`, `feat/m3-sensors`, etc.

---

## Orden de desarrollo recomendado

1. **Módulo 1** → definir protocolo completo en `docs/protocolo_iotp.md`.
2. **Módulo 4** → auth service (es simple y el servidor lo necesita).
3. **Módulo 2** → servidor en C (componente más complejo y central).
4. **Módulo 3** → clientes (necesitan el servidor corriendo para probar).
5. **Módulo 5** → Docker + AWS (se puede ir armando en paralelo desde el Módulo 2).
