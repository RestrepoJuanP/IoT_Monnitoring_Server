#include <stddef.h>
#ifndef PROTOCOL_H
#define PROTOCOL_H

#define IOTP_MAX_MSG     4096
#define IOTP_MAX_FIELDS  16
#define IOTP_DELIM       "|"
#define IOTP_TERMINATOR  "\r\n"

/* Opcodes */
#define OP_REGISTER    "REGISTER"
#define OP_ACK         "ACK"
#define OP_ERROR       "ERROR"
#define OP_DATA        "DATA"
#define OP_ALERT       "ALERT"
#define OP_QUERY       "QUERY"
#define OP_RESULT      "RESULT"
#define OP_STATUS      "STATUS"
#define OP_STATUSR     "STATUSR"
#define OP_DISCONNECT  "DISCONNECT"

/* Códigos de error */
#define ERR_NOT_AUTHENTICATED  "NOT_AUTHENTICATED"
#define ERR_UNKNOWN_OP         "UNKNOWN_OP"
#define ERR_AUTH_FAILED        "AUTH_FAILED"
#define ERR_AUTH_UNAVAILABLE   "AUTH_UNAVAILABLE"
#define ERR_MALFORMED          "MALFORMED"
#define ERR_FORBIDDEN          "FORBIDDEN"

/* Roles */
#define ROLE_SENSOR    "sensor"
#define ROLE_OPERATOR  "operator"

/* Targets de QUERY */
#define QUERY_SENSORS       "SENSORS"
#define QUERY_MEASUREMENTS  "MEASUREMENTS"
#define QUERY_ALERTS        "ALERTS"

/* Mensaje parseado */
typedef struct {
    char opcode[32];
    char fields[IOTP_MAX_FIELDS][512];
    int field_count;
} IotpMessage;

/*
 * Parsea un mensaje IOTP crudo (sin el \r\n final) en la estructura IotpMessage.
 * Retorna 0 si OK, -1 si el mensaje es malformado.
 */
int protocol_parse(const char *raw, IotpMessage *msg);

/*
 * Valida que el opcode sea conocido.
 * Retorna 1 si es válido, 0 si no.
 */
int protocol_valid_opcode(const char *opcode);

/*
 * Valida que el número de campos sea correcto para el opcode dado.
 * Retorna 1 si es válido, 0 si no.
 */
int protocol_valid_field_count(const IotpMessage *msg);

/* Generadores de respuestas — escriben en buf y retornan bytes escritos. */
int protocol_make_ack(char *buf, size_t buflen, const char *message);
int protocol_make_error(char *buf, size_t buflen, const char *code, const char *desc);
int protocol_make_alert(char *buf, size_t buflen, const char *sensor_id,
                        const char *type, double value, double threshold,
                        const char *timestamp);
int protocol_make_result(char *buf, size_t buflen, const char *query_type,
                         const char *data);
int protocol_make_statusr(char *buf, size_t buflen, int sensors, int operators,
                          long uptime);

#endif
