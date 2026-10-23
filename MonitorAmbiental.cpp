#include "MonitorAmbiental.h"
#include <Arduino.h>
#include <Wire.h>
#include "Log.h"
#include <Preferences.h>
#include <esp_system.h>
#include <stdint.h>
#include <esp_timer.h>

namespace
{
struct PersistentBootCounters
{
    uint32_t version;
    uint32_t boots;
    uint32_t watchdogs;
};
PersistentBootCounters contadoresAtuais{1, 0, 0};
bool contadoresDisponiveis = false;

void registrarInicializacao()
{
    Preferences prefs;
    if (!prefs.begin("telemetry", false))
    {
        LOG_ERROR("Falha ao abrir NVS para registrar contadores.");
        return;
    }

    PersistentBootCounters counters{1, 0, 0};
    if (prefs.getBytesLength("boots") == sizeof(counters))
    {
        if (prefs.getBytes("boots", &counters, sizeof(counters)) != sizeof(counters) ||
            counters.version != 1)
        {
            counters = {1, 0, 0};
        }
    }

    ++counters.boots;
    const esp_reset_reason_t motivo = esp_reset_reason();
    if (motivo == ESP_RST_INT_WDT || motivo == ESP_RST_TASK_WDT || motivo == ESP_RST_WDT)
    {
        ++counters.watchdogs;
    }

    const bool salvo =
        prefs.putBytes("boots", &counters, sizeof(counters)) == sizeof(counters);
    prefs.end();

    contadoresDisponiveis = salvo;
    if (salvo) contadoresAtuais = counters;

    if (!salvo)
    {
        LOG_ERROR("Falha ao salvar contadores de inicializacao na NVS.");
    }
}
} // namespace

bool MonitorAmbiental::contadoresValidos() const { return contadoresDisponiveis; }
uint32_t MonitorAmbiental::obterBoots() const { return contadoresAtuais.boots; }
uint32_t MonitorAmbiental::obterWatchdogs() const { return contadoresAtuais.watchdogs; }

void MonitorAmbiental::iniciarAtualizacaoOta()
{
    otaEmAndamento = true;
    display.iniciarAtualizacaoOta();
}

void MonitorAmbiental::definirTelaLigada(bool ligada)
{
    display.definirLigada(ligada);
    if (ligada)
    {
        paginaLcd = 0;
        ultimaInteracaoLcd = millis();
        atualizarLcd = true;
    }
}

void MonitorAmbiental::tarefaBotao(void* contexto)
{
    auto* monitor = static_cast<MonitorAmbiental*>(contexto);
    for (;;)
    {
        const auto evento = monitor->botao.atualizar(digitalRead(PINO_BOTAO) == LOW, millis());
        if (evento != BotaoExterno::Nenhum)
        {
            // Apenas enfileire eventos: I2C e estado da tela pertencem ao loop principal.
            xQueueSend(monitor->filaBotao, &evento, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void MonitorAmbiental::iniciarBotao()
{
    pinMode(PINO_BOTAO, INPUT_PULLUP);
    filaBotao = xQueueCreate(16, sizeof(BotaoExterno::Evento));
    if (filaBotao && xTaskCreate(tarefaBotao, "botao", 2048, this, 1, nullptr) == pdPASS)
        return;
    if (filaBotao) vQueueDelete(filaBotao);
    filaBotao = nullptr;
    LOG_WARN("[BOTAO] Sem memoria para tarefa; usando leitura pelo loop");
}

void MonitorAmbiental::tratarEventoBotao(BotaoExterno::Evento evento)
{
    if (otaEmAndamento) return;
    if (evento != BotaoExterno::Nenhum) ultimaInteracaoLcd = millis();
    if (evento == BotaoExterno::Pressionado)
    {
        ignorarGestoBotao = !display.estaLigada();
        if (ignorarGestoBotao) definirTelaLigada(true);
    }
    else if (evento == BotaoExterno::Longo)
    {
        if (!ignorarGestoBotao && display.estaLigada()) definirTelaLigada(false);
        ignorarGestoBotao = true;
    }
    else if (evento == BotaoExterno::Solto)
    {
        if (!ignorarGestoBotao && display.estaLigada())
        {
            paginaLcd = (paginaLcd + 1) % 2;
            atualizarLcd = true;
        }
        ignorarGestoBotao = false;
    }
}

void MonitorAmbiental::processarBotao()
{
    if (!filaBotao)
    {
        tratarEventoBotao(botao.atualizar(digitalRead(PINO_BOTAO) == LOW, millis()));
        return;
    }
    BotaoExterno::Evento evento;
    while (xQueueReceive(filaBotao, &evento, 0) == pdTRUE) tratarEventoBotao(evento);
}

void MonitorAmbiental::mostrarProgressoOta(uint8_t porcentagem)
{
    display.mostrarProgressoOta(porcentagem);
}

bool MonitorAmbiental::wifiConectado() const
{
    return wifi.conectado();
}

void MonitorAmbiental::iniciar()
{
    LOG_INFO("Iniciando monitor ambiental");
    registrarInicializacao();
    Wire.begin(8, 9);
    display.iniciar(); // Desenha a tela de boot vazia com o casco da barra
    sensor.iniciar();
    iniciarBotao();

    Preferences prefs;
    prefs.begin("wifi", true);
    int fusoSalvo = prefs.getInt("fuso", -3);
    bool dstSalvo = prefs.getBool("dst", false);
    prefs.end();

    relogio.configurarFuso(fusoSalvo, dstSalvo);
    wifi.iniciar();
    LOG_INFO("Inicializacao do monitor concluida");
}

bool MonitorAmbiental::zerarContadores()
{
    Preferences prefs;
    if (!prefs.begin("telemetry", false))
    {
        return false;
    }

    PersistentBootCounters counters{1, 0, 0};
    const bool salvo =
        prefs.putBytes("boots", &counters, sizeof(counters)) == sizeof(counters);
    prefs.end();
    if (salvo)
    {
        contadoresAtuais = counters;
        contadoresDisponiveis = true;
    }
    return salvo;
}

void MonitorAmbiental::atualizar()
{
    processarBotao();
    wifi.processar();
    if (otaEmAndamento) return;

    const uint32_t agora = millis();
    if (display.estaLigada() && paginaLcd != 0 && agora - ultimaInteracaoLcd >= 15000UL)
    {
        paginaLcd = 0;
        atualizarLcd = true;
    }
    static uint32_t ultimaLeitura = 0;
    static uint32_t ultimaTela = 0;
    static float temperaturaAnterior = NAN;

    // Mantenha o sensor local ativo também quando a rede estiver desconectada.
    if (agora - ultimaLeitura >= 2000)
    {
        ultimaLeitura = agora;
        leituraLcd = sensor.ler();
        if (leituraLcd.valida())
        {
            _tendenciaAtual = 0;
            if (isfinite(temperaturaAnterior))
            {
                if (leituraLcd.temperatura > temperaturaAnterior + 0.05f) _tendenciaAtual = 1;
                else if (leituraLcd.temperatura < temperaturaAnterior - 0.05f) _tendenciaAtual = -1;
            }
            temperaturaAnterior = leituraLcd.temperatura;
        }
        atualizarLcd = true;
    }

    if (display.estaLigada() && (atualizarLcd || agora - ultimaTela >= 1000))
    {
        struct tm tempo;
        char dataHora[21] = "--/--/---- --:--:--";
        if (getLocalTime(&tempo, 0))
            strftime(dataHora, sizeof(dataHora), "%d/%m/%Y %H:%M:%S", &tempo);
        const bool conectado = wifi.conectado();
        bool localEmCache = false;
        const LeituraAmbiental leituraExibida = sensor.lerParaDisplay(localEmCache);
        display.mostrarPagina(paginaLcd, dataHora, leituraExibida, climaPoa.temperatura,
                              climaPoa.umidade, conectado ? wifi.obterIp() : WiFi.softAPIP().toString(),
                              WiFi.SSID(), conectado ? WiFi.RSSI() : 0, conectado, localEmCache);
        ultimaTela = agora;
        atualizarLcd = false;
    }

    processarBotao();
    if (wifi.conectado() && (!climaConsultado || agora - ultimaLeituraPoa >= 900000UL))
    {
        forcarSincronizacaoClima();
        atualizarLcd = true;
    }
}

bool MonitorAmbiental::forcarSincronizacaoClima()
{
    climaConsultado = true;
    ultimaLeituraPoa = millis();
    if (wifi.conectado())
    {
        LOG_WARN("[UDP] Forçando sincronizacao climatica imediata...");
        DadosCidade novosDados = servicoClima.obterClimaPortoAlegre();
        if (novosDados.valida)
        {
            climaPoa = novosDados;
            ultimaAtualizacaoClimaUs = esp_timer_get_time();
            return true;
        }
        else
            LOG_WARN("[CLIMA] Atualizacao falhou; mantendo ultima leitura valida, se houver");
    }
    else
        LOG_ERROR("[CLIMA] Sincronizacao indisponivel sem Wi-Fi");
    return false;
}

float MonitorAmbiental::obterTemperaturaPoa() const
{
    return climaPoa.temperatura;
}

float MonitorAmbiental::obterUmidadePoa() const
{
    return climaPoa.umidade;
}

double MonitorAmbiental::obterIdadeClimaSegundos() const
{
    if (ultimaAtualizacaoClimaUs < 0)
    {
        return NAN;
    }
    return (esp_timer_get_time() - ultimaAtualizacaoClimaUs) / 1000000.0;
}
