#include "auth_client.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#define AUTH_TIMEOUT_SEC 5

int auth_validate(const char *auth_host, const char *auth_port,
                  const char *username, const char *password,
                  char *role_out, size_t role_len)
{
    if (!auth_host || !auth_port || !username || !password)
        return AUTH_ERROR;

    /* Resolver hostname por DNS — CERO IPs literales */
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int gai_err = getaddrinfo(auth_host, auth_port, &hints, &res);
    if (gai_err != 0) {
        fprintf(stderr, "auth_validate: getaddrinfo(%s:%s): %s\n",
                auth_host, auth_port, gai_strerror(gai_err));
        return AUTH_UNAVAILABLE;
    }

    int sockfd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0) continue;

        /* Timeout para connect */
        struct timeval tv = { .tv_sec = AUTH_TIMEOUT_SEC, .tv_usec = 0 };
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(res);

    if (sockfd < 0) {
        return AUTH_UNAVAILABLE;
    }

    /* Enviar AUTH|username|password\r\n */
    char req[512];
    int reqlen = snprintf(req, sizeof(req), "AUTH|%s|%s\r\n", username, password);

    ssize_t sent = send(sockfd, req, reqlen, 0);
    if (sent <= 0) {
        close(sockfd);
        return AUTH_UNAVAILABLE;
    }

    /* Recibir respuesta: OK|rol\r\n o FAIL|reason\r\n */
    char resp[256];
    memset(resp, 0, sizeof(resp));
    ssize_t received = recv(sockfd, resp, sizeof(resp) - 1, 0);
    close(sockfd);

    if (received <= 0) {
        return AUTH_UNAVAILABLE;
    }

    /* Eliminar \r\n */
    size_t rlen = strlen(resp);
    while (rlen > 0 && (resp[rlen - 1] == '\r' || resp[rlen - 1] == '\n'))
        resp[--rlen] = '\0';

    /* Parsear respuesta */
    if (strncmp(resp, "OK|", 3) == 0) {
        const char *role = resp + 3;
        if (role_out && role_len > 0)
            strncpy(role_out, role, role_len - 1);
        if (strcmp(role, "sensor") == 0)   return AUTH_ROLE_SENSOR;
        if (strcmp(role, "operator") == 0) return AUTH_ROLE_OPERATOR;
        return AUTH_ERROR;
    }

    if (strncmp(resp, "FAIL|", 5) == 0) {
        return AUTH_FAIL;
    }

    return AUTH_ERROR;
}
