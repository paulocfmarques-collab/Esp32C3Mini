#include "MonitorAmbiental.h"
#include "ProcessadorComandos.h"
#include "GerenciadorOta.h"
#include "Log.h"
#include "ServicoPrometheus.h"

MonitorAmbiental monitor;
ProcessadorComandos processador(monitor);
GerenciadorOta otaIde(monitor);
ServicoPrometheus prometheus;

void setup()
{
    Serial.begin(115200);
    LOG_INFO("Sistema iniciado");
    monitor.iniciar();
    prometheus.iniciar(monitor);
    processador.iniciar();

    otaIde.iniciar();
}

void loop()
{
    monitor.atualizar();
    processador.atualizar();
    otaIde.atualizar();
    prometheus.processar();
    delay(1);
}
