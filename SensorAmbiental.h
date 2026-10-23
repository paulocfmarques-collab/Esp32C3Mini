#pragma once

#include <Arduino.h>
#include <math.h>

struct LeituraAmbiental
{
    float temperatura;
    float umidade;

    bool valida() const;
};

class SensorAmbiental
{
  public:
    explicit SensorAmbiental(uint8_t pino);
    void iniciar();
    LeituraAmbiental ler();
    LeituraAmbiental lerParaDisplay(bool& emCache);

  private:
    uint8_t pino;
    uint32_t ultimaTentativa = 0;
    LeituraAmbiental ultimaLeitura{NAN, NAN};
    LeituraAmbiental ultimaValida{NAN, NAN};
    int64_t ultimaValidaUs = -1;
    bool aguardarPulso(uint8_t nivel, uint32_t inicioQuadro, uint32_t &duracao);
};
