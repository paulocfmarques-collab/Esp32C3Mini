#include "GerenciadorOta.h"
#include "Log.h"
#include <ArduinoOTA.h>

GerenciadorOta::GerenciadorOta(MonitorAmbiental& monitor) : _monitor(monitor)
{
}

void GerenciadorOta::iniciar()
{
    // Nome visível na lista de portas de rede (Network Ports) da Arduino IDE
    ArduinoOTA.setHostname("MonitorAmbiental-OTA");

    // Gatilho executado no instante do clique de Upload na IDE
    ArduinoOTA.onStart(
        [this]()
        {
            String tipo = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
            LOG_WARN("[OTA IDE] Iniciando gravacao remota do " + tipo);

            _monitor.iniciarAtualizacaoOta();
        });

    ArduinoOTA.onEnd([]()
                     { LOG_INFO("\n[OTA IDE] Gravacao concluida com sucesso! Reiniciando..."); });

    // Gatilho de repetição contínua que acompanha o fluxo de bytes transmitidos
    ArduinoOTA.onProgress(
        [this](unsigned int progresso, unsigned int total)
        {
            if (total == 0)
            {
                LOG_ERROR("[OTA IDE] Total de bytes invalido no progresso");
                return;
            }
            unsigned int porcentagem =
                progresso >= total ? 100 : (static_cast<uint64_t>(progresso) * 100) / total;

            // Exibe o contador textual no monitor Serial (UART)
            Serial.printf("[OTA IDE] Progresso: %u%%\r", porcentagem);

            _monitor.mostrarProgressoOta(static_cast<uint8_t>(porcentagem));
        });

    ArduinoOTA.onError(
        [this](ota_error_t erro)
        {
            _monitor.finalizarAtualizacaoOta();
            Serial.printf("[OTA IDE] ERRO (%u): ", erro);
            if (erro == OTA_AUTH_ERROR)
                LOG_ERROR("Falha na autenticacao de senha");
            else if (erro == OTA_BEGIN_ERROR)
                LOG_ERROR("Falha ao iniciar gravacao");
            else if (erro == OTA_CONNECT_ERROR)
                LOG_ERROR("Falha de conexao");
            else if (erro == OTA_RECEIVE_ERROR)
                LOG_ERROR("Falha na recepcao de pacotes");
            else if (erro == OTA_END_ERROR)
                LOG_ERROR("Falha na finalizacao da particao");
        });

    ArduinoOTA.begin();
    LOG_INFO("[OTA IDE] Servico de rede ArduinoOTA inicializado e ativo");
}

void GerenciadorOta::atualizar()
{
    if (_monitor.wifiConectado())
    {
        ArduinoOTA.handle();
    }
}
