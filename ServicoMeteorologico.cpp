#include "ServicoMeteorologico.h"
#include "Log.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <math.h>

DadosCidade ServicoMeteorologico::obterClimaPortoAlegre()
{
    const DadosCidade indisponivel = {NAN, NAN, false};
    if (WiFi.status() != WL_CONNECTED)
    {
        LOG_ERROR("[CLIMA] Wi-Fi desconectado");
        return indisponivel;
    }

    NetworkClientSecure cliente;
    cliente.useBuiltinCACertBundle();
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    if (!http.begin(cliente,
                    "https://api.open-meteo.com/v1/forecast?latitude=-30.0346"
                    "&longitude=-51.2177&current=temperature_2m,relative_humidity_2m"))
    {
        LOG_ERROR("[CLIMA] Falha ao iniciar HTTPS");
        return indisponivel;
    }

    int status = http.GET();
    if (status != HTTP_CODE_OK)
    {
        LOG_ERROR("[CLIMA] Falha HTTP: " + String(status));
        http.end();
        return indisponivel;
    }

    String payload = http.getString();
    http.end();
    JsonDocument documento;
    DeserializationError erro = deserializeJson(documento, payload);
    if (erro)
    {
        LOG_ERROR("[CLIMA] JSON invalido: " + String(erro.c_str()));
        return indisponivel;
    }
    JsonVariantConst temperatura = documento["current"]["temperature_2m"];
    JsonVariantConst umidade = documento["current"]["relative_humidity_2m"];
    if (!temperatura.is<float>() || !umidade.is<float>())
    {
        LOG_ERROR("[CLIMA] Resposta sem temperatura ou umidade numericas");
        return indisponivel;
    }
    float t = temperatura.as<float>();
    float u = umidade.as<float>();
    if (!isfinite(t) || !isfinite(u) || t < -100 || t > 70 || u < 0 || u > 100)
    {
        LOG_ERROR("[CLIMA] Valores fora dos limites");
        return indisponivel;
    }
    return {t, u, true};
}
