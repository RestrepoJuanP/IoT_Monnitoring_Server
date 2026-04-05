#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <pthread.h>

typedef struct {
    FILE *log_file;
    pthread_mutex_t lock;
} Logger;

/* Inicializa el logger. Abre el archivo y stdout. Retorna 0 si OK, -1 si error. */
int logger_init(Logger *logger, const char *filepath);

/* Cierra el archivo de log y destruye el mutex. */
void logger_destroy(Logger *logger);

/* Log genérico con timestamp ISO-8601. Escribe a stdout y al archivo. */
void logger_log(Logger *logger, const char *fmt, ...);

/* Log de request/response con IP y puerto del cliente. */
void logger_log_request(Logger *logger, const char *client_ip, int client_port,
                        const char *request, const char *response);

#endif
