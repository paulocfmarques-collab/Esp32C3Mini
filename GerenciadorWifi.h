#pragma once

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "RedeWifi.h"
#include "RelogioNtp.h"

class GerenciadorWifi
{
    friend class ProcessadorComandos;

  private:
    Preferences prefs;
    WebServer servidor;
    RelogioNtp relogio;

    RedeWifi redes[5];

    bool portalAtivo;

    void carregarRedes();
    bool salvarRedes();
    void configurarRotas();
    void paginaPrincipal();
    void salvarConfiguracao();
    void paginaDashboard();
    void apiDados();
    void executarComandoWeb();
    String injetarScriptsDashboard();

  public:
    RelogioNtp& obterRelogio()
    {
        return relogio;
    }

    GerenciadorWifi();
    void iniciar();
    bool conectar();
    void iniciarPortal();
    void processar();
    bool conectado() const;
    String obterIp() const;
};
