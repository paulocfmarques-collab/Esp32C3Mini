#include "MonitorAmbiental.h"
#include "ProcessadorComandos.h"
#include "GerenciadorOta.h"
#include "Log.h"
#include "ServicoPrometheus.h"
#include "IntegracaoHomeAssistant.h"

MonitorAmbiental monitor;
ProcessadorComandos processador(monitor);
GerenciadorOta otaIde(monitor);
ServicoPrometheus prometheus;
IntegracaoHomeAssistant homeAssistant;

void setup()
{
    Serial.begin(115200);
    LOG_INFO("Sistema iniciado");
    monitor.iniciar();
    homeAssistant.iniciar(monitor);
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
    homeAssistant.processar();
    delay(1);
}
