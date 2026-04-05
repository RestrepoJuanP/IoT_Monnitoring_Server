/*
 * Módulo 3 — Cliente Operador GUI
 *
 * Punto de entrada de la aplicación.
 * Inicializa la GUI (Dear ImGui + GLFW + OpenGL3) y el hilo de red.
 * Toda la comunicación con el servidor IOTP ocurre en un hilo separado
 * para nunca bloquear la interfaz gráfica.
 */

#include "gui.h"
#include <cstdio>

int main()
{
    std::fprintf(stdout, "IoT Monitor - Cliente Operador\n");
    std::fprintf(stdout, "Iniciando interfaz grafica...\n");

    OperadorGui gui;
    int resultado = gui.ejecutar();

    if (resultado != 0) {
        std::fprintf(stderr, "Error: la GUI termino con codigo %d\n", resultado);
    }

    return resultado;
}
