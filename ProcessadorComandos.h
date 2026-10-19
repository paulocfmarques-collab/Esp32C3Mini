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

  private:
    MonitorAmbiental& _monitor;
    WiFiUDP _udp;
    String _bufferSerial;
    char _packetBuffer[256];
    const unsigned int _portaUdp = 4210;

    void executarComandoBase(const String& cmd, const String& args, Print& resposta);
    void tratarWifiAdd(const String& args, Print& resposta);
    String obterMotivoReset(); // Método auxiliar para alimentar o CrashDiagnostics
};
