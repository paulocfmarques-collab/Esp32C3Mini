#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h> // Biblioteca PubSubClient, de Nick O'Leary (>= 2.8).

class MonitorAmbiental;

class IntegracaoHomeAssistant
{
  public:
    IntegracaoHomeAssistant();
    void iniciar(MonitorAmbiental& dispositivo);
    void processar();
    void configurarRotas(WebServer& web);
    bool conectado() { return mqtt.connected(); }
    uint32_t mensagensEnviadas() const { return enviadas; }
    uint32_t mensagensRecebidas() const { return recebidas; }
    uint32_t errosPublicacao() const { return erros; }
    String status();
    String obterUsuarioCadastrado() const { return reconfigurar ? proxima.usuario : config.usuario; }
    String obterSenhaCadastrada() const { return reconfigurar ? proxima.senha : config.senha; }
    void redescobrir() { descobertaPendente = true; publicarAgora = true; }

  private:
    struct Configuracao
    {
        uint32_t versao;
        uint16_t porta;
        uint16_t intervalo;
        uint8_t ativo;
        char host[128];
        char usuario[64];
        char senha[128];
    } config{1, 1883, 30, 0, "", "", ""};
    Configuracao proxima{};
    MonitorAmbiental* monitor = nullptr;
    WiFiClient tcp;
    PubSubClient mqtt;
    String id, base;
    bool bufferPronto = false;
    bool reconfigurar = false;
    bool primeiraTentativa = true;
    bool descobertaPendente = false;
    bool publicarAgora = true;
    bool atualizarClima = false;
    int comandoTela = -1;
    uint32_t ultimaTentativa = 0, ultimaPublicacao = 0, ultimaDescoberta = 0;
    uint32_t enviadas = 0, recebidas = 0, erros = 0;

    bool publicar(const String& topico, const String& payload, bool reter);
    bool publicarDescoberta(bool remover = false);
    bool publicarEstado();
    void receber(char* topico, uint8_t* payload, unsigned int tamanho);
    String dispositivoJson() const;
    void pagina(WebServer& web);
    void salvar(WebServer& web);
};
