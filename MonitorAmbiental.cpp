#include "MonitorAmbiental.h"
#include <Arduino.h>
#include <Wire.h>
#include "Log.h"
#include <Preferences.h>
#include <esp_system.h>
#include <stdint.h>
#include <esp_timer.h>

enum EstadoStatus
{
    EXIBIR_IP,
    EXIBIR_HORA,
    EXIBIR_DATA
};

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
    display.iniciarAtualizacaoOta();
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
    wifi.processar();

    unsigned long tempoAtual = millis();
    static unsigned long ultimaLeitura = 0;
    static unsigned long ultimaAlternancia = 0;
    static unsigned long ultimoPassoBarra = 0;
    static uint8_t passoAnimacao = 0;

    // Variáveis de histórico para cálculo de tendência térmica
    static float ultimaTemperatura = -999.0;
    static EstadoStatus estadoAtual = EXIBIR_IP;

    // FASE A: Se o Wi-Fi não estiver conectado, atualiza a animação da barra de progresso
    if (!wifi.conectado())
    {
        if (tempoAtual - ultimoPassoBarra >= 500) // Incrementa 1 bloco a cada 500ms
        {
            ultimoPassoBarra = tempoAtual;
            display.exibirProgressoConexao(passoAnimacao);
            passoAnimacao++;

            // Se estourar o limite visual dos colchetes, reseta limpando a linha
            if (passoAnimacao > 18)
            {
                passoAnimacao = 0;
            }
        }
        return; // Bloqueia a telemetria até estabelecer o IP estável
    }

    if (!climaConsultado || tempoAtual - ultimaLeituraPoa >= 900000UL)
        forcarSincronizacaoClima();

    // FASE B: Wi-Fi Conectado. Executa carrossel e monitoramento de sensores
    if (tempoAtual - ultimaLeitura >= 2000)
    {
        ultimaLeitura = tempoAtual;
        LeituraAmbiental leitura = sensor.ler();

        // Procure por esta linha dentro do loop condicional de leitura do sensor (2000ms):
        if (leitura.valida())
        {
            LOG_INFO("Atualizando display com calculo de tendencia");

            if (ultimaTemperatura != -999.0)
            {
                if (leitura.temperatura > ultimaTemperatura + 0.05)
                {
                    _tendenciaAtual = 1; // Temperatura subindo ▲
                }
                else if (leitura.temperatura < ultimaTemperatura - 0.05)
                {
                    _tendenciaAtual = -1; // Temperatura caindo ▼
                }
                else
                {
                    _tendenciaAtual = 0; // Estável
                }
            }

            ultimaTemperatura = leitura.temperatura;
            display.mostrarLeituraComDadosExternos(leitura, _tendenciaAtual,
                                                   climaPoa.temperatura, climaPoa.umidade);
        }
    }

    // Alternância do carrossel da 4ª linha (IP, Hora, Data)
    if (tempoAtual - ultimaAlternancia >= 3000)
    {
        ultimaAlternancia = tempoAtual;

        switch (estadoAtual)
        {
        case EXIBIR_IP:
        {
            String ip = wifi.obterIp();
            display.atualizarLinhaStatus(ip.isEmpty() ? "CONECTANDO" : ip);
            estadoAtual = EXIBIR_HORA;
        }
        break;

        case EXIBIR_HORA:
        {
            String hora = relogio.obterHora();
            display.atualizarLinhaStatus(hora == "--:--:--" ? "SINC. NTP" : hora);
            estadoAtual = EXIBIR_DATA;
        }
        break;

        case EXIBIR_DATA:
        {
            String data = relogio.obterData();
            display.atualizarLinhaStatus(data == "--/--/----" ? "SINC. NTP" : data);
            estadoAtual = EXIBIR_IP;
        }
        break;
        }
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
