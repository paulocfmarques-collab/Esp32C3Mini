#include "RelogioNtp.h"
#include "Log.h"
#include <esp_netif.h>
#include <esp_sntp.h>
#include <stdlib.h>

bool RelogioNtp::aplicarConfiguracao()
{
    esp_err_t erro = esp_netif_init();
    if (erro != ESP_OK)
    {
        LOG_ERROR(("[NTP] Falha ao inicializar rede: " + String(esp_err_to_name(erro))).c_str());
        return false;
    }

    char fuso[24];
    // POSIX usa o sinal oposto ao deslocamento UTC; DST e um ajuste manual.
    snprintf(fuso, sizeof(fuso), "UTC%+d", -(_fusoHorario + (_dstAtivo ? 1 : 0)));
    if (setenv("TZ", fuso, 1) != 0)
    {
        LOG_ERROR("[NTP] Falha ao configurar fuso horario");
        return false;
    }
    tzset();

    if (esp_sntp_enabled())
    {
        esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.nist.gov");
    esp_sntp_setservername(2, "br.pool.ntp.org");
    esp_sntp_init();
    return true;
}

void RelogioNtp::iniciar()
{
    LOG_INFO("[NTP] Iniciando sincronizacao...");

    if (!aplicarConfiguracao())
    {
        return;
    }
    LOG_INFO("[NTP] Servico configurado; aguardando horario");

    struct tm tempo;

    if (getLocalTime(&tempo, 10000))
    {
        sincronizado = true;

        LOG_INFO("[NTP] Relogio sincronizado");
        LOG_INFO(obterDataHora().c_str());
    }
    else
    {
        LOG_ERROR("[NTP] Falha ao sincronizar");
    }
}

bool RelogioNtp::estaSincronizado() const
{
    return sincronizado;
}

String RelogioNtp::obterDataHora()
{
    struct tm tempo;

    if (!getLocalTime(&tempo))
    {
        return "--/--/---- --:--:--";
    }

    char buffer[32];

    strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", &tempo);

    return String(buffer);
}

String RelogioNtp::obterHora()
{
    struct tm tempo;

    if (!getLocalTime(&tempo))
    {
        return "--:--:--";
    }

    char buffer[16];

    strftime(buffer, sizeof(buffer), "%H:%M:%S", &tempo);

    return String(buffer);
}

String RelogioNtp::obterData()
{
    struct tm tempo;

    if (!getLocalTime(&tempo))
    {
        return "--/--/----";
    }

    char buffer[16];

    strftime(buffer, sizeof(buffer), "%d/%m/%Y", &tempo);

    return String(buffer);
}

void RelogioNtp::configurarFuso(int fusoHorario, bool dstAtivo)
{
    _fusoHorario = fusoHorario;
    _dstAtivo = dstAtivo;

    LOG_INFO(("[NTP] Aplicando GMT " + String(_fusoHorario) +
              " | DST: " + String(_dstAtivo ? "ON" : "OFF"))
                 .c_str());

    if (!aplicarConfiguracao())
    {
        return;
    }

    struct tm tempo;
    if (getLocalTime(&tempo, 2000))
    {
        sincronizado = true;
    }
}

void RelogioNtp::definirDst(bool ativo)
{
    configurarFuso(_fusoHorario, ativo);
}
