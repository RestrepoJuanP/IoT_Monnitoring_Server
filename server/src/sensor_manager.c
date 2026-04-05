#include "sensor_manager.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

int sm_init(SensorManager *sm)
{
    if (!sm) return -1;
    memset(sm, 0, sizeof(SensorManager));
    sm->start_time = time(NULL);
    if (pthread_mutex_init(&sm->lock, NULL) != 0) return -1;
    return 0;
}

void sm_destroy(SensorManager *sm)
{
    if (!sm) return;
    pthread_mutex_destroy(&sm->lock);
}

int sm_add_sensor(SensorManager *sm, const char *sensor_id, const char *type, int fd)
{
    pthread_mutex_lock(&sm->lock);

    if (sm->sensor_count >= MAX_SENSORS) {
        pthread_mutex_unlock(&sm->lock);
        return -1;
    }

    /* Verificar si ya existe (reconexión) */
    for (int i = 0; i < sm->sensor_count; i++) {
        if (sm->sensors[i].active &&
            strcmp(sm->sensors[i].sensor_id, sensor_id) == 0) {
            sm->sensors[i].fd = fd;
            strncpy(sm->sensors[i].type, type, sizeof(sm->sensors[i].type) - 1);
            pthread_mutex_unlock(&sm->lock);
            return 0;
        }
    }

    SensorEntry *s = &sm->sensors[sm->sensor_count];
    strncpy(s->sensor_id, sensor_id, sizeof(s->sensor_id) - 1);
    strncpy(s->type, type, sizeof(s->type) - 1);
    s->fd = fd;
    s->active = 1;
    sm->sensor_count++;

    pthread_mutex_unlock(&sm->lock);
    return 0;
}

void sm_remove_sensor(SensorManager *sm, int fd)
{
    pthread_mutex_lock(&sm->lock);
    for (int i = 0; i < sm->sensor_count; i++) {
        if (sm->sensors[i].fd == fd) {
            sm->sensors[i].active = 0;
            sm->sensors[i].fd = -1;
            break;
        }
    }
    pthread_mutex_unlock(&sm->lock);
}

int sm_add_operator(SensorManager *sm, const char *username, int fd)
{
    pthread_mutex_lock(&sm->lock);

    if (sm->operator_count >= MAX_OPERATORS) {
        pthread_mutex_unlock(&sm->lock);
        return -1;
    }

    OperatorEntry *op = &sm->operators[sm->operator_count];
    strncpy(op->username, username, sizeof(op->username) - 1);
    op->fd = fd;
    op->active = 1;
    sm->operator_count++;

    pthread_mutex_unlock(&sm->lock);
    return 0;
}

void sm_remove_operator(SensorManager *sm, int fd)
{
    pthread_mutex_lock(&sm->lock);
    for (int i = 0; i < sm->operator_count; i++) {
        if (sm->operators[i].fd == fd) {
            sm->operators[i].active = 0;
            sm->operators[i].fd = -1;
            break;
        }
    }
    pthread_mutex_unlock(&sm->lock);
}

/*
 * Verifica si un valor está fuera de los umbrales para el tipo de sensor dado.
 * Si hay anomalía, escribe el umbral cruzado en *threshold y retorna 1.
 */
static int check_anomaly(const char *type, double value, double *threshold)
{
    if (strcmp(type, "temperatura") == 0) {
        if (value > TEMP_MAX) { *threshold = TEMP_MAX; return 1; }
        if (value < TEMP_MIN) { *threshold = TEMP_MIN; return 1; }
    } else if (strcmp(type, "vibracion") == 0) {
        if (value > VIBRA_MAX) { *threshold = VIBRA_MAX; return 1; }
    } else if (strcmp(type, "energia") == 0) {
        if (value > ENERGY_MAX) { *threshold = ENERGY_MAX; return 1; }
    }
    return 0;
}

int sm_add_measurement(SensorManager *sm, const char *sensor_id,
                       const char *type, double value, const char *timestamp,
                       AlertEntry *alert_out)
{
    if (!sm || !sensor_id || !type || !timestamp) return -1;

    pthread_mutex_lock(&sm->lock);

    /* Almacenar medición en buffer circular */
    int idx = sm->measurement_idx % MAX_MEASUREMENTS;
    Measurement *m = &sm->measurements[idx];
    strncpy(m->sensor_id, sensor_id, sizeof(m->sensor_id) - 1);
    strncpy(m->type, type, sizeof(m->type) - 1);
    m->value = value;
    strncpy(m->timestamp, timestamp, sizeof(m->timestamp) - 1);
    sm->measurement_idx++;
    if (sm->measurement_count < MAX_MEASUREMENTS)
        sm->measurement_count++;

    /* Detectar anomalía */
    double threshold = 0.0;
    int is_anomaly = check_anomaly(type, value, &threshold);

    if (is_anomaly && alert_out) {
        strncpy(alert_out->sensor_id, sensor_id, sizeof(alert_out->sensor_id) - 1);
        strncpy(alert_out->type, type, sizeof(alert_out->type) - 1);
        alert_out->value = value;
        alert_out->threshold = threshold;
        strncpy(alert_out->timestamp, timestamp, sizeof(alert_out->timestamp) - 1);

        /* Almacenar alerta en buffer circular */
        int aidx = sm->alert_idx % MAX_ALERTS;
        sm->alerts[aidx] = *alert_out;
        sm->alert_idx++;
        if (sm->alert_count < MAX_ALERTS)
            sm->alert_count++;
    }

    pthread_mutex_unlock(&sm->lock);
    return is_anomaly ? 1 : 0;
}

int sm_query_sensors(SensorManager *sm, char *buf, size_t buflen)
{
    pthread_mutex_lock(&sm->lock);

    int pos = 0;
    pos += snprintf(buf + pos, buflen - pos, "[");

    int first = 1;
    for (int i = 0; i < sm->sensor_count; i++) {
        if (!sm->sensors[i].active) continue;
        if (!first) pos += snprintf(buf + pos, buflen - pos, ",");
        pos += snprintf(buf + pos, buflen - pos,
                        "{\"id\":\"%s\",\"tipo\":\"%s\",\"activo\":true}",
                        sm->sensors[i].sensor_id, sm->sensors[i].type);
        first = 0;
    }

    pos += snprintf(buf + pos, buflen - pos, "]");

    pthread_mutex_unlock(&sm->lock);
    return pos;
}

int sm_query_measurements(SensorManager *sm, char *buf, size_t buflen)
{
    pthread_mutex_lock(&sm->lock);

    int pos = 0;
    pos += snprintf(buf + pos, buflen - pos, "[");

    int count = sm->measurement_count;
    /* Mostrar las últimas mediciones (hasta 50) */
    int show = count < 50 ? count : 50;
    int start = (sm->measurement_idx - show + MAX_MEASUREMENTS) % MAX_MEASUREMENTS;

    int first = 1;
    for (int i = 0; i < show; i++) {
        int idx = (start + i) % MAX_MEASUREMENTS;
        Measurement *m = &sm->measurements[idx];
        if (!first) pos += snprintf(buf + pos, buflen - pos, ",");
        pos += snprintf(buf + pos, buflen - pos,
                        "{\"sensor\":\"%s\",\"tipo\":\"%s\","
                        "\"valor\":%.2f,\"timestamp\":\"%s\"}",
                        m->sensor_id, m->type, m->value, m->timestamp);
        first = 0;
    }

    pos += snprintf(buf + pos, buflen - pos, "]");

    pthread_mutex_unlock(&sm->lock);
    return pos;
}

int sm_query_alerts(SensorManager *sm, char *buf, size_t buflen)
{
    pthread_mutex_lock(&sm->lock);

    int pos = 0;
    pos += snprintf(buf + pos, buflen - pos, "[");

    int count = sm->alert_count;
    int show = count < 50 ? count : 50;
    int start = (sm->alert_idx - show + MAX_ALERTS) % MAX_ALERTS;

    int first = 1;
    for (int i = 0; i < show; i++) {
        int idx = (start + i) % MAX_ALERTS;
        AlertEntry *a = &sm->alerts[idx];
        if (!first) pos += snprintf(buf + pos, buflen - pos, ",");
        pos += snprintf(buf + pos, buflen - pos,
                        "{\"sensor\":\"%s\",\"tipo\":\"%s\","
                        "\"valor\":%.2f,\"umbral\":%.2f,\"timestamp\":\"%s\"}",
                        a->sensor_id, a->type, a->value,
                        a->threshold, a->timestamp);
        first = 0;
    }

    pos += snprintf(buf + pos, buflen - pos, "]");

    pthread_mutex_unlock(&sm->lock);
    return pos;
}

void sm_get_status(SensorManager *sm, int *sensors, int *operators, long *uptime)
{
    pthread_mutex_lock(&sm->lock);

    int sc = 0, oc = 0;
    for (int i = 0; i < sm->sensor_count; i++)
        if (sm->sensors[i].active) sc++;
    for (int i = 0; i < sm->operator_count; i++)
        if (sm->operators[i].active) oc++;

    *sensors = sc;
    *operators = oc;
    *uptime = (long)(time(NULL) - sm->start_time);

    pthread_mutex_unlock(&sm->lock);
}

void sm_broadcast_alert(SensorManager *sm, const char *alert_msg)
{
    size_t len = strlen(alert_msg);

    pthread_mutex_lock(&sm->lock);
    for (int i = 0; i < sm->operator_count; i++) {
        if (!sm->operators[i].active) continue;
        /* send() puede fallar si el operador se desconectó — ignorar error */
        ssize_t sent = send(sm->operators[i].fd, alert_msg, len, MSG_NOSIGNAL);
        (void)sent;
    }
    pthread_mutex_unlock(&sm->lock);
}
