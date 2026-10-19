#include "SensorAmbiental.h"
#include <math.h>
#include "Log.h"

bool LeituraAmbiental::valida() const
{
    return isfinite(temperatura) && isfinite(umidade);
}

SensorAmbiental::SensorAmbiental(uint8_t pino) : pino(pino)
{
}

void SensorAmbiental::iniciar()
{
    LOG_INFO("Iniciando sensor ambiental");
    pinMode(pino, INPUT_PULLUP);
    ultimaTentativa = millis(); // Aguarde a estabilização do DHT22 ao ligar.
    ultimaLeitura = {NAN, NAN};
}

bool SensorAmbiental::aguardarPulso(uint8_t nivel, uint32_t inicioQuadro,
                                   uint32_t &duracao)
{
    const uint32_t inicio = micros();
    while (digitalRead(pino) == nivel)
    {
        const uint32_t agora = micros();
        if (agora - inicio >= 150 || agora - inicioQuadro >= 7000)
        {
            return false;
        }
    }
    duracao = micros() - inicio;
    return duracao > 0 && duracao < 150 && micros() - inicioQuadro < 7000;
}

LeituraAmbiental SensorAmbiental::ler()
{
    // API web, comandos e display compartilham uma tentativa a cada dois segundos.
    if (millis() - ultimaTentativa < 2000)
    {
        return ultimaLeitura;
    }
    ultimaTentativa = millis();
    LOG_INFO("Lendo dados do sensor ambiental");
    ultimaLeitura = {NAN, NAN};

    digitalWrite(pino, LOW);
    pinMode(pino, OUTPUT);
    delayMicroseconds(1100);
    pinMode(pino, INPUT_PULLUP);
    const uint32_t inicioQuadro = micros();
    uint32_t duracao;
    uint8_t dados[5] = {};

    // Mantenha as interrupções habilitadas, inclusive as da USB Serial/JTAG.
    // Uma interrupção pode invalidar uma amostra; o checksum impede publicá-la.
    bool sucesso = aguardarPulso(HIGH, inicioQuadro, duracao) &&
                   aguardarPulso(LOW, inicioQuadro, duracao) &&
                   aguardarPulso(HIGH, inicioQuadro, duracao);
    for (int bit = 0; sucesso && bit < 40; bit++)
    {
        uint32_t baixo, alto;
        sucesso = aguardarPulso(LOW, inicioQuadro, baixo) &&
                  aguardarPulso(HIGH, inicioQuadro, alto);
        if (sucesso)
        {
            dados[bit / 8] = (dados[bit / 8] << 1) | (alto > 50 ? 1 : 0);
        }
    }

    if (sucesso && static_cast<uint8_t>(dados[0] + dados[1] + dados[2] + dados[3]) == dados[4])
    {
        const float umidade = ((uint16_t(dados[0]) << 8) | dados[1]) * 0.1f;
        float temperatura = ((uint16_t(dados[2] & 0x7f) << 8) | dados[3]) * 0.1f;
        if (dados[2] & 0x80)
        {
            temperatura = -temperatura;
        }
        if (umidade <= 100 && temperatura >= -40 && temperatura <= 80)
        {
            ultimaLeitura = {temperatura, umidade};
        }
    }

    if (!ultimaLeitura.valida())
    {
        LOG_WARN("[DHT22] Amostra descartada (timeout, checksum ou faixa invalida)");
    }
    LOG_INFO("Leitura do sensor concluida");
    return ultimaLeitura;
}
