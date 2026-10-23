#include "ServicoPrometheus.h"
#include "MonitorAmbiental.h"
#include "IntegracaoHomeAssistant.h"
#include "ProcessadorComandos.h"
#include "Log.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_app_desc.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <new>
#include <string.h>
#include <initializer_list>

extern IntegracaoHomeAssistant homeAssistant;
extern ProcessadorComandos processador;

namespace
{
String htmlSeguro(String s)
{
    s.replace("&", "&amp;");
    s.replace("\"", "&quot;");
    s.replace("'", "&#39;");
    s.replace("<", "&lt;");
    s.replace(">", "&gt;");
    return s;
}

String labelSeguro(String s)
{
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    s.replace("\n", "\\n");
    s.replace("\r", "");
    return s;
}

bool portaValida(uint16_t p)
{
    return p != 0 && p != 80 && p != 3232;
}

bool lerPorta(const String& s, uint16_t& porta)
{
    if (s.isEmpty() || s.length() > 5)
        return false;
    uint32_t valor = 0;
    for (size_t i = 0; i < s.length(); ++i)
    {
        if (s[i] < '0' || s[i] > '9')
            return false;
        valor = valor * 10 + s[i] - '0';
    }
    if (valor == 0 || valor > 65535)
        return false;
    porta = static_cast<uint16_t>(valor);
    return true;
}

bool hostValido(const String& s)
{
    if (s.isEmpty() || s.length() > 253)
        return false;
    unsigned tamanho = 0;
    char anterior = 0;
    for (size_t i = 0; i < s.length(); ++i)
    {
        const char c = s[i];
        const bool alnum =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (c == '.')
        {
            if (!tamanho || anterior == '-')
                return false;
            tamanho = 0;
        }
        else if ((!alnum && c != '-') || (!tamanho && c == '-') || ++tamanho > 63)
            return false;
        anterior = c;
    }
    return tamanho && anterior != '-';
}

const char* motivoReset()
{
    switch (esp_reset_reason())
    {
    case ESP_RST_POWERON:
        return "power_on";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt_watchdog";
    case ESP_RST_TASK_WDT:
        return "task_watchdog";
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deep_sleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    default:
        return "other";
    }
}
} // namespace

void ServicoPrometheus::iniciar(MonitorAmbiental& dispositivo)
{
    monitor = &dispositivo;
    Preferences prefs;
    if (prefs.begin("prometheus", true))
    {
        struct ConfiguracaoLegada
        {
            uint32_t versao;
            uint16_t portaMetricas;
            uint16_t portaServidor;
            char servidor[128];
        };

        const size_t tamanhoSalvo = prefs.getBytesLength("config");
        Configuracao salva{};
        if (tamanhoSalvo == sizeof(salva) &&
            prefs.getBytes("config", &salva, sizeof(salva)) == sizeof(salva) && salva.versao == 2 &&
            memchr(salva.servidor, '\0', sizeof(salva.servidor)) &&
            portaValida(salva.portaMetricas) && salva.intervalo >= 5 && salva.intervalo <= 3600 &&
            (!salva.servidor[0] || hostValido(salva.servidor)))
        {
            config = salva;
        }
        else if (tamanhoSalvo == sizeof(ConfiguracaoLegada))
        {
            ConfiguracaoLegada antiga{};
            if (prefs.getBytes("config", &antiga, sizeof(antiga)) == sizeof(antiga) &&
                antiga.versao == 1 && memchr(antiga.servidor, '\0', sizeof(antiga.servidor)) &&
                portaValida(antiga.portaMetricas) &&
                (!antiga.servidor[0] || hostValido(antiga.servidor)))
            {
                config.portaMetricas = antiga.portaMetricas;
                memcpy(config.servidor, antiga.servidor, strlen(antiga.servidor) + 1);
            }
        }
        prefs.end();
    }
    WiFi.onEvent(
        [this](WiFiEvent_t evento, WiFiEventInfo_t)
        {
            if (evento == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
                ++desconexoes;
        });
    abrirServidor();
}

void ServicoPrometheus::abrirServidor()
{
    if (servidor)
        servidor->stop();
    servidor.reset(new (std::nothrow) WebServer(config.portaMetricas));
    if (!servidor)
    {
        LOG_ERROR("[Prometheus] Memoria insuficiente para abrir servidor");
        return;
    }
    servidor->on("/metrics", HTTP_GET,
                 [this]()
                 {
                     servidor->sendHeader("Cache-Control", "no-store");
                     servidor->send(200, "text/plain; version=0.0.4; charset=utf-8", metricas());
                 });
    servidor->onNotFound([this]()
                         { servidor->send(404, "text/plain; charset=utf-8", "Use GET /metrics"); });
    servidor->begin();
    LOG_INFO(
        ("[Prometheus] Metricas na porta " + String(config.portaMetricas) + "/metrics").c_str());
}

void ServicoPrometheus::processar()
{
#if !configUSE_TRACE_FACILITY || !configGENERATE_RUN_TIME_STATS
    static bool avisoCpuEmitido = false;
    if (!avisoCpuEmitido)
    {
        LOG_WARN("[Prometheus] Uso de CPU indisponivel: SDK sem estatisticas de runtime.");
        avisoCpuEmitido = true;
    }
#endif
    atualizarUsoCpu();
    if (reconfigurar)
    {
        reconfigurar = false;
        abrirServidor();
    }
    if (servidor)
        servidor->handleClient();
}

void ServicoPrometheus::atualizarUsoCpu()
{
#if configUSE_TRACE_FACILITY && configGENERATE_RUN_TIME_STATS
    if (millis() - cpuAmostradoEmMs < 5000UL)
        return;
    cpuAmostradoEmMs = millis();

    if (uxTaskGetNumberOfTasks() > 64)
    {
        usoCpu = NAN;
        baselineCpuValida = false;
        LOG_WARN("[Prometheus] Uso de CPU indisponivel: mais de 64 tarefas.");
        return;
    }

    configRUN_TIME_COUNTER_TYPE tempoTotalRuntime = 0;
    const UBaseType_t quantidade = uxTaskGetSystemState(tarefas, 64, &tempoTotalRuntime);
    const int64_t agoraUs = esp_timer_get_time();
    uint64_t deltaOcioso = 0;
    bool encontrouTodos = quantidade != 0;

    for (BaseType_t nucleo = 0; nucleo < portNUM_PROCESSORS; ++nucleo)
    {
        const TaskHandle_t tarefaOciosa = xTaskGetIdleTaskHandleForCore(nucleo);
        bool encontrou = false;
        for (UBaseType_t i = 0; i < quantidade; ++i)
        {
            if (tarefas[i].xHandle == tarefaOciosa)
            {
                const configRUN_TIME_COUNTER_TYPE atual = tarefas[i].ulRunTimeCounter;
                deltaOcioso += static_cast<configRUN_TIME_COUNTER_TYPE>(
                    atual - tempoOciosoAnterior[nucleo]);
                tempoOciosoAnterior[nucleo] = atual;
                encontrou = true;
                break;
            }
        }
        encontrouTodos = encontrouTodos && encontrou;
    }

    const uint64_t capacidade =
        static_cast<uint64_t>(agoraUs - tempoAmostraAnteriorUs) * portNUM_PROCESSORS;
    usoCpu = baselineCpuValida && encontrouTodos && capacidade && deltaOcioso <= capacidade
                 ? 100.0f * (1.0f - static_cast<float>(deltaOcioso) / capacidade)
                 : NAN;
    tempoAmostraAnteriorUs = agoraUs;
    baselineCpuValida = encontrouTodos;
#else
    usoCpu = NAN;
#endif
}

void ServicoPrometheus::registrarHttp(uint32_t inicio, bool erro)
{
    ++requisicoes;
    if (erro)
        ++erros;
    ultimaDuracaoMs = static_cast<uint32_t>(micros() - inicio) / 1000.0;
    duracaoTotalMs += ultimaDuracaoMs;
}

void ServicoPrometheus::atender(const std::function<void()>& funcao)
{
    const uint32_t inicio = micros();
    erroHttp = false;
    funcao();
    registrarHttp(inicio, erroHttp);
}

void ServicoPrometheus::responder(WebServer& web, int codigo, const char* tipo, const String& corpo)
{
    if (codigo >= 400)
        erroHttp = true;
    web.send(codigo, tipo, corpo);
}

String ServicoPrometheus::metricas()
{
    String texto;
    texto.reserve(13000);
    const String device = labelSeguro(WiFi.macAddress());
    auto emitir = [&texto, &device](const char* nome, double valor, const char* ajuda,
                                    const char* tipo = "gauge", const String& labels = String())
    {
        texto += "# HELP " + String(nome) + " " + ajuda + "\n# TYPE " + nome + " " + tipo + "\n";
        texto += String(nome) + "{device=\"" + device + "\"";
        if (!labels.isEmpty())
            texto += "," + labels;
        texto += "}";
        texto += " " + (isfinite(valor) ? String(valor, 6) : String("NaN")) + "\n";
    };
    auto ausente = [&emitir](const char* nome)
    { emitir(nome, NAN, "Unavailable: hardware or integration not configured."); };

    const esp_app_desc_t* app = esp_app_get_description();
    char sha[65] = {};
    esp_ota_get_app_elf_sha256(sha, sizeof(sha));
    emitir("esp_info", 1, "Device identification.", "gauge",
           "chip=\"" + labelSeguro(ESP.getChipModel()) + "\",model=\"ESP32C3Mini\"");
    emitir("esp_firmware_version", 1, "Firmware build identification.", "gauge",
           "version=\"" + labelSeguro(app->version) + "\",build=\"" +
               labelSeguro(String(app->date) + " " + app->time) + "\",sha256=\"" + sha + "\"");
    emitir("esp_ip_address", 1, "Current interface IP address.", "gauge",
           "ip=\"" +
               (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString()
                                              : WiFi.softAPIP().toString()) +
               "\"");
    emitir("esp_mac_address", 1, "Station MAC address.", "gauge",
           "mac=\"" + labelSeguro(WiFi.macAddress()) + "\"");
    emitir("esp_status_online", WiFi.status() == WL_CONNECTED ? 1 : 0,
           "Station connected to Wi-Fi; use Prometheus up for scrape availability.");
    emitir("esp_uptime_seconds", esp_timer_get_time() / 1000000.0, "Seconds since boot.");
    emitir("esp_last_restart_reason", 1, "Last reset reason as label.", "gauge",
           "reason=\"" + String(motivoReset()) + "\"");
    const double boots =
        monitor->contadoresValidos() ? static_cast<double>(monitor->obterBoots()) : NAN;
    emitir("esp_boot_count", boots,
           "Persistent boots since counter initialization or reset_counters.", "counter");
    emitir("esp_reboot_count", isfinite(boots) ? (boots > 0 ? boots - 1 : 0) : NAN,
           "Boots after the first since reset_counters.", "counter");
    emitir("esp_watchdog_resets",
           monitor->contadoresValidos() ? static_cast<double>(monitor->obterWatchdogs()) : NAN,
           "Persistent resets explicitly identified as watchdog.", "counter");
    const bool conectado = WiFi.status() == WL_CONNECTED;
    emitir("esp_wifi_rssi_dbm", conectado ? WiFi.RSSI() : NAN, "Station RSSI in dBm.");
    emitir("esp_wifi_channel", conectado ? WiFi.channel() : NAN, "Station Wi-Fi channel.");
    emitir("esp_wifi_disconnect_count", desconexoes.load(),
           "Station disconnect events since exporter startup.", "counter");
    emitir("esp_wifi_tx_power", conectado ? static_cast<int>(WiFi.getTxPower()) / 4.0 : NAN,
           "Configured maximum Wi-Fi transmit power in dBm, not measured instantaneous power.");
    emitir("esp_cpu_frequency_mhz", ESP.getCpuFreqMHz(), "CPU frequency in MHz.");
    emitir("esp_cpu_usage_percent", usoCpu,
           "Aggregate CPU usage percentage from FreeRTOS runtime statistics.");
    emitir("esp_cpu_temperature_celsius", temperatureRead(),
           "Internal CPU temperature in degrees Celsius.");
    emitir("esp_task_count", uxTaskGetNumberOfTasks(), "Current FreeRTOS task count.");
    emitir("esp_free_rtos_tasks", uxTaskGetNumberOfTasks(),
           "Alias for current FreeRTOS task count; not unused task slots.");
    emitir("esp_heap_free_bytes", ESP.getFreeHeap(), "Free internal heap bytes.");
    emitir("esp_heap_total_bytes", ESP.getHeapSize(), "Total internal heap bytes.");
    emitir("esp_heap_minimum_free_bytes", ESP.getMinFreeHeap(),
           "Lowest free internal heap since boot.");
    emitir("esp_psram_free_bytes", ESP.getFreePsram(), "Free PSRAM bytes; zero when absent.");
    emitir("esp_psram_total_bytes", ESP.getPsramSize(), "Total PSRAM bytes; zero when absent.");
    const esp_partition_t* particao = esp_ota_get_running_partition();
    emitir("esp_flash_usage_percent",
           particao && particao->size ? 100.0 * ESP.getSketchSize() / particao->size : NAN,
           "Firmware image bytes divided by running application partition capacity, percent.");

    const LeituraAmbiental leitura = monitor->lerSensor();
    emitir("sensor_local_temperature_celsius", leitura.temperatura,
           "Local DHT22 temperature in Celsius.", "gauge", "location=\"local\",sensor=\"dht22\"");
    emitir("sensor_local_humidity_percent", leitura.umidade,
           "Local DHT22 relative humidity percent.", "gauge",
           "location=\"local\",sensor=\"dht22\"");
    emitir("sensor_temperature_celsius", monitor->obterTemperaturaPoa(),
           "Last available Porto Alegre temperature from Open-Meteo cache.", "gauge",
           "source=\"open_meteo\",location=\"porto_alegre\"");
    emitir("sensor_humidity_percent", monitor->obterUmidadePoa(),
           "Last available Porto Alegre relative humidity from Open-Meteo cache.", "gauge",
           "source=\"open_meteo\",location=\"porto_alegre\"");
    emitir("esp_weather_data_age_seconds", monitor->obterIdadeClimaSegundos(),
           "Seconds since last successful retrieval of Porto Alegre weather; NaN before first success.");
    emitir("esp_udp_listener_up", processador.udpListenerAtivo() ? 1 : 0,
           "UDP command listener bind succeeded (port 4210). Not a remote delivery probe.");
    emitir("esp_udp_packets_received_total", processador.udpPacotesRecebidos(),
           "Nonempty UDP datagrams reported by parsePacket since boot, including invalid commands.", "counter");
    emitir("esp_udp_replies_sent_total", processador.udpRespostasEnviadas(),
           "UDP replies completely written and accepted by endPacket since boot; not delivery acknowledgements.", "counter");
    emitir("esp_udp_send_errors_total", processador.udpErrosEnvio(),
           "UDP reply attempts with beginPacket, write or endPacket failure since boot.", "counter");
    emitir("mqtt_connected", homeAssistant.conectado() ? 1 : 0, "Home Assistant MQTT client connected to broker.");
    emitir("mqtt_messages_sent", homeAssistant.mensagensEnviadas(), "Successful MQTT publish calls since boot, including discovery and availability.", "counter");
    emitir("mqtt_messages_received", homeAssistant.mensagensRecebidas(), "MQTT messages delivered to callback since boot.", "counter");
    emitir("mqtt_publish_errors", homeAssistant.errosPublicacao(), "Failed MQTT publish calls since boot.", "counter");
    emitir("esp_lcd_on", monitor->telaLigada() ? 1 : 0, "LCD display and backlight enabled.");

    const char* indisponiveis[] = {"esp_voltage_v",
                                   "esp_current_ma",
                                   "esp_power_w",
                                   "esp_battery_percent",
                                   "esp_battery_voltage_v",
                                   "sensor_pressure_hpa",
                                   "sensor_air_quality",
                                   "relay_state",
                                   "relay_activation_count",
                                   "relay_runtime_seconds",
                                   "gpio_digital_input",
                                   "gpio_digital_output",
                                   "gpio_analog_value"};
    for (const char* nome : indisponiveis)
        ausente(nome);
    // Sem integração SD, não publique séries sdcard_* (nem zero nem NaN).
    // Os painéis devem receber ausência de dados; preserve apenas o indicador de suporte.
    texto += "# HELP esp_metric_supported Whether the integration is implemented (not hardware "
             "presence).\n# TYPE esp_metric_supported gauge\n";
    for (const char* grupo :
         {"energy", "pressure", "air_quality", "relays", "gpio", "sdcard"})
        texto +=
            "esp_metric_supported{device=\"" + device + "\",group=\"" + String(grupo) + "\"} 0\n";
    texto += "esp_metric_supported{device=\"" + device + "\",group=\"dht22\"} 1\n";
    texto += "esp_metric_supported{device=\"" + device + "\",group=\"mqtt\"} 1\n";
    texto += "esp_metric_supported{device=\"" + device + "\",group=\"cpu_temperature\"} 1\n";
#if configUSE_TRACE_FACILITY && configGENERATE_RUN_TIME_STATS
    texto += "esp_metric_supported{device=\"" + device + "\",group=\"cpu_usage\"} 1\n";
#else
    texto += "esp_metric_supported{device=\"" + device + "\",group=\"cpu_usage\"} 0\n";
#endif
    emitir("application_requests_total", requisicoes,
           "HTTP requests handled on dashboard port 80 since boot, excludes metrics listener.",
           "counter");
    emitir("application_errors_total", erros,
           "HTTP responses with status 400 or above on dashboard port 80 since boot.", "counter");
    emitir("application_response_time_ms", ultimaDuracaoMs,
           "Last completed HTTP handler duration on port 80, milliseconds; excludes socket receive "
           "wait.");
    emitir("application_response_time_ms_sum", duracaoTotalMs,
           "Sum of completed HTTP handler durations on port 80 in milliseconds.", "counter");
    emitir("application_response_time_ms_count", requisicoes, "Completed HTTP handlers on port 80.",
           "counter");
    return texto;
}

String ServicoPrometheus::configuracaoColeta() const
{
    String ip =
        WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    return "# Merge this job into scrape_configs on your Prometheus server.\n"
           "# Prometheus server reference: " +
           String(config.servidor) +
           "\n"
           "scrape_configs:\n  - job_name: 'esp32c3mini'\n    scrape_interval: " +
           String(config.intervalo) +
           "s\n"
           "    metrics_path: /metrics\n    static_configs:\n      - targets: ['" +
           ip + ":" + String(config.portaMetricas) + "']\n";
}

void ServicoPrometheus::configurarRotas(WebServer& web)
{
    web.on("/prometheus", HTTP_GET, [this, &web]() { atender([this, &web]() { pagina(web); }); });
    web.on("/prometheus/salvar", HTTP_POST,
           [this, &web]() { atender([this, &web]() { salvar(web); }); });
    web.on("/prometheus/config.yml", HTTP_GET,
           [this, &web]()
           {
               atender(
                   [this, &web]()
                   {
                       web.sendHeader("Cache-Control", "no-store");
                       responder(web, 200, "text/plain; charset=utf-8", configuracaoColeta());
                   });
           });
}

void ServicoPrometheus::pagina(WebServer& web)
{
    String html =
        R"HTML(<!doctype html><html lang="pt-BR"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Prometheus | Monitor Ambiental</title><style>
    :root{font-family:system-ui,sans-serif;color:#203d35;background:#f5f6f0;color-scheme:light}*{box-sizing:border-box}main{max-width:760px;margin:36px auto;padding:0 18px}a{color:#286649}nav{display:flex;gap:20px;font-size:14px}h1{font-size:34px;letter-spacing:-1px;margin-bottom:8px}p{line-height:1.6;color:#65796f}section,form{padding:24px;border:1px solid #dce5de;background:white;border-radius:16px;margin:20px 0}label{display:block;font-weight:600;font-size:14px;margin:16px 0 8px}input{width:100%;padding:12px;border:1px solid #bdcebf;border-radius:8px;font:inherit}button{padding:13px 20px;background:#286649;color:white;font:inherit;border:0;border-radius:8px;margin-top:20px;cursor:pointer}pre{overflow:auto;background:#edf1e9;padding:16px;border-radius:8px;font-size:12px}input:focus-visible,button:focus-visible,a:focus-visible{outline:3px solid #b17c28;outline-offset:3px}.aviso{background:#e4efdf;padding:14px;border-radius:8px}small{color:#65796f;line-height:1.5;display:block;margin-top:8px}</style></head><body><main><nav><a href="/">← Painel</a><a href="/config">Wi-Fi</a></nav><h1>Métricas no Prometheus</h1><p>Seu servidor coleta as métricas do monitor em uma porta exclusiva. O painel continua na porta 80.</p>)HTML";
    if (web.arg("salvo") == "1")
        html += "<p class='aviso' role='status'>Configuração salva. A porta de métricas será "
                "aplicada automaticamente.</p>";
    html += "<form method='post' action='/prometheus/salvar'><label for='host'>Endereço do "
            "servidor Prometheus (IPv4 ou DNS)</label><input id='host' name='host' maxlength='253' "
            "required placeholder='prometheus.local' value='" +
            htmlSeguro(config.servidor) +
            "'><small>Sem http://, caminho ou porta. É apenas uma referência; não configura o "
            "servidor nem restringe quem pode coletar.</small>";
    html += "<label for='port'>Porta de métricas no ESP32</label><input id='port' name='port' "
            "type='number' min='1' max='65535' required value='" +
            String(config.portaMetricas) +
            "'><small>Padrão 9120. As portas 80 e 3232 são reservadas ao painel e OTA.</small>";
    html += "<label for='interval'>Intervalo de coleta (segundos)</label><input id='interval' "
            "name='interval' type='number' min='5' max='3600' required value='" +
            String(config.intervalo) +
            "'><small>De 5 a 3600 segundos; o YAML gerado usa este valor.</small><button>Salvar "
            "configuração</button></form>";
    html +=
        "<section><h2>Configurar a coleta</h2><p>Inclua este destino no prometheus.yml do servidor "
        "e recarregue a configuração. Use um IP reservado para o monitor no roteador.</p><pre>" +
        htmlSeguro(configuracaoColeta()) +
        "</pre><a href='/prometheus/config.yml'>Abrir configuração de coleta</a>";
    String ip =
        WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    html += "<p><a href='http://" + ip + ":" + String(config.portaMetricas) +
            "/metrics' target='_blank' rel='noopener'>Ver métricas do monitor →</a></p>";
    html += "<p>Energia, pressão, qualidade do ar, relés e GPIO sem integração retornam NaN. "
            "Métricas de SD e logs do cartão não são exportadas, para os painéis indicarem No data. "
            "O uso de CPU depende das estatísticas disponíveis no firmware.</p></section></main></body></html>";
    web.sendHeader("Cache-Control", "no-store");
    responder(web, 200, "text/html; charset=utf-8", html);
}

void ServicoPrometheus::salvar(WebServer& web)
{
    Configuracao nova{};
    nova.versao = 2;
    String host = web.arg("host");
    host.trim();
    if (!web.hasArg("host") || !hostValido(host) ||
        !lerPorta(web.arg("port"), nova.portaMetricas) || !portaValida(nova.portaMetricas) ||
        !lerPorta(web.arg("interval"), nova.intervalo) || nova.intervalo < 5 ||
        nova.intervalo > 3600)
    {
        responder(web, 400, "text/plain; charset=utf-8",
                  "Use IPv4/DNS sem URL, porta de métricas 1 a 65535 (exceto 80 e 3232) e "
                  "intervalo de 5 a 3600 segundos.");
        return;
    }
    host.toCharArray(nova.servidor, sizeof(nova.servidor));
    Preferences prefs;
    bool salvo = prefs.begin("prometheus", false);
    if (salvo)
    {
        Configuracao verificacao{};
        salvo =
            prefs.putBytes("config", &nova, sizeof(nova)) == sizeof(nova) &&
            prefs.getBytes("config", &verificacao, sizeof(verificacao)) == sizeof(verificacao) &&
            memcmp(&nova, &verificacao, sizeof(nova)) == 0;
        prefs.end();
    }
    if (!salvo)
    {
        responder(web, 500, "text/plain; charset=utf-8",
                  "Falha ao salvar nas Preferences; a configuração ativa foi mantida.");
        return;
    }
    reconfigurar = config.portaMetricas != nova.portaMetricas;
    config = nova;
    web.sendHeader("Location", "/prometheus?salvo=1");
    responder(web, 303, "text/plain; charset=utf-8", "Configuração salva.");
}
