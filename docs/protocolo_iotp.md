# Especificación Formal del Protocolo IOTP
## IoT Transport Protocol — Versión 1.0

**Proyecto:** Sistema de Monitoreo IoT  
**Materia:** Internet: Arquitectura y Protocolos — Universidad EAFIT, 2026-1  
**Estado:** Definitivo

---

## Tabla de Contenidos

1. [Resumen del Protocolo](#1-resumen-del-protocolo)
2. [Formato de Mensaje (ABNF)](#2-formato-de-mensaje-abnf)
3. [Tabla de Opcodes](#3-tabla-de-opcodes)
4. [Diagramas de Secuencia](#4-diagramas-de-secuencia)
5. [Códigos de Error](#5-códigos-de-error)
6. [Umbrales de Anomalía](#6-umbrales-de-anomalía)
7. [Manejo de Errores](#7-manejo-de-errores)

---

## 1. Resumen del Protocolo

### 1.1 Propósito

IOTP (IoT Transport Protocol) es un protocolo de capa de aplicación diseñado para la comunicación entre sensores industriales simulados, un servidor central de procesamiento y operadores humanos. Su objetivo es permitir:

- El registro autenticado de clientes (sensores y operadores).
- La transmisión de mediciones de sensores al servidor central.
- La detección y propagación de alertas por anomalías.
- La consulta del estado del sistema por parte de operadores.
- La desconexión ordenada de clientes.

### 1.2 Modelo de Comunicación

IOTP es un protocolo **cliente–servidor**, **orientado a sesión** y **stateful**:

- **Stateful:** cada conexión tiene un estado de sesión. Un cliente debe autenticarse mediante `REGISTER` antes de enviar cualquier otro mensaje. El servidor mantiene el estado de la sesión por conexión.
- **Request–Response:** todo mensaje enviado por un cliente recibe exactamente una respuesta del servidor (sobre TCP).
- **Push del servidor:** el servidor puede enviar mensajes `ALERT` a operadores conectados de forma proactiva, sin solicitud previa.

### 1.3 Transporte

| Característica     | Valor                                      |
|--------------------|--------------------------------------------|
| Protocolo base     | TCP (principal), UDP (opcional para DATA)  |
| Puerto IOTP        | 9000/tcp (por defecto)                     |
| Puerto HTTP        | 8080/tcp (interfaz web, fuera de IOTP)     |
| Puerto Auth        | 9001/tcp (servicio de autenticación)       |
| Codificación       | ASCII (texto plano, 7 bits)               |
| Terminador         | `\r\n` (CR LF, 0x0D 0x0A)                |
| Delimitador campos | `|` (pipe, 0x7C)                          |
| Timeout lectura    | 30 segundos                                |

### 1.4 Roles de Cliente

| Rol        | Descripción                                                      |
|------------|------------------------------------------------------------------|
| `sensor`   | Envía mediciones periódicas mediante `DATA`. Solo puede enviar `REGISTER`, `DATA` y `DISCONNECT`. |
| `operator` | Monitorea el sistema. Puede enviar `REGISTER`, `QUERY`, `STATUS` y `DISCONNECT`. Recibe `ALERT` de forma asíncrona. |

---

## 2. Formato de Mensaje (ABNF)

La siguiente gramática describe la sintaxis de todos los mensajes IOTP en notación ABNF (RFC 5234 simplificado):

```abnf
; Mensaje genérico
message         = opcode *( "|" field ) CRLF

CRLF            = %x0D %x0A          ; \r\n
opcode          = 1*UPPER
field           = *( VCHAR / SP )    ; texto ASCII imprimible
UPPER           = %x41-5A            ; A–Z
VCHAR           = %x21-7E            ; caracteres imprimibles excepto "|"
SP              = %x20

; Tipos de datos de campos
integer         = 1*DIGIT
decimal         = 1*DIGIT "." 1*DIGIT   ; separador decimal SIEMPRE "."
timestamp       = date "T" time "Z"     ; ISO-8601 UTC
date            = 4DIGIT "-" 2DIGIT "-" 2DIGIT
time            = 2DIGIT ":" 2DIGIT ":" 2DIGIT
sensor_id       = 1*( ALPHA / DIGIT / "_" / "-" )
rol             = "sensor" / "operator"
username        = 1*( ALPHA / DIGIT / "_" )
password        = 1*VCHAR_NO_PIPE
VCHAR_NO_PIPE   = %x21-7B / %x7D-7E    ; imprimible excepto "|"

; Mensajes específicos
msg_register    = "REGISTER" "|" rol "|" username "|" password CRLF
msg_ack         = "ACK" "|" 1*VCHAR_NO_PIPE CRLF
msg_error       = "ERROR" "|" error_code "|" 1*VCHAR_NO_PIPE CRLF
msg_data        = "DATA" "|" sensor_id "|" sensor_type "|" decimal "|" timestamp CRLF
msg_alert       = "ALERT" "|" sensor_id "|" sensor_type "|" decimal "|" decimal "|" timestamp CRLF
msg_query       = "QUERY" "|" query_target CRLF
msg_result      = "RESULT" "|" query_target "|" 1*VCHAR_NO_PIPE CRLF
msg_status      = "STATUS" CRLF
msg_statusr     = "STATUSR" "|" integer "|" integer "|" integer CRLF
msg_disconnect  = "DISCONNECT" "|" ( sensor_id / username ) CRLF

sensor_type     = "temperatura" / "vibracion" / "energia"
query_target    = "SENSORS" / "MEASUREMENTS" / "ALERTS"
error_code      = "NOT_AUTHENTICATED" / "UNKNOWN_OP" / "AUTH_UNAVAILABLE"
               / "AUTH_FAILED" / "MALFORMED" / "FORBIDDEN"
```

### 2.1 Restricciones lexicales

- **I1:** Todo contenido es ASCII de 7 bits. Ningún byte con valor > 0x7E es válido.
- **I2:** El delimitador `|` no puede aparecer dentro de un campo.
- **I5:** Los valores numéricos decimales usan `.` como separador. Jamás `,`.
- El terminador `\r\n` es obligatorio al final de cada mensaje. Un mensaje sin `\r\n` es incompleto y el receptor debe esperar hasta recibirlo o hasta que expire el timeout.

---

## 3. Tabla de Opcodes

### 3.1 Mensajes de cliente a servidor

| Opcode       | Emisor    | Receptor  | Campos                                          | Descripción                              |
|--------------|-----------|-----------|-------------------------------------------------|------------------------------------------|
| `REGISTER`   | Cliente   | Servidor  | `rol\|username\|password`                       | Autenticación e inicio de sesión         |
| `DATA`       | Sensor    | Servidor  | `sensor_id\|tipo\|valor\|timestamp`             | Envío de una medición                    |
| `QUERY`      | Operador  | Servidor  | `target`                                        | Consulta de datos del sistema            |
| `STATUS`     | Operador  | Servidor  | *(sin campos)*                                  | Solicitud de estado general del servidor |
| `DISCONNECT` | Cliente   | Servidor  | `id`                                            | Desconexión limpia y liberación de sesión|

### 3.2 Mensajes de servidor a cliente

| Opcode     | Emisor   | Receptor  | Campos                                              | Descripción                              |
|------------|----------|-----------|-----------------------------------------------------|------------------------------------------|
| `ACK`      | Servidor | Cliente   | `mensaje`                                           | Confirmación de operación exitosa        |
| `ERROR`    | Servidor | Cliente   | `código\|descripción`                               | Notificación de error                    |
| `ALERT`    | Servidor | Operador  | `sensor_id\|tipo\|valor\|umbral\|timestamp`         | Notificación asíncrona de anomalía       |
| `RESULT`   | Servidor | Operador  | `tipo_query\|datos`                                 | Respuesta a `QUERY`                      |
| `STATUSR`  | Servidor | Operador  | `sensores_activos\|operadores_conectados\|uptime_s` | Respuesta a `STATUS`                     |

### 3.3 Detalle de campos por opcode

#### `REGISTER`
| Campo      | Tipo     | Descripción                              |
|------------|----------|------------------------------------------|
| `rol`      | string   | `sensor` o `operator`                    |
| `username` | string   | Identificador de usuario (alfanumérico)  |
| `password` | string   | Contraseña en texto plano                |

Ejemplo: `REGISTER|sensor|temp01|secreto123\r\n`

#### `ACK`
| Campo     | Tipo   | Descripción                                 |
|-----------|--------|---------------------------------------------|
| `mensaje` | string | Texto descriptivo de la confirmación        |

Ejemplos:
- `ACK|Registered as sensor\r\n`
- `ACK|Data received\r\n`

#### `ERROR`
| Campo         | Tipo   | Descripción                            |
|---------------|--------|----------------------------------------|
| `código`      | string | Código de error (ver §5)               |
| `descripción` | string | Mensaje legible por humanos            |

Ejemplo: `ERROR|NOT_AUTHENTICATED|Debe registrarse antes de enviar datos\r\n`

#### `DATA`
| Campo       | Tipo      | Descripción                                   |
|-------------|-----------|-----------------------------------------------|
| `sensor_id` | string    | Identificador único del sensor                |
| `tipo`      | string    | `temperatura`, `vibracion` o `energia`        |
| `valor`     | decimal   | Medición con punto decimal (ej: `72.50`)      |
| `timestamp` | timestamp | Momento de la medición en ISO-8601 UTC        |

Ejemplo: `DATA|temp01|temperatura|72.50|2026-04-02T14:30:00Z\r\n`

#### `ALERT`
| Campo       | Tipo      | Descripción                                      |
|-------------|-----------|--------------------------------------------------|
| `sensor_id` | string    | Sensor que generó la anomalía                    |
| `tipo`      | string    | Tipo de sensor                                   |
| `valor`     | decimal   | Valor que excedió el umbral                      |
| `umbral`    | decimal   | Umbral configurado que fue superado              |
| `timestamp` | timestamp | Momento en que se detectó la anomalía            |

Ejemplo: `ALERT|temp01|temperatura|85.30|80.00|2026-04-02T14:31:05Z\r\n`

#### `QUERY`
| Campo    | Tipo   | Valores válidos                          | Descripción               |
|----------|--------|------------------------------------------|---------------------------|
| `target` | string | `SENSORS`, `MEASUREMENTS`, `ALERTS`      | Tipo de consulta          |

Ejemplos:
- `QUERY|SENSORS\r\n` — lista de sensores activos
- `QUERY|MEASUREMENTS\r\n` — mediciones recientes
- `QUERY|ALERTS\r\n` — alertas registradas

#### `RESULT`
| Campo        | Tipo   | Descripción                                              |
|--------------|--------|----------------------------------------------------------|
| `tipo_query` | string | El mismo `target` de la `QUERY` que origina este resultado |
| `datos`      | string | JSON o CSV con los resultados (sin `|` interno)          |

Ejemplo: `RESULT|SENSORS|[{"id":"temp01","tipo":"temperatura","activo":true}]\r\n`

#### `STATUS`
Sin campos adicionales.

Ejemplo: `STATUS\r\n`

#### `STATUSR`
| Campo                  | Tipo    | Descripción                              |
|------------------------|---------|------------------------------------------|
| `sensores_activos`     | integer | Número de sensores con sesión activa     |
| `operadores_conectados`| integer | Número de operadores con sesión activa   |
| `uptime_s`             | integer | Tiempo en segundos desde el inicio del servidor |

Ejemplo: `STATUSR|5|2|3600\r\n`

#### `DISCONNECT`
| Campo | Tipo   | Descripción                                         |
|-------|--------|-----------------------------------------------------|
| `id`  | string | `sensor_id` o `username` del cliente que se desconecta |

Ejemplo: `DISCONNECT|temp01\r\n`

---

## 4. Diagramas de Secuencia

Los diagramas muestran el flujo de mensajes entre los actores del sistema. Los tiempos fluyen de arriba hacia abajo.

### 4.1 Registro exitoso de sensor

```
Sensor                     Servidor                  Auth Service
  │                           │                           │
  │── REGISTER|sensor|        │                           │
  │   temp01|pass123 ────────▶│                           │
  │                           │── AUTH|temp01|pass123 ───▶│
  │                           │                           │ (valida users.json)
  │                           │◀── OK|sensor ────────────│
  │◀── ACK|Registered         │                           │
  │    as sensor ─────────────│                           │
  │                           │                           │
  │   [sesión activa: sensor] │                           │
```

### 4.2 Registro exitoso de operador

```
Operador                   Servidor                  Auth Service
  │                           │                           │
  │── REGISTER|operator|      │                           │
  │   admin|pass456 ─────────▶│                           │
  │                           │── AUTH|admin|pass456 ────▶│
  │                           │◀── OK|operator ──────────│
  │◀── ACK|Registered         │                           │
  │    as operator ───────────│                           │
  │                           │                           │
  │   [sesión activa: operador]│                          │
```

### 4.3 Envío de medición normal (sin anomalía)

```
Sensor                     Servidor
  │                           │
  │ [sesión activa]           │
  │                           │
  │── DATA|temp01|temperatura │
  │   |72.50|2026-04-02T      │
  │   14:30:00Z ─────────────▶│
  │                           │ (almacena medición)
  │                           │ (72.50 dentro de rango [15.0, 80.0])
  │◀── ACK|Data received ─────│
  │                           │
```

### 4.4 Envío de medición que dispara alerta

```
Sensor           Servidor              Operador(es) conectados
  │                  │                          │
  │ [sesión activa]  │                          │
  │                  │                          │
  │── DATA|temp01|temperatura                   │
  │   |85.30|2026-04-02T14:31:05Z ────────────▶│
  │                  │ (85.30 > umbral 80.00)   │
  │                  │ (genera alerta)           │
  │◀── ACK|Data received                        │
  │                  │── ALERT|temp01|temperatura│
  │                  │   |85.30|80.00|           │
  │                  │   2026-04-02T14:31:05Z ──▶│
  │                  │                          │ (operador muestra alerta en GUI)
```

> **Nota:** el `ACK` al sensor y el `ALERT` a los operadores son eventos independientes. El servidor envía el `ALERT` a todos los operadores con sesión activa en ese momento.

### 4.5 Consulta QUERY

```
Operador                   Servidor
  │                           │
  │ [sesión activa]           │
  │                           │
  │── QUERY|SENSORS ─────────▶│
  │                           │ (consulta sensor_manager)
  │◀── RESULT|SENSORS|        │
  │    [{"id":"temp01",...}] ──│
  │                           │
  │── QUERY|MEASUREMENTS ────▶│
  │                           │
  │◀── RESULT|MEASUREMENTS|   │
  │    [{"sensor":"temp01",   │
  │     "valor":72.50,...}] ──│
  │                           │
  │── QUERY|ALERTS ──────────▶│
  │                           │
  │◀── RESULT|ALERTS|         │
  │    [{"sensor":"temp01",   │
  │     "valor":85.30,...}] ──│
  │                           │
```

### 4.6 Consulta STATUS

```
Operador                   Servidor
  │                           │
  │ [sesión activa]           │
  │                           │
  │── STATUS ────────────────▶│
  │                           │ (cuenta sensores, operadores, calcula uptime)
  │◀── STATUSR|5|2|3600 ──────│
  │                           │
```

### 4.7 Desconexión limpia

```
Cliente                    Servidor
  │                           │
  │ [sesión activa]           │
  │                           │
  │── DISCONNECT|temp01 ─────▶│
  │                           │ (cierra sesión, libera recursos)
  │                           │ (no envía respuesta — fire and forget)
  X                           │
  [conexión cerrada]          │ (servidor cierra socket)
```

### 4.8 Fallo de autenticación (credenciales inválidas)

```
Cliente                    Servidor                  Auth Service
  │                           │                           │
  │── REGISTER|sensor|        │                           │
  │   temp01|wrongpass ──────▶│                           │
  │                           │── AUTH|temp01|wrongpass ─▶│
  │                           │◀── FAIL|invalid_credentials│
  │◀── ERROR|AUTH_FAILED|     │                           │
  │    Credenciales inválidas ─│                           │
  │                           │                           │
  │   [sesión NO iniciada]    │                           │
  X (el servidor puede        │                           │
    cerrar el socket)         │                           │
```

### 4.9 Auth Service no disponible

```
Cliente                    Servidor                  Auth Service
  │                           │                           │
  │── REGISTER|sensor|        │                           │
  │   temp01|pass123 ────────▶│                           X
  │                           │── AUTH|temp01|... ───────▶│
  │                           │   [timeout / conexión rechazada]
  │◀── ERROR|AUTH_UNAVAILABLE │
  │    |Servicio de auth no   │
  │    disponible ────────────│
  │                           │
  │   [sesión NO iniciada]    │
```

### 4.10 Mensaje antes de autenticarse (no autenticado)

```
Sensor                     Servidor
  │                           │
  │ [sin sesión activa]       │
  │                           │
  │── DATA|temp01|temperatura │
  │   |72.50|... ────────────▶│
  │                           │ (sesión no autenticada)
  │◀── ERROR|NOT_AUTHENTICATED│
  │    |Debe registrarse      │
  │    primero ───────────────│
  │                           │
```

---

## 5. Códigos de Error

| Código              | Situación que lo genera                                          | Acción esperada del cliente              |
|---------------------|------------------------------------------------------------------|------------------------------------------|
| `NOT_AUTHENTICATED` | El cliente envió un mensaje distinto de `REGISTER` sin haber completado el handshake. | Enviar `REGISTER` antes de continuar. |
| `UNKNOWN_OP`        | El opcode recibido no pertenece al conjunto definido en §3.      | Verificar el mensaje enviado. No reintentar el mismo mensaje. |
| `AUTH_FAILED`       | El Auth Service respondió `FAIL` — credenciales incorrectas o usuario inexistente. | Verificar credenciales. |
| `AUTH_UNAVAILABLE`  | El servidor no pudo conectar con el Auth Service (timeout o conexión rechazada). | Reintentar con backoff exponencial (máx. 5 intentos). |
| `MALFORMED`         | El mensaje no respeta la gramática ABNF (campos faltantes, delimitadores incorrectos, etc.). | Corregir el formato del mensaje. |
| `FORBIDDEN`         | Un sensor intentó enviar un mensaje reservado para operadores (ej. `QUERY`), o viceversa. | Verificar que el rol del cliente sea el correcto para el opcode. |

### 5.1 Formato completo del mensaje ERROR

```
ERROR|<código>|<descripción legible>\r\n
```

Ejemplos:
```
ERROR|NOT_AUTHENTICATED|Debe registrarse antes de enviar datos\r\n
ERROR|UNKNOWN_OP|Opcode desconocido: PING\r\n
ERROR|AUTH_FAILED|Credenciales invalidas para el usuario temp01\r\n
ERROR|AUTH_UNAVAILABLE|El servicio de autenticacion no esta disponible\r\n
ERROR|MALFORMED|Numero de campos incorrecto para opcode DATA\r\n
ERROR|FORBIDDEN|El rol sensor no puede enviar QUERY\r\n
```

---

## 6. Umbrales de Anomalía

El servidor evalúa cada medición recibida mediante `DATA` contra los siguientes umbrales. Si el valor cae fuera del rango normal, se genera un `ALERT` y se difunde a todos los operadores conectados.

| Tipo de sensor | Unidad | Rango normal        | Condición de alerta         |
|----------------|--------|---------------------|-----------------------------|
| `temperatura`  | °C     | 15.0 — 80.0         | `valor > 80.0` o `valor < 15.0` |
| `vibracion`    | mm/s   | 0.0 — 40.0          | `valor > 40.0`              |
| `energia`      | kW     | 0.0 — 450.0         | `valor > 450.0`             |

### 6.1 Lógica de detección

```
para cada mensaje DATA recibido:
    si valor > umbral_superior[tipo]:
        generar ALERT con umbral = umbral_superior[tipo]
    sino si valor < umbral_inferior[tipo]:
        generar ALERT con umbral = umbral_inferior[tipo]
    sino:
        no generar ALERT
```

### 6.2 Campo `umbral` en ALERT

El campo `umbral` del mensaje `ALERT` contiene el valor de umbral específico que fue cruzado (superior o inferior), no el rango completo.

Ejemplo — temperatura de 10.5 °C (por debajo del mínimo):
```
ALERT|temp01|temperatura|10.50|15.00|2026-04-02T14:45:00Z\r\n
```

Ejemplo — temperatura de 85.3 °C (por encima del máximo):
```
ALERT|temp01|temperatura|85.30|80.00|2026-04-02T14:31:05Z\r\n
```

---

## 7. Manejo de Errores

### 7.1 Timeout de lectura

- Cada conexión tiene un timeout de lectura de **30 segundos**.
- Si el servidor no recibe ningún byte durante 30 segundos, cierra el socket y libera los recursos de la sesión.
- Si el cliente no recibe respuesta del servidor en 30 segundos, debe cerrar el socket y reconectar con backoff exponencial.

### 7.2 Mensaje malformado

Un mensaje malformado es aquel que:
- No tiene terminador `\r\n` al final (incompleto).
- Tiene un número incorrecto de campos para su opcode.
- Contiene caracteres no ASCII (byte > 0x7E).
- Tiene un campo numérico con formato inválido (ej: `72,5` en lugar de `72.5`).
- Contiene el carácter `|` dentro del valor de un campo.

**Respuesta del servidor:** `ERROR|MALFORMED|<descripción>\r\n`

La sesión NO se cierra automáticamente por un mensaje malformado. El cliente puede enviar el mensaje corregido.

### 7.3 Opcode desconocido

Si el servidor recibe un opcode que no está en el conjunto definido en §3, responde:

```
ERROR|UNKNOWN_OP|<opcode recibido>\r\n
```

La sesión permanece activa.

### 7.4 Reconexión con backoff exponencial

Los clientes deben implementar reconexión automática con backoff exponencial:

```
intento 1: esperar 1 segundo
intento 2: esperar 2 segundos
intento 3: esperar 4 segundos
intento 4: esperar 8 segundos
intento 5: esperar 16 segundos
intento 6+: abandonar y reportar fallo
```

Máximo de reintentos: **5**. Si se superan, el cliente debe reportar el error al usuario o proceso supervisor.

### 7.5 Invariantes del protocolo (resumen)

| ID | Invariante                                                                                      |
|----|-------------------------------------------------------------------------------------------------|
| I1 | Todo mensaje es texto plano ASCII de 7 bits. Sin contenido binario.                             |
| I2 | Estructura fija `OPCODE\|CAMPO1\|CAMPO2\|...\r\n`. Delimitador `\|`, terminador `\r\n`.        |
| I3 | Opcodes forman un conjunto cerrado. Opcode desconocido → `ERROR\|UNKNOWN_OP\|<opcode>`.         |
| I4 | Todo request recibe exactamente una response (TCP). Fire-and-forget solo para DATA sobre UDP.   |
| I5 | Campos numéricos usan `.` como separador decimal, nunca `,`.                                    |
| I6 | Protocolo stateful: REGISTER exitoso es requisito previo a cualquier otro comando.              |

---

*Fin de la especificación — IOTP v1.0*
