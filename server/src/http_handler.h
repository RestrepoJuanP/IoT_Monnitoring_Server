#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

#include "logger.h"
#include "auth_client.h"
#include "sensor_manager.h"

typedef struct {
    int            port;
    char           web_root[256];    /* ruta a la carpeta con HTML estáticos */
    char           auth_host[128];
    char           auth_port[8];
    Logger         *logger;
    SensorManager  *sm;              /* acceso al estado del sistema para /status */
} HttpConfig;

/*
 * Función de entrada para el hilo HTTP.
 * Recibe un puntero a HttpConfig.
 * Escucha en el puerto configurado y sirve archivos HTML estáticos.
 * Solo acepta GET. Retorna 405 para otros métodos.
 *
 * Rutas:
 *   GET /             → login.html
 *   GET /dashboard    → dashboard.html
 *   GET /login?user=X&pass=Y → autenticar y redirigir (302)
 *   GET /status       → JSON con estado del sistema
 *   Otros             → 404
 */
void *http_handler_thread(void *arg);

#endif
