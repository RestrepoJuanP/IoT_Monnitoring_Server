#ifndef GUI_H
#define GUI_H

#include "network_thread.h"
#include <string>
#include <vector>
#include <map>

/*
 * Clase que maneja la interfaz gráfica del operador usando Dear ImGui.
 * Toda la lógica de red está delegada a NetworkThread.
 * La GUI nunca se bloquea por operaciones de I/O.
 */
class OperadorGui {
public:
    OperadorGui();
    ~OperadorGui();

    /* Inicia la ventana y el loop principal de renderizado */
    int ejecutar();

private:
    /* Renderiza el frame completo de Dear ImGui */
    void renderFrame();

    /* Panel de conexión (formulario de host/port/user/pass) */
    void renderPanelConexion();

    /* Indicador visual del estado de conexión */
    void renderIndicadorEstado();

    /* Tabla de sensores activos */
    void renderTablaSensores();

    /* Panel de alertas con scroll */
    void renderPanelAlertas();

    /* Panel de resultados (respuestas a QUERY y STATUS) */
    void renderPanelResultados();

    /* Barra de botones de acción */
    void renderBotonesAccion();

    /* Procesa los mensajes recibidos del hilo de red */
    void procesarMensajes();

    /* Parsea JSON simple de sensores del RESULT */
    void parsearSensoresJson(const std::string& json);

    /* Hilo de red */
    NetworkThread m_red;

    /* Datos de conexión (campos del formulario) */
    char m_host[128];
    char m_port_str[8];
    char m_username[64];
    char m_password[64];

    /* Estado de la GUI */
    bool m_conectado;

    /* Datos para las vistas */
    std::map<std::string, InfoSensor> m_sensores;
    std::vector<Alerta>               m_alertas;
    std::vector<std::string>          m_resultados;   /* últimos RESULT recibidos */
    std::string                       m_ultimo_status; /* último STATUSR */
    std::vector<std::string>          m_log_eventos;   /* log visual de eventos */
};

#endif
