# Cliente Operador GUI — Módulo 3

Interfaz gráfica para operadores del sistema IoT. Permite monitorear sensores, ver alertas en tiempo real y consultar el estado del servidor.

## Dependencias

- CMake >= 3.16
- Compilador C++17 (GCC, Clang, MSVC)
- OpenGL 3.3+
- GLFW y Dear ImGui se descargan automáticamente via FetchContent

## Compilación

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

En Windows con MSVC:
```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

## Ejecución

```bash
./iot_operator
```

La aplicación abre una ventana donde se configura:
- **Host**: hostname del servidor IOTP (se resuelve por DNS)
- **Puerto**: puerto del servidor IOTP (default: 9000)
- **Usuario/Clave**: credenciales del operador

## Funcionalidades

- Indicador de conexión: verde (conectado), amarillo (conectando/reconectando), rojo (desconectado)
- Tabla de sensores activos con última medición
- Panel de alertas en tiempo real (más recientes arriba)
- Botones: Consultar Sensores, Consultar Mediciones, Consultar Alertas, Estado del Servidor, Desconectar
- Reconexión automática con backoff exponencial (máx. 5 intentos)
- La GUI nunca se bloquea por operaciones de red
