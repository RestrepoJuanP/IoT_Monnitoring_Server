#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "server.h"
#include "logger.h"
#include "sensor_manager.h"
#include "http_handler.h"

#define DEFAULT_HTTP_PORT  8080
#define DEFAULT_AUTH_HOST  "auth-service"
#define DEFAULT_AUTH_PORT  "9001"
#define WEB_ROOT           "./web"

static Logger        g_logger;
static SensorManager g_sm;

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Uso: %s <puerto_iotp> <archivo_log> "
            "[--http-port <puerto>] [--auth-host <host>] [--auth-port <puerto>]\n",
            prog);
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *iotp_port = argv[1];
    const char *log_file  = argv[2];

    int  http_port = DEFAULT_HTTP_PORT;
    char auth_host[128];
    char auth_port[8];
    strncpy(auth_host, DEFAULT_AUTH_HOST, sizeof(auth_host) - 1);
    strncpy(auth_port, DEFAULT_AUTH_PORT, sizeof(auth_port) - 1);

    /* Parseo de argumentos opcionales */
    for (int i = 3; i < argc - 1; i++) {
        if (strcmp(argv[i], "--http-port") == 0) {
            http_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--auth-host") == 0) {
            strncpy(auth_host, argv[++i], sizeof(auth_host) - 1);
        } else if (strcmp(argv[i], "--auth-port") == 0) {
            strncpy(auth_port, argv[++i], sizeof(auth_port) - 1);
        }
    }

    /* Ignorar SIGPIPE para que send() no mate el proceso */
    signal(SIGPIPE, SIG_IGN);

    /* Inicializar logger */
    if (logger_init(&g_logger, log_file) != 0) {
        fprintf(stderr, "Error: no se pudo abrir el archivo de log '%s'\n", log_file);
        return EXIT_FAILURE;
    }

    /* Inicializar sensor manager */
    if (sm_init(&g_sm) != 0) {
        fprintf(stderr, "Error: no se pudo inicializar el sensor manager\n");
        logger_destroy(&g_logger);
        return EXIT_FAILURE;
    }

    logger_log(&g_logger, "Servidor IOTP iniciando — puerto IOTP=%s, HTTP=%d, auth=%s:%s",
               iotp_port, http_port, auth_host, auth_port);

    /* Lanzar hilo del servidor HTTP */
    HttpConfig http_config;
    memset(&http_config, 0, sizeof(http_config));
    http_config.port = http_port;
    strncpy(http_config.web_root, WEB_ROOT, sizeof(http_config.web_root) - 1);
    strncpy(http_config.auth_host, auth_host, sizeof(http_config.auth_host) - 1);
    strncpy(http_config.auth_port, auth_port, sizeof(http_config.auth_port) - 1);
    http_config.logger = &g_logger;
    http_config.sm = &g_sm;  /* acceso al estado del sistema para /status */

    pthread_t http_thread;
    if (pthread_create(&http_thread, NULL, http_handler_thread, &http_config) != 0) {
        logger_log(&g_logger, "Error: no se pudo crear hilo HTTP");
        sm_destroy(&g_sm);
        logger_destroy(&g_logger);
        return EXIT_FAILURE;
    }
    pthread_detach(http_thread);

    /* Crear socket IOTP — resolver con getaddrinfo, CERO IPs literales */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    int gai_err = getaddrinfo(NULL, iotp_port, &hints, &res);
    if (gai_err != 0) {
        logger_log(&g_logger, "getaddrinfo error: %s", gai_strerror(gai_err));
        sm_destroy(&g_sm);
        logger_destroy(&g_logger);
        return EXIT_FAILURE;
    }

    int server_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server_fd < 0) {
        logger_log(&g_logger, "socket error: %s", strerror(errno));
        freeaddrinfo(res);
        sm_destroy(&g_sm);
        logger_destroy(&g_logger);
        return EXIT_FAILURE;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_fd, res->ai_addr, res->ai_addrlen) < 0) {
        logger_log(&g_logger, "bind error: %s", strerror(errno));
        close(server_fd);
        freeaddrinfo(res);
        sm_destroy(&g_sm);
        logger_destroy(&g_logger);
        return EXIT_FAILURE;
    }

    freeaddrinfo(res);

    if (listen(server_fd, 20) < 0) {
        logger_log(&g_logger, "listen error: %s", strerror(errno));
        close(server_fd);
        sm_destroy(&g_sm);
        logger_destroy(&g_logger);
        return EXIT_FAILURE;
    }

    logger_log(&g_logger, "Servidor IOTP escuchando en puerto %s", iotp_port);

    /* Loop accept — un hilo por conexión */
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            logger_log(&g_logger, "accept error: %s", strerror(errno));
            continue;  /* NUNCA terminar por error de red */
        }

        /* Extraer IP y puerto del cliente */
        char client_ip[64] = {0};
        int  client_port = 0;

        if (client_addr.ss_family == AF_INET) {
            struct sockaddr_in *addr4 = (struct sockaddr_in *)&client_addr;
            inet_ntop(AF_INET, &addr4->sin_addr, client_ip, sizeof(client_ip));
            client_port = ntohs(addr4->sin_port);
        } else if (client_addr.ss_family == AF_INET6) {
            struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&client_addr;
            inet_ntop(AF_INET6, &addr6->sin6_addr, client_ip, sizeof(client_ip));
            client_port = ntohs(addr6->sin6_port);
        }

        /* Crear sesión para el hilo */
        ClientSession *session = calloc(1, sizeof(ClientSession));
        if (!session) {
            logger_log(&g_logger, "Error: calloc para ClientSession fallo");
            close(client_fd);
            continue;
        }

        session->fd = client_fd;
        strncpy(session->client_ip, client_ip, sizeof(session->client_ip) - 1);
        session->client_port = client_port;
        session->authenticated = 0;
        strncpy(session->auth_host, auth_host, sizeof(session->auth_host) - 1);
        strncpy(session->auth_port, auth_port, sizeof(session->auth_port) - 1);
        session->logger = &g_logger;
        session->sm = &g_sm;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler_thread, session) != 0) {
            logger_log(&g_logger, "Error: pthread_create fallo para %s:%d",
                       client_ip, client_port);
            close(client_fd);
            free(session);
            continue;
        }

        pthread_detach(tid);
    }

    /* Limpieza (en la práctica no se llega aquí por el while(1)) */
    close(server_fd);
    sm_destroy(&g_sm);
    logger_destroy(&g_logger);

    return EXIT_SUCCESS;
}
