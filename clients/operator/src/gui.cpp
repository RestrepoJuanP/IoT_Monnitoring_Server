#include "gui.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <cstring>
#include <cstdio>
#include <algorithm>

/* Colores para el indicador de estado */
static const ImVec4 COLOR_ROJO    = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
static const ImVec4 COLOR_VERDE   = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
static const ImVec4 COLOR_AMARILLO = ImVec4(1.0f, 1.0f, 0.2f, 1.0f);

OperadorGui::OperadorGui()
    : m_conectado(false)
{
    std::memset(m_host, 0, sizeof(m_host));
    std::memset(m_port_str, 0, sizeof(m_port_str));
    std::memset(m_username, 0, sizeof(m_username));
    std::memset(m_password, 0, sizeof(m_password));

    /* Valores por defecto */
    std::strncpy(m_host, "localhost", sizeof(m_host) - 1);
    std::strncpy(m_port_str, "9000", sizeof(m_port_str) - 1);
    std::strncpy(m_username, "operator1", sizeof(m_username) - 1);
    std::strncpy(m_password, "op123", sizeof(m_password) - 1);
}

OperadorGui::~OperadorGui()
{
    m_red.detener();
}

int OperadorGui::ejecutar()
{
    /* Inicializar GLFW */
    if (!glfwInit()) {
        std::fprintf(stderr, "Error: no se pudo inicializar GLFW\n");
        return -1;
    }

    /* Configurar OpenGL 3.3 core */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1100, 750,
        "IoT Monitor - Operador", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Error: no se pudo crear la ventana GLFW\n");
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  /* VSync */

    /* Inicializar Dear ImGui */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    /* Loop principal de renderizado */
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        /* Procesar mensajes del hilo de red sin bloquear */
        procesarMensajes();

        /* Nuevo frame */
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        renderFrame();

        /* Render */
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    /* Desconectar antes de cerrar */
    if (m_conectado) {
        m_red.disconnect();
        m_red.detener();
    }

    /* Limpieza */
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

void OperadorGui::renderFrame()
{
    /* Ventana principal que ocupa todo el espacio */
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("IoT Monitor", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_MenuBar);

    /* Barra de menú con el título */
    if (ImGui::BeginMenuBar()) {
        ImGui::Text("IoT Monitoring System - Panel de Operador");
        ImGui::EndMenuBar();
    }

    /* Indicador de estado + panel de conexión */
    renderIndicadorEstado();
    ImGui::SameLine();
    renderPanelConexion();

    ImGui::Separator();

    /* Botones de acción */
    renderBotonesAccion();

    ImGui::Separator();

    /* Layout: sensores a la izquierda, alertas a la derecha */
    float ancho_total = ImGui::GetContentRegionAvail().x;

    /* Columna izquierda: sensores + resultados */
    ImGui::BeginChild("PanelIzquierdo", ImVec2(ancho_total * 0.55f, 0), true);
    renderTablaSensores();
    ImGui::Spacing();
    renderPanelResultados();
    ImGui::EndChild();

    ImGui::SameLine();

    /* Columna derecha: alertas */
    ImGui::BeginChild("PanelDerecho", ImVec2(0, 0), true);
    renderPanelAlertas();
    ImGui::EndChild();

    ImGui::End();
}

void OperadorGui::renderPanelConexion()
{
    EstadoConexion estado = m_red.getEstado();
    bool en_proceso = (estado == EstadoConexion::CONECTANDO ||
                       estado == EstadoConexion::RECONECTANDO);

    if (!m_conectado) {
        ImGui::SetNextItemWidth(120);
        ImGui::InputText("Host", m_host, sizeof(m_host));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        ImGui::InputText("Puerto", m_port_str, sizeof(m_port_str));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::InputText("Usuario", m_username, sizeof(m_username));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::InputText("Clave", m_password, sizeof(m_password),
                         ImGuiInputTextFlags_Password);
        ImGui::SameLine();

        if (en_proceso) {
            ImGui::BeginDisabled();
            ImGui::Button("Conectando...");
            ImGui::EndDisabled();
        } else {
            if (ImGui::Button("Conectar")) {
                int port = std::atoi(m_port_str);
                if (port > 0) {
                    m_red.conectar(m_host, port, m_username, m_password);
                    m_conectado = true;
                }
            }
        }
    }
}

void OperadorGui::renderIndicadorEstado()
{
    EstadoConexion estado = m_red.getEstado();
    ImVec4 color;
    const char* texto;

    switch (estado) {
        case EstadoConexion::CONECTADO:
            color = COLOR_VERDE;
            texto = "CONECTADO";
            break;
        case EstadoConexion::CONECTANDO:
            color = COLOR_AMARILLO;
            texto = "CONECTANDO...";
            break;
        case EstadoConexion::RECONECTANDO:
            color = COLOR_AMARILLO;
            texto = "RECONECTANDO...";
            break;
        default:
            color = COLOR_ROJO;
            texto = "DESCONECTADO";
            /* Si estábamos conectados y ahora no, actualizar bandera */
            if (m_conectado) {
                m_conectado = false;
            }
            break;
    }

    /* Círculo de color + texto */
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float radio = 6.0f;
    draw->AddCircleFilled(ImVec2(pos.x + radio, pos.y + radio + 2), radio,
                          ImGui::ColorConvertFloat4ToU32(color));
    ImGui::Dummy(ImVec2(radio * 2 + 4, radio * 2));
    ImGui::SameLine();
    ImGui::TextColored(color, "%s", texto);
}

void OperadorGui::renderTablaSensores()
{
    ImGui::Text("Sensores Activos (%zu)", m_sensores.size());

    if (ImGui::BeginTable("TablaSensores", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 200))) {

        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_None, 100);
        ImGui::TableSetupColumn("Tipo", ImGuiTableColumnFlags_None, 100);
        ImGui::TableSetupColumn("Ultimo Valor", ImGuiTableColumnFlags_None, 100);
        ImGui::TableSetupColumn("Timestamp", ImGuiTableColumnFlags_None, 180);
        ImGui::TableHeadersRow();

        for (auto& [id, sensor] : m_sensores) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", sensor.id.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", sensor.tipo.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", sensor.ultimo_valor.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%s", sensor.timestamp.c_str());
        }

        ImGui::EndTable();
    }
}

void OperadorGui::renderPanelAlertas()
{
    ImGui::Text("Alertas (%zu)", m_alertas.size());

    if (ImGui::Button("Limpiar Alertas")) {
        m_alertas.clear();
    }

    ImGui::BeginChild("ScrollAlertas", ImVec2(0, 0), false,
                       ImGuiWindowFlags_HorizontalScrollbar);

    /* Más recientes arriba */
    for (int i = (int)m_alertas.size() - 1; i >= 0; i--) {
        const Alerta& a = m_alertas[i];
        ImGui::PushStyleColor(ImGuiCol_Text, COLOR_ROJO);
        ImGui::Text("[%s] ALERTA: %s (%s) = %s (umbral: %s)",
                    a.timestamp.c_str(), a.sensor_id.c_str(),
                    a.tipo.c_str(), a.valor.c_str(), a.umbral.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
}

void OperadorGui::renderPanelResultados()
{
    ImGui::Text("Resultados de Consultas");

    /* Estado del servidor */
    if (!m_ultimo_status.empty()) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                           "Estado: %s", m_ultimo_status.c_str());
    }

    /* Últimos resultados de QUERY */
    ImGui::BeginChild("ScrollResultados", ImVec2(0, 150), true,
                       ImGuiWindowFlags_HorizontalScrollbar);

    for (int i = (int)m_resultados.size() - 1; i >= 0; i--) {
        ImGui::TextWrapped("%s", m_resultados[i].c_str());
        ImGui::Separator();
    }

    ImGui::EndChild();
}

void OperadorGui::renderBotonesAccion()
{
    bool habilitado = (m_red.getEstado() == EstadoConexion::CONECTADO);

    if (!habilitado) ImGui::BeginDisabled();

    if (ImGui::Button("Consultar Sensores")) {
        m_red.sendQuery("SENSORS");
    }
    ImGui::SameLine();

    if (ImGui::Button("Consultar Mediciones")) {
        m_red.sendQuery("MEASUREMENTS");
    }
    ImGui::SameLine();

    if (ImGui::Button("Consultar Alertas")) {
        m_red.sendQuery("ALERTS");
    }
    ImGui::SameLine();

    if (ImGui::Button("Estado del Servidor")) {
        m_red.sendStatus();
    }
    ImGui::SameLine();

    /* Botón desconectar en rojo */
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("Desconectar")) {
        m_red.disconnect();
        m_red.detener();
        m_conectado = false;
    }
    ImGui::PopStyleColor();

    if (!habilitado) ImGui::EndDisabled();
}

void OperadorGui::procesarMensajes()
{
    auto mensajes = m_red.obtenerMensajes();

    for (const auto& msg : mensajes) {
        if (msg.opcode == "ALERT" && msg.campos.size() >= 5) {
            /* ALERT|sensor_id|tipo|valor|umbral|timestamp */
            Alerta alerta;
            alerta.sensor_id = msg.campos[0];
            alerta.tipo      = msg.campos[1];
            alerta.valor     = msg.campos[2];
            alerta.umbral    = msg.campos[3];
            alerta.timestamp = msg.campos[4];
            m_alertas.push_back(alerta);

        } else if (msg.opcode == "RESULT" && msg.campos.size() >= 2) {
            /* RESULT|tipo_query|datos */
            std::string entrada = "[" + msg.campos[0] + "] " + msg.campos[1];
            m_resultados.push_back(entrada);

            /* Si es SENSORS, intentar actualizar la tabla */
            if (msg.campos[0] == "SENSORS") {
                parsearSensoresJson(msg.campos[1]);
            }

            /* Limitar historial */
            if (m_resultados.size() > 50) {
                m_resultados.erase(m_resultados.begin());
            }

        } else if (msg.opcode == "STATUSR" && msg.campos.size() >= 3) {
            /* STATUSR|sensores|operadores|uptime */
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "Sensores: %s | Operadores: %s | Uptime: %ss",
                          msg.campos[0].c_str(), msg.campos[1].c_str(),
                          msg.campos[2].c_str());
            m_ultimo_status = buf;

        } else if (msg.opcode == "ACK") {
            /* ACK de registro u otras operaciones — solo log visual */
            if (!msg.campos.empty()) {
                m_log_eventos.push_back("ACK: " + msg.campos[0]);
            }

        } else if (msg.opcode == "ERROR") {
            /* Mostrar error como alerta especial */
            Alerta err;
            err.sensor_id = "SERVIDOR";
            err.tipo = "ERROR";
            err.valor = msg.campos.size() > 0 ? msg.campos[0] : "desconocido";
            err.umbral = msg.campos.size() > 1 ? msg.campos[1] : "";
            err.timestamp = "---";
            m_alertas.push_back(err);
        }
    }
}

void OperadorGui::parsearSensoresJson(const std::string& json)
{
    /*
     * Parser simple para el formato JSON del servidor:
     * [{"id":"temp01","tipo":"temperatura","activo":true}, ...]
     * No es un parser JSON completo — solo para el formato conocido.
     */
    m_sensores.clear();

    size_t pos = 0;
    while (pos < json.size()) {
        /* Buscar cada objeto {...} */
        size_t inicio = json.find('{', pos);
        if (inicio == std::string::npos) break;
        size_t fin = json.find('}', inicio);
        if (fin == std::string::npos) break;

        std::string objeto = json.substr(inicio + 1, fin - inicio - 1);
        pos = fin + 1;

        InfoSensor sensor;

        /* Extraer campos por búsqueda de string */
        auto extraerCampo = [&](const std::string& clave) -> std::string {
            std::string buscar = "\"" + clave + "\":\"";
            size_t p = objeto.find(buscar);
            if (p == std::string::npos) return "";
            p += buscar.size();
            size_t fin_val = objeto.find('"', p);
            if (fin_val == std::string::npos) return "";
            return objeto.substr(p, fin_val - p);
        };

        sensor.id   = extraerCampo("id");
        sensor.tipo = extraerCampo("tipo");
        sensor.ultimo_valor = extraerCampo("valor");
        sensor.timestamp    = extraerCampo("timestamp");

        if (!sensor.id.empty()) {
            m_sensores[sensor.id] = sensor;
        }
    }
}
