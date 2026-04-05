#include "server.h"
#include "protocol.h"
#include "auth_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>

/*
 * Lee una línea completa terminada en \r\n del socket.
 * Retorna el número de bytes leídos, 0 si desconexión, -1 si error/timeout.
 */
static ssize_t recv_line(int fd, char *buf, size_t buflen)
{
    size_t total = 0;

    while (total < buflen - 1) {
        ssize_t n = recv(fd, buf + total, 1, 0);
        if (n <= 0) return n;

        total++;

        /* Verificar si terminamos con \r\n */
        if (total >= 2 && buf[total - 2] == '\r' && buf[total - 1] == '\n') {
            buf[total] = '\0';
            return total;
        }
    }

    buf[total] = '\0';
    return total;
}

static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            return -1;
        }
        sent += n;
    }
    return 0;
}

static void handle_register(ClientSession *session, IotpMessage *msg, char *resp, size_t rlen)
{
    const char *role     = msg->fields[0];
    const char *username = msg->fields[1];
    const char *password = msg->fields[2];

    /* Validar rol */
    if (strcmp(role, ROLE_SENSOR) != 0 && strcmp(role, ROLE_OPERATOR) != 0) {
        protocol_make_error(resp, rlen, ERR_MALFORMED, "Rol invalido, debe ser sensor u operator");
        return;
    }

    /* Autenticar contra el auth service */
    char auth_role[32] = {0};
    int result = auth_validate(session->auth_host, session->auth_port,
                               username, password, auth_role, sizeof(auth_role));

    if (result == AUTH_UNAVAILABLE) {
        protocol_make_error(resp, rlen, ERR_AUTH_UNAVAILABLE,
                            "El servicio de autenticacion no esta disponible");
        return;
    }

    if (result == AUTH_FAIL || result == AUTH_ERROR) {
        protocol_make_error(resp, rlen, ERR_AUTH_FAILED,
                            "Credenciales invalidas");
        return;
    }

    /* Verificar que el rol solicitado coincida con el del auth */
    if (strcmp(role, auth_role) != 0) {
        protocol_make_error(resp, rlen, ERR_AUTH_FAILED,
                            "Rol solicitado no coincide con el registrado");
        return;
    }

    /* Sesión autenticada */
    session->authenticated = 1;
    strncpy(session->role, role, sizeof(session->role) - 1);
    strncpy(session->username, username, sizeof(session->username) - 1);

    if (strcmp(role, ROLE_SENSOR) == 0) {
        sm_add_sensor(session->sm, username, "", session->fd);
        protocol_make_ack(resp, rlen, "Registered as sensor");
    } else {
        sm_add_operator(session->sm, username, session->fd);
        protocol_make_ack(resp, rlen, "Registered as operator");
    }
}

static void handle_data(ClientSession *session, IotpMessage *msg, char *resp, size_t rlen)
{
    if (!session->authenticated) {
        protocol_make_error(resp, rlen, ERR_NOT_AUTHENTICATED,
                            "Debe registrarse antes de enviar datos");
        return;
    }

    if (strcmp(session->role, ROLE_SENSOR) != 0) {
        protocol_make_error(resp, rlen, ERR_FORBIDDEN,
                            "Solo sensores pueden enviar DATA");
        return;
    }

    const char *sensor_id  = msg->fields[0];
    const char *type       = msg->fields[1];
    const char *value_str  = msg->fields[2];
    const char *timestamp  = msg->fields[3];

    double value = atof(value_str);

    /* Actualizar tipo del sensor si no estaba registrado con tipo */
    sm_add_sensor(session->sm, sensor_id, type, session->fd);

    AlertEntry alert;
    memset(&alert, 0, sizeof(alert));
    int anomaly = sm_add_measurement(session->sm, sensor_id, type, value,
                                     timestamp, &alert);

    protocol_make_ack(resp, rlen, "Data received");

    /* Si hay anomalía, generar y difundir ALERT a operadores */
    if (anomaly == 1) {
        char alert_msg[IOTP_MAX_MSG];
        protocol_make_alert(alert_msg, sizeof(alert_msg),
                            alert.sensor_id, alert.type,
                            alert.value, alert.threshold, alert.timestamp);
        sm_broadcast_alert(session->sm, alert_msg);

        logger_log(session->logger, "ALERTA: sensor=%s tipo=%s valor=%.2f umbral=%.2f",
                   alert.sensor_id, alert.type, alert.value, alert.threshold);
    }
}

static void handle_query(ClientSession *session, IotpMessage *msg, char *resp, size_t rlen)
{
    if (!session->authenticated) {
        protocol_make_error(resp, rlen, ERR_NOT_AUTHENTICATED,
                            "Debe registrarse antes de enviar consultas");
        return;
    }

    if (strcmp(session->role, ROLE_OPERATOR) != 0) {
        protocol_make_error(resp, rlen, ERR_FORBIDDEN,
                            "Solo operadores pueden enviar QUERY");
        return;
    }

    const char *target = msg->fields[0];
    char data_buf[IOTP_MAX_MSG];

    if (strcmp(target, QUERY_SENSORS) == 0) {
        sm_query_sensors(session->sm, data_buf, sizeof(data_buf));
        protocol_make_result(resp, rlen, QUERY_SENSORS, data_buf);
    } else if (strcmp(target, QUERY_MEASUREMENTS) == 0) {
        sm_query_measurements(session->sm, data_buf, sizeof(data_buf));
        protocol_make_result(resp, rlen, QUERY_MEASUREMENTS, data_buf);
    } else if (strcmp(target, QUERY_ALERTS) == 0) {
        sm_query_alerts(session->sm, data_buf, sizeof(data_buf));
        protocol_make_result(resp, rlen, QUERY_ALERTS, data_buf);
    } else {
        protocol_make_error(resp, rlen, ERR_MALFORMED,
                            "Target de QUERY invalido");
    }
}

static void handle_status(ClientSession *session, char *resp, size_t rlen)
{
    if (!session->authenticated) {
        protocol_make_error(resp, rlen, ERR_NOT_AUTHENTICATED,
                            "Debe registrarse primero");
        return;
    }

    if (strcmp(session->role, ROLE_OPERATOR) != 0) {
        protocol_make_error(resp, rlen, ERR_FORBIDDEN,
                            "Solo operadores pueden enviar STATUS");
        return;
    }

    int sensors, operators;
    long uptime;
    sm_get_status(session->sm, &sensors, &operators, &uptime);
    protocol_make_statusr(resp, rlen, sensors, operators, uptime);
}

static void handle_disconnect(ClientSession *session, char *resp, size_t rlen)
{
    (void)resp;
    (void)rlen;

    logger_log(session->logger, "Cliente %s [%s:%d] desconectado (DISCONNECT)",
               session->username, session->client_ip, session->client_port);
}

void *client_handler_thread(void *arg)
{
    ClientSession *session = (ClientSession *)arg;

    /* Timeout de lectura: 30 segundos */
    struct timeval tv = { .tv_sec = READ_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(session->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    logger_log(session->logger, "Nueva conexion desde %s:%d (fd=%d)",
               session->client_ip, session->client_port, session->fd);

    char recv_buf[RECV_BUF_SIZE];
    int running = 1;

    while (running) {
        memset(recv_buf, 0, sizeof(recv_buf));
        ssize_t nbytes = recv_line(session->fd, recv_buf, sizeof(recv_buf));

        if (nbytes <= 0) {
            if (nbytes == 0) {
                logger_log(session->logger, "Cliente %s:%d cerro conexion",
                           session->client_ip, session->client_port);
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    logger_log(session->logger, "Timeout de lectura para %s:%d",
                               session->client_ip, session->client_port);
                } else {
                    logger_log(session->logger, "Error recv de %s:%d: %s",
                               session->client_ip, session->client_port,
                               strerror(errno));
                }
            }
            break;
        }

        /* Parsear mensaje */
        IotpMessage msg;
        char resp[IOTP_MAX_MSG];
        memset(resp, 0, sizeof(resp));

        if (protocol_parse(recv_buf, &msg) != 0) {
            protocol_make_error(resp, sizeof(resp), ERR_MALFORMED,
                                "Mensaje no se pudo parsear");
            send_all(session->fd, resp, strlen(resp));
            logger_log_request(session->logger, session->client_ip,
                               session->client_port, recv_buf, resp);
            continue;
        }

        /* Validar opcode conocido */
        if (!protocol_valid_opcode(msg.opcode)) {
            protocol_make_error(resp, sizeof(resp), ERR_UNKNOWN_OP, msg.opcode);
            send_all(session->fd, resp, strlen(resp));
            logger_log_request(session->logger, session->client_ip,
                               session->client_port, recv_buf, resp);
            continue;
        }

        /* Validar número de campos */
        if (!protocol_valid_field_count(&msg)) {
            protocol_make_error(resp, sizeof(resp), ERR_MALFORMED,
                                "Numero de campos incorrecto para el opcode");
            send_all(session->fd, resp, strlen(resp));
            logger_log_request(session->logger, session->client_ip,
                               session->client_port, recv_buf, resp);
            continue;
        }

        /* Despachar según opcode */
        if (strcmp(msg.opcode, OP_REGISTER) == 0) {
            handle_register(session, &msg, resp, sizeof(resp));
        } else if (strcmp(msg.opcode, OP_DATA) == 0) {
            handle_data(session, &msg, resp, sizeof(resp));
        } else if (strcmp(msg.opcode, OP_QUERY) == 0) {
            handle_query(session, &msg, resp, sizeof(resp));
        } else if (strcmp(msg.opcode, OP_STATUS) == 0) {
            handle_status(session, resp, sizeof(resp));
        } else if (strcmp(msg.opcode, OP_DISCONNECT) == 0) {
            handle_disconnect(session, resp, sizeof(resp));
            running = 0;
            /* DISCONNECT no envía respuesta — log y salir */
            logger_log_request(session->logger, session->client_ip,
                               session->client_port, recv_buf, "(disconnect)");
            break;
        } else {
            /* No debería llegar aquí, pero por seguridad */
            protocol_make_error(resp, sizeof(resp), ERR_UNKNOWN_OP, msg.opcode);
        }

        /* Enviar respuesta (si hay) */
        if (strlen(resp) > 0) {
            if (send_all(session->fd, resp, strlen(resp)) < 0) {
                logger_log(session->logger, "Error enviando respuesta a %s:%d",
                           session->client_ip, session->client_port);
                break;
            }
        }

        logger_log_request(session->logger, session->client_ip,
                           session->client_port, recv_buf, resp);
    }

    /* Limpiar recursos */
    if (strcmp(session->role, ROLE_SENSOR) == 0) {
        sm_remove_sensor(session->sm, session->fd);
    } else if (strcmp(session->role, ROLE_OPERATOR) == 0) {
        sm_remove_operator(session->sm, session->fd);
    }

    close(session->fd);
    logger_log(session->logger, "Sesion cerrada para %s:%d (fd=%d)",
               session->client_ip, session->client_port, session->fd);
    free(session);
    return NULL;
}
