#include "logger.h"
#include <stdarg.h>
#include <string.h>
#include <time.h>

static void get_iso8601(char *buf, size_t len)
{
    time_t now = time(NULL);
    struct tm tm_buf;
    gmtime_r(&now, &tm_buf);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
}

int logger_init(Logger *logger, const char *filepath)
{
    if (!logger || !filepath) return -1;

    logger->log_file = fopen(filepath, "a");
    if (!logger->log_file) {
        perror("logger_init: fopen");
        return -1;
    }

    if (pthread_mutex_init(&logger->lock, NULL) != 0) {
        fclose(logger->log_file);
        return -1;
    }

    return 0;
}

void logger_destroy(Logger *logger)
{
    if (!logger) return;
    pthread_mutex_lock(&logger->lock);
    if (logger->log_file) {
        fclose(logger->log_file);
        logger->log_file = NULL;
    }
    pthread_mutex_unlock(&logger->lock);
    pthread_mutex_destroy(&logger->lock);
}

void logger_log(Logger *logger, const char *fmt, ...)
{
    if (!logger) return;

    char ts[32];
    get_iso8601(ts, sizeof(ts));

    pthread_mutex_lock(&logger->lock);

    va_list args;

    /* stdout */
    fprintf(stdout, "[%s] ", ts);
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
    fprintf(stdout, "\n");
    fflush(stdout);

    /* archivo */
    if (logger->log_file) {
        fprintf(logger->log_file, "[%s] ", ts);
        va_start(args, fmt);
        vfprintf(logger->log_file, fmt, args);
        va_end(args);
        fprintf(logger->log_file, "\n");
        fflush(logger->log_file);
    }

    pthread_mutex_unlock(&logger->lock);
}

void logger_log_request(Logger *logger, const char *client_ip, int client_port,
                        const char *request, const char *response)
{
    if (!logger) return;

    char ts[32];
    get_iso8601(ts, sizeof(ts));

    /* Copias limpias sin \r\n para el log */
    char req_clean[1024];
    char rsp_clean[1024];
    snprintf(req_clean, sizeof(req_clean), "%s", request ? request : "(null)");
    snprintf(rsp_clean, sizeof(rsp_clean), "%s", response ? response : "(null)");

    /* Eliminar \r\n del final */
    size_t rlen = strlen(req_clean);
    while (rlen > 0 && (req_clean[rlen - 1] == '\r' || req_clean[rlen - 1] == '\n'))
        req_clean[--rlen] = '\0';
    rlen = strlen(rsp_clean);
    while (rlen > 0 && (rsp_clean[rlen - 1] == '\r' || rsp_clean[rlen - 1] == '\n'))
        rsp_clean[--rlen] = '\0';

    pthread_mutex_lock(&logger->lock);

    /* stdout */
    fprintf(stdout, "[%s] [%s:%d] REQ: %s RSP: %s\n",
            ts, client_ip, client_port, req_clean, rsp_clean);
    fflush(stdout);

    /* archivo */
    if (logger->log_file) {
        fprintf(logger->log_file, "[%s] [%s:%d] REQ: %s RSP: %s\n",
                ts, client_ip, client_port, req_clean, rsp_clean);
        fflush(logger->log_file);
    }

    pthread_mutex_unlock(&logger->lock);
}
