#pragma once

#include <stdint.h>

// Máquina de estados sem acesso ao hardware, para reutilização e teste.
class BotaoExterno
{
  public:
    enum Evento : uint8_t { Nenhum, Pressionado, Solto, Longo };

    Evento atualizar(bool pressionado, uint32_t agora)
    {
        if (pressionado != leituraAnterior)
        {
            leituraAnterior = pressionado;
            ultimaMudanca = agora;
        }
        if (agora - ultimaMudanca >= 40 && pressionado != estavel)
        {
            estavel = pressionado;
            if (estavel)
            {
                inicioPressao = agora;
                longoEmitido = false;
                return Pressionado;
            }
            return Solto;
        }
        if (estavel && !longoEmitido && agora - inicioPressao >= 5000)
        {
            longoEmitido = true;
            return Longo;
        }
        return Nenhum;
    }

  private:
    bool leituraAnterior = false;
    bool estavel = false;
    bool longoEmitido = false;
    uint32_t ultimaMudanca = 0;
    uint32_t inicioPressao = 0;
};
