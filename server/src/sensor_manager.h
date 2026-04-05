#include <stddef.h>
#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <pthread.h>
#include <time.h>

#define MAX_SENSORS       64
#define MAX_OPERATORS     16
#define MAX_MEASUREMENTS  1024
#define MAX_ALERTS        512

/* Umbrales de anomalía */
#define TEMP_MIN   15.0
#define TEMP_MAX   80.0
#define VIBRA_MAX  40.0
#define ENERGY_MAX 450.0

typedef struct {
    char sensor_id[64];
    char type[32];
    int  fd;          /* socket del sensor */
    int  active;
} SensorEntry;

typedef struct {
    char   sensor_id[64];
    char   type[32];
    double value;
    char   timestamp[32];
} Measurement;

typedef struct {
    char   sensor_id[64];
    char   type[32];
    double value;
    double threshold;
    char   timestamp[32];
} AlertEntry;

typedef struct {
    int fd;          /* socket del operador */
    int active;
    char username[64];
} OperatorEntry;

typedef struct {
    SensorEntry   sensors[MAX_SENSORS];
    int           sensor_count;

    OperatorEntry operators[MAX_OPERATORS];
    int           operator_count;

    Measurement   measurements[MAX_MEASUREMENTS];
    int           measurement_count;
    int           measurement_idx;    /* índice circular */

    AlertEntry    alerts[MAX_ALERTS];
    int           alert_count;
    int           alert_idx;          /* índice circular */

    time_t        start_time;
    pthread_mutex_t lock;
} SensorManager;

/* Inicializa la estructura compartida. */
int sm_init(SensorManager *sm);

/* Destruye el mutex. */
void sm_destroy(SensorManager *sm);

/* Registra un sensor activo. Retorna 0 si OK, -1 si lleno. */
int sm_add_sensor(SensorManager *sm, const char *sensor_id, const char *type, int fd);

/* Elimina un sensor por fd. */
void sm_remove_sensor(SensorManager *sm, int fd);

/* Registra un operador activo. Retorna 0 si OK, -1 si lleno. */
int sm_add_operator(SensorManager *sm, const char *username, int fd);

/* Elimina un operador por fd. */
void sm_remove_operator(SensorManager *sm, int fd);

/*
 * Registra una medición y detecta anomalías.
 * Si hay anomalía, llena alert_out y retorna 1.
 * Si no hay anomalía, retorna 0.
 * Retorna -1 en caso de error.
 */
int sm_add_measurement(SensorManager *sm, const char *sensor_id,
                       const char *type, double value, const char *timestamp,
                       AlertEntry *alert_out);

/* Genera JSON con la lista de sensores activos. Escribe en buf. */
int sm_query_sensors(SensorManager *sm, char *buf, size_t buflen);

/* Genera JSON con mediciones recientes. Escribe en buf. */
int sm_query_measurements(SensorManager *sm, char *buf, size_t buflen);

/* Genera JSON con alertas recientes. Escribe en buf. */
int sm_query_alerts(SensorManager *sm, char *buf, size_t buflen);

/* Retorna contadores para STATUSR. */
void sm_get_status(SensorManager *sm, int *sensors, int *operators, long *uptime);

/* Envía un ALERT a todos los operadores conectados. */
void sm_broadcast_alert(SensorManager *sm, const char *alert_msg);

#endif
