#pragma once

#include "SensorAmbiental.h"
#include "DisplayAmbiental.h"
#include "GerenciadorWifi.h"
#include "ServicoMeteorologico.h"
#include <math.h>

class MonitorAmbiental
{
    friend class ProcessadorComandos;

  private:
    SensorAmbiental sensor{2};
    int _tendenciaAtual = 0;
    ServicoMeteorologico servicoClima;
    DadosCidade climaPoa{NAN, NAN, false};
    unsigned long ultimaLeituraPoa = 0;
    bool climaConsultado = false;
    int64_t ultimaAtualizacaoClimaUs = -1;

    DisplayAmbiental display;
    GerenciadorWifi wifi;
    RelogioNtp& relogio = wifi.obterRelogio();

  public:
    void definirTelaLigada(bool ligada) { display.definirLigada(ligada); }
    bool telaLigada() const { return display.estaLigada(); }

    LeituraAmbiental lerSensor()
    {
        return sensor.ler();
    }

    void iniciar();
    void atualizar();
    void iniciarAtualizacaoOta();
    void mostrarProgressoOta(uint8_t porcentagem);
    bool wifiConectado() const;
    bool forcarSincronizacaoClima();
    bool zerarContadores();
    bool contadoresValidos() const;
    uint32_t obterBoots() const;
    uint32_t obterWatchdogs() const;
    float obterTemperaturaPoa() const;
    float obterUmidadePoa() const;
    double obterIdadeClimaSegundos() const;

    int obterTendenciaTermica() const
    {
        return _tendenciaAtual;
    }
};
