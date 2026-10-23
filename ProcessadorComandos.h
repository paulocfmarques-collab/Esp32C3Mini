#pragma once

#include "MonitorAmbiental.h"
#include <WiFiUdp.h>

class ProcessadorComandos
{
  public:
    ProcessadorComandos(MonitorAmbiental& monitor);
    void iniciar();
    void atualizar();
    void processarString(const String& entrada, Print& resposta);
    bool udpListenerAtivo() const { return _udpAtivo; }
    uint32_t udpPacotesRecebidos() const { return _udpRecebidos; }
    uint32_t udpRespostasEnviadas() const { return _udpRespostas; }
    uint32_t udpErrosEnvio() const { return _udpErros; }

  private:
    MonitorAmbiental& _monitor;
    WiFiUDP _udp;
    String _bufferSerial;
    char _packetBuffer[256];
    const unsigned int _portaUdp = 4210;
    bool _udpAtivo = false;
    uint32_t _udpRecebidos = 0, _udpRespostas = 0, _udpErros = 0;
    uint32_t _ultimaTentativaUdp = 0;

    void executarComandoBase(const String& cmd, const String& args, Print& resposta);
    void tratarWifiAdd(const String& args, Print& resposta);
    String obterMotivoReset(); // Método auxiliar para alimentar o CrashDiagnostics
};
