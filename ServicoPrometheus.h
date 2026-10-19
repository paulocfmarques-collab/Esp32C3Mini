#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <atomic>
#include <math.h>

class MonitorAmbiental;

class ServicoPrometheus
{
  public:
    void iniciar(MonitorAmbiental& monitor);
    void processar();
    void configurarRotas(WebServer& web);
    void registrarHttp(uint32_t microsInicio, bool erro);
    void atender(const std::function<void()>& funcao);
    void responder(WebServer& web, int codigo, const char* tipo, const String& corpo);

  private:
    struct Configuracao
    {
        uint32_t versao;
        uint16_t portaMetricas;
        uint16_t intervalo;
        char servidor[254];
    } config{2, 9120, 15, ""};

    MonitorAmbiental* monitor = nullptr;
    std::unique_ptr<WebServer> servidor;
    bool reconfigurar = false;
    std::atomic<uint32_t> desconexoes{0};
    uint32_t requisicoes = 0;
    uint32_t erros = 0;
    double duracaoTotalMs = 0;
    double ultimaDuracaoMs = 0;
    bool erroHttp = false;
    float usoCpu = NAN;
#if configUSE_TRACE_FACILITY && configGENERATE_RUN_TIME_STATS
    TaskStatus_t tarefas[64];
    configRUN_TIME_COUNTER_TYPE tempoOciosoAnterior[portNUM_PROCESSORS]{};
    int64_t tempoAmostraAnteriorUs = 0;
    uint32_t cpuAmostradoEmMs = 0;
    bool baselineCpuValida = false;
#endif

    void abrirServidor();
    void atualizarUsoCpu();
    String metricas();
    void pagina(WebServer& web);
    void salvar(WebServer& web);
    String configuracaoColeta() const;
};
