#pragma once

#include <Arduino.h>
#include <time.h>

class RelogioNtp
{
  private:
    bool sincronizado = false;
    int _fusoHorario = -3; // Padrão GMT-3
    bool _dstAtivo = false;
    bool aplicarConfiguracao();

  public:
    bool estaSincronizado() const;

    void iniciar();
    String obterDataHora();
    String obterHora();
    String obterData();
    void configurarFuso(int fusoHorario, bool dstAtivo = false);
    void definirDst(bool ativo);

    bool obterDst() const
    {
        return _dstAtivo;
    }

    int obterFuso() const
    {
        return _fusoHorario;
    }
};