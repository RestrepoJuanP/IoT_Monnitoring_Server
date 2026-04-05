#include "network_thread.h"
#include <cstring>
#include <iostream>
#include <sstream>
#include <chrono>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int ssize_t;
    #define CLOSE_SOCKET closesocket
    #define SOCKET_ERROR_CODE WSAGetLastError()
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <errno.h>
    #include <fcntl.h>
    #define CLOSE_SOCKET close
    #define SOCKET_ERROR_CODE errno
    #define INVALID_SOCKET -1
#endif

/* Timeout de lectura en segundos (invariante I6 del Módulo 3) */
static constexpr int READ_TIMEOUT_SEC = 30;
/* Máximo de reintentos con backoff exponencial */
static constexpr int MAX_REINTENTOS = 5;

NetworkThread::NetworkThread()
    : m_port(0)
    , m_sockfd(-1)
    , m_running(false)
    , m_estado(EstadoConexion::DESCONECTADO)
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

NetworkThread::~NetworkThread()
{
    detener();
#ifdef _WIN32
    WSACleanup();
#endif
}

void NetworkThread::conectar(const std::string& host, int port,
                             const std::string& username, const std::string& password)
{
    /* Si ya está corriendo, detener primero */
    detener();

    m_host = host;
    m_port = port;
    m_username = username;
    m_password = password;
    m_running = true;

    m_thread = std::thread(&NetworkThread::hiloRed, this);
}

void NetworkThread::detener()
{
    m_running = false;

    if (m_sockfd >= 0) {
        CLOSE_SOCKET(m_sockfd);
        m_sockfd = -1;
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    m_estado = EstadoConexion::DESCONECTADO;
}

void NetworkThread::sendQuery(const std::string& target)
{
    enviarMensaje("QUERY|" + target + "\r\n");
}

void NetworkThread::sendStatus()
{
    enviarMensaje("STATUS\r\n");
}

void NetworkThread::disconnect()
{
    enviarMensaje("DISCONNECT|" + m_username + "\r\n");
    m_running = false;
}

EstadoConexion NetworkThread::getEstado() const
{
    return m_estado.load();
}

std::vector<MensajeIotp> NetworkThread::obtenerMensajes()
{
    std::lock_guard<std::mutex> lock(m_mutex_cola);
    std::vector<MensajeIotp> mensajes;
    while (!m_cola.empty()) {
        mensajes.push_back(m_cola.front());
        m_cola.pop();
    }
    return mensajes;
}

int NetworkThread::conectarSocket(const std::string& host, int port)
{
    /* Resolver hostname por DNS con getaddrinfo — CERO IPs literales */
    struct addrinfo hints{}, *res, *rp;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    int gai_err = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (gai_err != 0) {
        std::cerr << "[Red] getaddrinfo(" << host << ":" << port << "): "
                  << gai_strerror(gai_err) << std::endl;
        return -1;
    }

    int sockfd = -1;
    for (rp = res; rp != nullptr; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0) continue;

        /* Timeout de conexión */
#ifdef _WIN32
        DWORD timeout_ms = READ_TIMEOUT_SEC * 1000;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
#else
        struct timeval tv;
        tv.tv_sec = READ_TIMEOUT_SEC;
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

        if (connect(sockfd, rp->ai_addr, (int)rp->ai_addrlen) == 0) {
            break;
        }

        CLOSE_SOCKET(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(res);
    return sockfd;
}

bool NetworkThread::enviarMensaje(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(m_mutex_send);

    if (m_sockfd < 0) return false;

    size_t total = 0;
    while (total < msg.size()) {
        ssize_t n = send(m_sockfd, msg.c_str() + total, (int)(msg.size() - total), 0);
        if (n <= 0) {
            std::cerr << "[Red] Error al enviar mensaje" << std::endl;
            return false;
        }
        total += n;
    }

    return true;
}

std::string NetworkThread::recibirLinea()
{
    std::string linea;
    char c;

    while (m_running) {
        ssize_t n = recv(m_sockfd, &c, 1, 0);
        if (n <= 0) {
            if (n == 0) {
                throw std::runtime_error("Servidor cerro la conexion");
            }
            /* Timeout o error */
#ifdef _WIN32
            if (WSAGetLastError() == WSAETIMEDOUT) {
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
#endif
                throw std::runtime_error("Timeout de lectura");
            }
            throw std::runtime_error("Error de recv");
        }

        linea += c;

        /* Verificar terminador \r\n */
        if (linea.size() >= 2 &&
            linea[linea.size() - 2] == '\r' &&
            linea[linea.size() - 1] == '\n') {
            /* Retornar sin el \r\n */
            return linea.substr(0, linea.size() - 2);
        }
    }

    return "";
}

MensajeIotp NetworkThread::parsearMensaje(const std::string& raw)
{
    MensajeIotp msg;
    msg.raw = raw;

    std::istringstream ss(raw);
    std::string token;

    /* Primer campo: opcode */
    if (std::getline(ss, token, '|')) {
        msg.opcode = token;
    }

    /* Campos restantes */
    while (std::getline(ss, token, '|')) {
        msg.campos.push_back(token);
    }

    return msg;
}

void NetworkThread::encolarMensaje(const MensajeIotp& msg)
{
    std::lock_guard<std::mutex> lock(m_mutex_cola);
    m_cola.push(msg);
}

void NetworkThread::hiloRed()
{
    int intentos = 0;

    while (m_running && intentos < MAX_REINTENTOS) {
        m_estado = (intentos == 0) ? EstadoConexion::CONECTANDO : EstadoConexion::RECONECTANDO;

        /* 1. Conectar con backoff exponencial */
        if (intentos > 0) {
            int espera = 1 << (intentos - 1);  /* 1, 2, 4, 8, 16 */
            std::cerr << "[Red] Reintentando en " << espera << "s (intento "
                      << intentos + 1 << "/" << MAX_REINTENTOS << ")" << std::endl;
            for (int i = 0; i < espera && m_running; i++) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (!m_running) break;
        }

        m_sockfd = conectarSocket(m_host, m_port);
        if (m_sockfd < 0) {
            std::cerr << "[Red] No se pudo conectar a " << m_host << ":" << m_port << std::endl;
            intentos++;
            continue;
        }

        /* 2. Enviar REGISTER */
        std::string msg_register = "REGISTER|operator|" + m_username + "|" + m_password + "\r\n";
        if (!enviarMensaje(msg_register)) {
            CLOSE_SOCKET(m_sockfd);
            m_sockfd = -1;
            intentos++;
            continue;
        }

        /* 3. Esperar ACK de registro */
        try {
            std::string resp = recibirLinea();
            MensajeIotp parsed = parsearMensaje(resp);

            if (parsed.opcode == "ERROR") {
                std::cerr << "[Red] Registro rechazado: " << resp << std::endl;
                encolarMensaje(parsed);
                CLOSE_SOCKET(m_sockfd);
                m_sockfd = -1;
                m_estado = EstadoConexion::DESCONECTADO;
                m_running = false;
                return;
            }

            if (parsed.opcode == "ACK") {
                std::cerr << "[Red] Registrado exitosamente: " << resp << std::endl;
                m_estado = EstadoConexion::CONECTADO;
                intentos = 0;  /* Reiniciar contador de reintentos */
                encolarMensaje(parsed);
            }
        } catch (const std::exception& e) {
            std::cerr << "[Red] Error durante registro: " << e.what() << std::endl;
            CLOSE_SOCKET(m_sockfd);
            m_sockfd = -1;
            intentos++;
            continue;
        }

        /* 4. Loop de recepción */
        try {
            while (m_running) {
                std::string linea = recibirLinea();
                if (linea.empty() && !m_running) break;

                MensajeIotp msg = parsearMensaje(linea);
                encolarMensaje(msg);

                std::cerr << "[Red] Recibido: " << linea << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Red] Error en loop de recepcion: " << e.what() << std::endl;
        }

        /* Conexión perdida — intentar reconectar */
        if (m_sockfd >= 0) {
            CLOSE_SOCKET(m_sockfd);
            m_sockfd = -1;
        }

        if (m_running) {
            intentos++;
            std::cerr << "[Red] Conexion perdida, intentando reconectar..." << std::endl;
        }
    }

    if (intentos >= MAX_REINTENTOS) {
        std::cerr << "[Red] Agotados los reintentos de conexion" << std::endl;
        /* Encolar mensaje de error para que la GUI lo muestre */
        MensajeIotp err_msg;
        err_msg.opcode = "ERROR";
        err_msg.campos = {"CONNECTION_LOST", "Agotados los reintentos de conexion"};
        err_msg.raw = "ERROR|CONNECTION_LOST|Agotados los reintentos de conexion";
        encolarMensaje(err_msg);
    }

    m_estado = EstadoConexion::DESCONECTADO;
}
