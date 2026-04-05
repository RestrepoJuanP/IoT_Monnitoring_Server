#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int protocol_parse(const char *raw, IotpMessage *msg)
{
    if (!raw || !msg) return -1;

    memset(msg, 0, sizeof(IotpMessage));

    /* Copiar para tokenizar sin modificar el original */
    char buf[IOTP_MAX_MSG];
    strncpy(buf, raw, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Eliminar \r\n del final si quedó */
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n'))
        buf[--len] = '\0';

    if (len == 0) return -1;

    /* Primer token es el opcode */
    char *saveptr = NULL;
    char *token = strtok_r(buf, IOTP_DELIM, &saveptr);
    if (!token) return -1;

    strncpy(msg->opcode, token, sizeof(msg->opcode) - 1);
    msg->field_count = 0;

    /* Campos restantes */
    while ((token = strtok_r(NULL, IOTP_DELIM, &saveptr)) != NULL) {
        if (msg->field_count >= IOTP_MAX_FIELDS) return -1;
        strncpy(msg->fields[msg->field_count], token,
                sizeof(msg->fields[0]) - 1);
        msg->field_count++;
    }

    return 0;
}

int protocol_valid_opcode(const char *opcode)
{
    if (!opcode) return 0;

    static const char *valid_ops[] = {
        OP_REGISTER, OP_ACK, OP_ERROR, OP_DATA, OP_ALERT,
        OP_QUERY, OP_RESULT, OP_STATUS, OP_STATUSR, OP_DISCONNECT,
        NULL
    };

    for (int i = 0; valid_ops[i]; i++) {
        if (strcmp(opcode, valid_ops[i]) == 0) return 1;
    }
    return 0;
}

int protocol_valid_field_count(const IotpMessage *msg)
{
    if (!msg) return 0;

    if (strcmp(msg->opcode, OP_REGISTER) == 0)   return msg->field_count == 3;
    if (strcmp(msg->opcode, OP_DATA) == 0)        return msg->field_count == 4;
    if (strcmp(msg->opcode, OP_QUERY) == 0)       return msg->field_count == 1;
    if (strcmp(msg->opcode, OP_STATUS) == 0)      return msg->field_count == 0;
    if (strcmp(msg->opcode, OP_DISCONNECT) == 0)  return msg->field_count == 1;
    if (strcmp(msg->opcode, OP_ACK) == 0)         return msg->field_count == 1;
    if (strcmp(msg->opcode, OP_ERROR) == 0)       return msg->field_count == 2;
    if (strcmp(msg->opcode, OP_ALERT) == 0)       return msg->field_count == 5;
    if (strcmp(msg->opcode, OP_RESULT) == 0)      return msg->field_count == 2;
    if (strcmp(msg->opcode, OP_STATUSR) == 0)     return msg->field_count == 3;

    return 0;
}

int protocol_make_ack(char *buf, size_t buflen, const char *message)
{
    return snprintf(buf, buflen, "ACK|%s\r\n", message);
}

int protocol_make_error(char *buf, size_t buflen, const char *code,
                        const char *desc)
{
    return snprintf(buf, buflen, "ERROR|%s|%s\r\n", code, desc);
}

int protocol_make_alert(char *buf, size_t buflen, const char *sensor_id,
                        const char *type, double value, double threshold,
                        const char *timestamp)
{
    return snprintf(buf, buflen, "ALERT|%s|%s|%.2f|%.2f|%s\r\n",
                    sensor_id, type, value, threshold, timestamp);
}

int protocol_make_result(char *buf, size_t buflen, const char *query_type,
                         const char *data)
{
    return snprintf(buf, buflen, "RESULT|%s|%s\r\n", query_type, data);
}

int protocol_make_statusr(char *buf, size_t buflen, int sensors, int operators,
                          long uptime)
{
    return snprintf(buf, buflen, "STATUSR|%d|%d|%ld\r\n",
                    sensors, operators, uptime);
}
