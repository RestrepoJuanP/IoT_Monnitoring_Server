#include <stddef.h>
#ifndef AUTH_CLIENT_H
#define AUTH_CLIENT_H

#define AUTH_ROLE_SENSOR    1
#define AUTH_ROLE_OPERATOR  2
#define AUTH_FAIL           0
#define AUTH_UNAVAILABLE   -1
#define AUTH_ERROR         -2

/*
 * Conecta al servicio de autenticación (por DNS, sin IPs literales),
 * envía AUTH|username|password\r\n, y espera la respuesta.
 *
 * Parámetros:
 *   auth_host  — hostname del auth service (ej: "auth-service")
 *   auth_port  — puerto como string (ej: "9001")
 *   username   — nombre de usuario a autenticar
 *   password   — contraseña del usuario
 *   role_out   — buffer donde se escribe el rol retornado ("sensor" o "operator")
 *   role_len   — tamaño del buffer role_out
 *
 * Retorna:
 *   AUTH_ROLE_SENSOR   (1) si OK y rol = sensor
 *   AUTH_ROLE_OPERATOR (2) si OK y rol = operator
 *   AUTH_FAIL          (0) si las credenciales son inválidas
 *   AUTH_UNAVAILABLE  (-1) si no se pudo conectar al auth service
 *   AUTH_ERROR        (-2) si hubo un error de protocolo
 */
int auth_validate(const char *auth_host, const char *auth_port,
                  const char *username, const char *password,
                  char *role_out, size_t role_len);

#endif
