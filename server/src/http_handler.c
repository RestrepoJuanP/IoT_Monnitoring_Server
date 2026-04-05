#include "http_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#define HTTP_BUF_SIZE 8192

/* ---------- Utilidades HTTP ---------- */

static const char *get_content_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0)  return "text/css; charset=utf-8";
    if (strcmp(ext, ".js") == 0)   return "application/javascript; charset=utf-8";
    if (strcmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(ext, ".png") == 0)  return "image/png";
    if (strcmp(ext, ".jpg") == 0)  return "image/jpeg";
    if (strcmp(ext, ".ico") == 0)  return "image/x-icon";
    if (strcmp(ext, ".svg") == 0)  return "image/svg+xml";
    return "application/octet-stream";
}

/*
 * Envía una respuesta HTTP completa con headers correctos.
 * Siempre incluye Content-Type, Content-Length y Connection: close.
 */
static void send_response(int fd, int status, const char *status_text,
                          const char *content_type, const char *body, size_t body_len)
{
    char header[1024];
    int hlen = snprintf(header, sizeof(header),
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n"
                        "\r\n",
                        status, status_text, content_type, body_len);

    /* Enviar header */
    ssize_t sent = send(fd, header, hlen, 0);
    if (sent <= 0) return;

    /* Enviar body */
    if (body && body_len > 0) {
        size_t total = 0;
        while (total < body_len) {
            sent = send(fd, body + total, body_len - total, 0);
            if (sent <= 0) return;
            total += sent;
        }
    }
}

/* Envía una respuesta de redirección 302 */
static void send_redirect(int fd, const char *location)
{
    char resp[1024];
    int len = snprintf(resp, sizeof(resp),
                       "HTTP/1.1 302 Found\r\n"
                       "Location: %s\r\n"
                       "Content-Type: text/html; charset=utf-8\r\n"
                       "Content-Length: 0\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       location);
    send(fd, resp, len, 0);
}

/* Envía respuesta 405 Method Not Allowed con header Allow */
static void send_405(int fd)
{
    const char *body =
        "<!DOCTYPE html><html><head><title>405</title></head>"
        "<body><h1>405 Method Not Allowed</h1>"
        "<p>Solo se acepta el metodo GET.</p></body></html>";
    size_t body_len = strlen(body);

    char header[512];
    int hlen = snprintf(header, sizeof(header),
                        "HTTP/1.1 405 Method Not Allowed\r\n"
                        "Allow: GET\r\n"
                        "Content-Type: text/html; charset=utf-8\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n"
                        "\r\n",
                        body_len);
    send(fd, header, hlen, 0);
    send(fd, body, body_len, 0);
}

/* Envía respuesta 404 Not Found */
static void send_404(int fd)
{
    const char *body =
        "<!DOCTYPE html><html><head><title>404</title></head>"
        "<body><h1>404 Not Found</h1>"
        "<p>El recurso solicitado no existe.</p></body></html>";
    send_response(fd, 404, "Not Found", "text/html; charset=utf-8",
                  body, strlen(body));
}

/* ---------- Decodificación URL ---------- */

/*
 * Decodifica percent-encoding simple (%XX) en la query string.
 * Modifica el buffer in-place.
 */
static void url_decode(char *str)
{
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], '\0' };
            *dst = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst = ' ';
            src++;
        } else {
            *dst = *src;
            src++;
        }
        dst++;
    }
    *dst = '\0';
}

/* ---------- Handlers de ruta ---------- */

/*
 * GET /login?user=X&pass=Y
 * Contacta al auth service, valida credenciales y redirige.
 * INVARIANTE: NUNCA loguear la contraseña.
 */
static void handle_login(int client_fd, char *path, HttpConfig *config)
{
    char user[64] = {0}, pass[64] = {0};
    char *query = strchr(path, '?');
    if (!query) {
        send_redirect(client_fd, "/");
        return;
    }
    query++;  /* saltar el '?' */

    /* Parsear query string: copiar para tokenizar */
    char query_buf[512];
    strncpy(query_buf, query, sizeof(query_buf) - 1);
    query_buf[sizeof(query_buf) - 1] = '\0';

    char *saveptr = NULL;
    char *tok = strtok_r(query_buf, "&", &saveptr);
    while (tok) {
        if (strncmp(tok, "user=", 5) == 0) {
            strncpy(user, tok + 5, sizeof(user) - 1);
            url_decode(user);
        } else if (strncmp(tok, "pass=", 5) == 0) {
            strncpy(pass, tok + 5, sizeof(pass) - 1);
            url_decode(pass);
        }
        tok = strtok_r(NULL, "&", &saveptr);
    }

    /* Log SIN contraseña */
    logger_log(config->logger, "HTTP: /login intento de autenticacion para usuario='%s'", user);

    if (strlen(user) == 0 || strlen(pass) == 0) {
        send_redirect(client_fd, "/?error=campos_vacios");
        return;
    }

    /* Contactar al auth service */
    char role[32] = {0};
    int result = auth_validate(config->auth_host, config->auth_port,
                               user, pass, role, sizeof(role));

    if (result == AUTH_ROLE_SENSOR || result == AUTH_ROLE_OPERATOR) {
        logger_log(config->logger, "HTTP: usuario '%s' autenticado como '%s'", user, role);
        send_redirect(client_fd, "/dashboard");
    } else if (result == AUTH_UNAVAILABLE) {
        logger_log(config->logger, "HTTP: auth service no disponible para usuario '%s'", user);
        const char *body =
            "<!DOCTYPE html><html><head><title>Error</title></head>"
            "<body><h1>503 - Servicio no disponible</h1>"
            "<p>El servicio de autenticacion no esta disponible. "
            "Intente de nuevo mas tarde.</p>"
            "<a href=\"/\">Volver al login</a></body></html>";
        send_response(client_fd, 503, "Service Unavailable",
                      "text/html; charset=utf-8", body, strlen(body));
    } else {
        logger_log(config->logger, "HTTP: autenticacion fallida para usuario '%s'", user);
        send_redirect(client_fd, "/?error=credenciales_invalidas");
    }
}

/*
 * GET /status
 * Retorna JSON con el estado actual del sistema.
 * Usa SensorManager para obtener datos en tiempo real.
 */
static void handle_status(int client_fd, HttpConfig *config)
{
    int sensors = 0, operators = 0;
    long uptime = 0;
    sm_get_status(config->sm, &sensors, &operators, &uptime);

    /* Obtener listas JSON de sensores, mediciones y alertas */
    char sensors_json[4096];
    char measurements_json[4096];
    char alerts_json[4096];
    sm_query_sensors(config->sm, sensors_json, sizeof(sensors_json));
    sm_query_measurements(config->sm, measurements_json, sizeof(measurements_json));
    sm_query_alerts(config->sm, alerts_json, sizeof(alerts_json));

    /* Construir JSON de respuesta */
    char body[16384];
    int body_len = snprintf(body, sizeof(body),
        "{"
        "\"sensores_activos\":%d,"
        "\"operadores_conectados\":%d,"
        "\"uptime_segundos\":%ld,"
        "\"sensores\":%s,"
        "\"mediciones_recientes\":%s,"
        "\"alertas_recientes\":%s"
        "}",
        sensors, operators, uptime,
        sensors_json, measurements_json, alerts_json);

    send_response(client_fd, 200, "OK", "application/json; charset=utf-8",
                  body, body_len);

    logger_log(config->logger, "HTTP: /status -> sensores=%d, operadores=%d, uptime=%ld",
               sensors, operators, uptime);
}

/*
 * Sirve un archivo estático desde web_root.
 * Protege contra path traversal (..).
 */
static void serve_file(int client_fd, const char *filepath, HttpConfig *config)
{
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        send_404(client_fd);
        logger_log(config->logger, "HTTP: 404 archivo no encontrado: %s", filepath);
        return;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 10 * 1024 * 1024) {  /* max 10 MB */
        fclose(f);
        send_404(client_fd);
        return;
    }

    char *content = malloc(fsize);
    if (!content) {
        fclose(f);
        const char *body = "500 Internal Server Error";
        send_response(client_fd, 500, "Internal Server Error",
                      "text/plain", body, strlen(body));
        return;
    }

    size_t read_bytes = fread(content, 1, fsize, f);
    fclose(f);

    send_response(client_fd, 200, "OK", get_content_type(filepath),
                  content, read_bytes);
    free(content);
}

/* ---------- Dispatch principal ---------- */

static void handle_http_client(int client_fd, HttpConfig *config)
{
    /* Timeout de lectura para el request */
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buf[HTTP_BUF_SIZE];
    memset(buf, 0, sizeof(buf));

    ssize_t received = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (received <= 0) {
        close(client_fd);
        return;
    }

    /* Parsear primera línea: METHOD PATH HTTP/1.x */
    char method[16] = {0};
    char path[1024] = {0};
    sscanf(buf, "%15s %1023s", method, path);

    /* INVARIANTE: Solo aceptar método GET (Módulo 4 I1) */
    if (strcmp(method, "GET") != 0) {
        logger_log(config->logger, "HTTP: 405 metodo no permitido: %s %s", method, path);
        send_405(client_fd);
        close(client_fd);
        return;
    }

    /* Extraer solo el path sin query string para el routing */
    char path_only[1024];
    strncpy(path_only, path, sizeof(path_only) - 1);
    path_only[sizeof(path_only) - 1] = '\0';
    char *qmark = strchr(path_only, '?');
    if (qmark) *qmark = '\0';

    /* --- Routing --- */

    /* GET /login?... → autenticación */
    if (strcmp(path_only, "/login") == 0) {
        handle_login(client_fd, path, config);
        close(client_fd);
        return;
    }

    /* GET /status → JSON con estado del sistema */
    if (strcmp(path_only, "/status") == 0) {
        handle_status(client_fd, config);
        close(client_fd);
        return;
    }

    /* GET / o /index.html → login.html */
    if (strcmp(path_only, "/") == 0 || strcmp(path_only, "/index.html") == 0) {
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/login.html", config->web_root);
        serve_file(client_fd, filepath, config);
        close(client_fd);
        return;
    }

    /* GET /dashboard → dashboard.html */
    if (strcmp(path_only, "/dashboard") == 0) {
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/dashboard.html", config->web_root);
        serve_file(client_fd, filepath, config);
        close(client_fd);
        return;
    }

    /* Archivos estáticos genéricos (CSS, JS, imágenes) */
    /* Protección contra path traversal */
    if (strstr(path_only, "..") != NULL) {
        const char *body = "403 Forbidden";
        send_response(client_fd, 403, "Forbidden", "text/plain", body, strlen(body));
        close(client_fd);
        logger_log(config->logger, "HTTP: 403 intento de path traversal: %s", path_only);
        return;
    }

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s%s", config->web_root, path_only);
    serve_file(client_fd, filepath, config);
    close(client_fd);
}

/* ---------- Hilo HTTP principal ---------- */

void *http_handler_thread(void *arg)
{
    HttpConfig *config = (HttpConfig *)arg;

    /* Crear socket escuchando — resolver con getaddrinfo, CERO IPs literales */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", config->port);

    int gai_err = getaddrinfo(NULL, port_str, &hints, &res);
    if (gai_err != 0) {
        logger_log(config->logger, "HTTP: getaddrinfo error: %s",
                   gai_strerror(gai_err));
        return NULL;
    }

    int server_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server_fd < 0) {
        logger_log(config->logger, "HTTP: socket error: %s", strerror(errno));
        freeaddrinfo(res);
        return NULL;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_fd, res->ai_addr, res->ai_addrlen) < 0) {
        logger_log(config->logger, "HTTP: bind error en puerto %d: %s",
                   config->port, strerror(errno));
        close(server_fd);
        freeaddrinfo(res);
        return NULL;
    }

    freeaddrinfo(res);

    if (listen(server_fd, 16) < 0) {
        logger_log(config->logger, "HTTP: listen error: %s", strerror(errno));
        close(server_fd);
        return NULL;
    }

    logger_log(config->logger, "HTTP: servidor escuchando en puerto %d", config->port);

    /* Loop de accept — maneja cada request secuencialmente (Connection: close) */
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            logger_log(config->logger, "HTTP: accept error: %s", strerror(errno));
            continue;  /* NUNCA terminar por error de red */
        }

        handle_http_client(client_fd, config);
    }

    close(server_fd);
    return NULL;
}
