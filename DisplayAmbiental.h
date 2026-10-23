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
    void mostrarPagina(uint8_t pagina, const String& dataHora, const LeituraAmbiental& local,
                       float temperaturaExterna, float umidadeExterna, const String& ip,
                       const String& ssid, int rssi, bool conectado, bool localEmCache = false);
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
    String linhasAnteriores[4];
    void escreverLinha(uint8_t linha, String texto);
    void invalidarLinhas();
};
