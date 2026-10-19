#pragma once

#include <LiquidCrystal_I2C.h>
#include "SensorAmbiental.h"

class DisplayAmbiental
{
  public:
    DisplayAmbiental();
    void iniciar();
    void definirLigada(bool ligada);
    bool estaLigada() const { return ligada; }
    void mostrarLeitura(const LeituraAmbiental& leitura);
    void atualizarLinhaStatus(const String& texto);
    void exibirProgressoConexao(uint8_t passo);
    void mostrarLeituraComTendencia(const LeituraAmbiental& leitura, int tendencia);
    void iniciarAtualizacaoOta();
    void mostrarProgressoOta(uint8_t porcentagem);
    void mostrarLeituraComDadosExternos(const LeituraAmbiental& leitura, int tendencia,
                                        float temperaturaExterna, float umidadeExterna);

  private:
    LiquidCrystal_I2C lcd;
    bool ligada = true;
    bool layoutDesenhado = false;
};
