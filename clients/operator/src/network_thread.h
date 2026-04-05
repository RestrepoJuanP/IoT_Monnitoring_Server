#ifndef NETWORK_THREAD_H
#define NETWORK_THREAD_H

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>

/* Estados de conexión para el indicador visual */
enum class EstadoConexion {
    DESCONECTADO,   // Rojo
    CONECTANDO,     // Amarillo
    CONECTADO,      // Verde
    RECONECTANDO    // Amarillo
};

/* Mensaje IOTP parseado recibido del servidor */
struct MensajeIotp {
    std::string opcode;
    std::vector<std::string> campos;
    std::string raw;  /* mensaje original completo */
};

/* Datos de un sensor para mostrar en la tabla */
struct InfoSensor {
    std::string id;
    std::string tipo;
    std::string ultimo_valor;
    std::string timestamp;
};

/* Alerta recibida del servidor */
struct Alerta {
    std::string sensor_id;
    std::string tipo;
    std::string valor;
    std::string umbral;
    std::string timestamp;
};

/*
 * Hilo de red que maneja la conexión TCP al servidor IOTP.
 * Toda operación de I/O ocurre en un hilo separado para no bloquear la GUI.
 */
class NetworkThread {
public:
    NetworkThread();
    ~NetworkThread();

    /* Conecta al servidor y arranca el hilo de recepción */
    void conectar(const std::string& host, int port,
                  const std::string& username, const std::string& password);

    /* Detiene el hilo y cierra la conexión */
    void detener();

    /* Envía QUERY|target al servidor (SENSORS, MEASUREMENTS, ALERTS) */
    void sendQuery(const std::string& target);

    /* Envía STATUS al servidor */
    void sendStatus();

    /* Envía DISCONNECT y cierra la conexión */
    void disconnect();

    /* Consulta el estado de conexión (thread-safe) */
    EstadoConexion getEstado() const;

    /* Extrae todos los mensajes pendientes de la cola (thread-safe) */
    std::vector<MensajeIotp> obtenerMensajes();

private:
    /* Hilo principal de red: conecta, registra y recibe mensajes */
    void hiloRed();

    /* Conecta al servidor con getaddrinfo() — CERO IPs literales */
    int conectarSocket(const std::string& host, int port);

    /* Envía un mensaje IOTP completo (con \r\n) */
    bool enviarMensaje(const std::string& msg);

    /* Recibe una línea completa terminada en \r\n */
    std::string recibirLinea();

    /* Parsea un mensaje IOTP crudo */
    MensajeIotp parsearMensaje(const std::string& raw);

    /* Encola un mensaje recibido (thread-safe) */
    void encolarMensaje(const MensajeIotp& msg);

    /* Datos de conexión */
    std::string m_host;
    int         m_port;
    std::string m_username;
    std::string m_password;
    int         m_sockfd;

    /* Control del hilo */
    std::thread       m_thread;
    std::atomic<bool> m_running;
    std::atomic<EstadoConexion> m_estado;

    /* Cola de mensajes thread-safe */
    std::queue<MensajeIotp>  m_cola;
    mutable std::mutex       m_mutex_cola;

    /* Mutex para envío (evitar escrituras concurrentes en el socket) */
    std::mutex m_mutex_send;
};

#endif
