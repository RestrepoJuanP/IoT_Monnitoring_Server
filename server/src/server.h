#ifndef SERVER_H
#define SERVER_H

#include "logger.h"
#include "sensor_manager.h"

#define READ_TIMEOUT_SEC  30
#define RECV_BUF_SIZE     4096

/* Estado de sesión de un cliente conectado */
typedef struct {
    int  fd;
    char client_ip[64];
    int  client_port;
    int  authenticated;
    char role[16];       /* "sensor" o "operator" */
    char username[64];
    char auth_host[128];
    char auth_port[8];
    Logger        *logger;
    SensorManager *sm;
} ClientSession;

/*
 * Función de entrada para el hilo que maneja un cliente IOTP.
 * Recibe un puntero a ClientSession (debe ser liberado por el hilo).
 */
void *client_handler_thread(void *arg);

#endif
