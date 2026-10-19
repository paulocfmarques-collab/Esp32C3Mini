#pragma once
#include <Arduino.h>

struct DadosCidade
{
    float temperatura;
    float umidade;
    bool valida;
};

class ServicoMeteorologico
{
  public:
    DadosCidade obterClimaPortoAlegre();
};
