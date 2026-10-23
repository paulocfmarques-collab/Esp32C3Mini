#include "IntegracaoHomeAssistant.h"
#include "MonitorAmbiental.h"
#include "ServicoPrometheus.h"
#include "Log.h"
#include <Preferences.h>
#include <esp_app_desc.h>
#include <esp_timer.h>
#include <math.h>
#include <string.h>

extern ServicoPrometheus prometheus;

namespace
{
String jsonTexto(const String& texto)
{
    String saida = "\"";
    for (size_t i = 0; i < texto.length(); ++i)
    {
        const uint8_t c = texto[i];
        if (c == '"' || c == '\\') { saida += '\\'; saida += char(c); }
        else if (c < 32)
        {
            char escape[7];
            snprintf(escape, sizeof(escape), "\\u%04x", c);
            saida += escape;
        }
        else saida += char(c);
    }
    return saida + "\"";
}
String htmlTexto(String texto)
{
    texto.replace("&", "&amp;"); texto.replace("'", "&#39;");
    texto.replace("\"", "&quot;"); texto.replace("<", "&lt;"); texto.replace(">", "&gt;");
    return texto;
}
String numero(double valor) { return isfinite(valor) ? String(valor, 2) : String("null"); }
bool hostValido(const String& host)
{
    if (host.isEmpty() || host.length() > 127) return false;
    for (size_t i = 0; i < host.length(); ++i)
    {
        char c = host[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
    }
    return true;
}
bool inteiro(const String& texto, uint16_t& resultado, uint32_t minimo, uint32_t maximo)
{
    if (texto.isEmpty() || texto.length() > 5) return false;
    uint32_t n = 0;
    for (size_t i = 0; i < texto.length(); ++i)
    {
        if (texto[i] < '0' || texto[i] > '9') return false;
        n = n * 10 + texto[i] - '0';
    }
    if (n < minimo || n > maximo) return false;
    resultado = static_cast<uint16_t>(n);
    return true;
}
struct SensorDescoberta { const char* chave; const char* nome; const char* unidade; const char* classe; };
const SensorDescoberta sensores[] = {
    {"local_temperature", "Temperatura local", "°C", "temperature"},
    {"local_humidity", "Umidade local", "%", "humidity"},
    {"poa_temperature", "Temperatura Porto Alegre", "°C", "temperature"},
    {"poa_humidity", "Umidade Porto Alegre", "%", "humidity"},
    {"weather_age", "Idade dos dados de Porto Alegre", "s", "duration"},
    {"uptime", "Tempo ligado", "s", "duration"},
    {"wifi_rssi", "Sinal Wi-Fi", "dBm", "signal_strength"}
};
}

IntegracaoHomeAssistant::IntegracaoHomeAssistant() : mqtt(tcp) {}

void IntegracaoHomeAssistant::iniciar(MonitorAmbiental& dispositivo)
{
    monitor = &dispositivo;
    id = "esp32c3mini_" + WiFi.macAddress();
    id.replace(":", ""); id.toLowerCase();
    base = "monitorambiental/" + id;
    Preferences prefs;
    if (prefs.begin("homeassistant", true))
    {
        Configuracao salva{};
        if (prefs.getBytesLength("config") == sizeof(salva) &&
            prefs.getBytes("config", &salva, sizeof(salva)) == sizeof(salva) &&
            salva.versao == 1 && salva.ativo <= 1 && salva.porta > 0 &&
            salva.intervalo >= 10 && salva.intervalo <= 3600 &&
            memchr(salva.host, 0, sizeof(salva.host)) && memchr(salva.usuario, 0, sizeof(salva.usuario)) &&
            memchr(salva.senha, 0, sizeof(salva.senha)) &&
            (!salva.ativo || hostValido(salva.host))) config = salva;
        prefs.end();
    }
    bufferPronto = mqtt.setBufferSize(1536);
    if (!bufferPronto) LOG_ERROR("[HA] Memoria insuficiente para buffer MQTT");
    mqtt.setKeepAlive(30);
    mqtt.setSocketTimeout(2);
    mqtt.setServer(config.host, config.porta);
    mqtt.setCallback([this](char* topico, uint8_t* dados, unsigned int tamanho) { receber(topico, dados, tamanho); });
}

bool IntegracaoHomeAssistant::publicar(const String& topico, const String& payload, bool reter)
{
    const bool ok = mqtt.publish(topico.c_str(), payload.c_str(), reter);
    if (ok) ++enviadas; else ++erros;
    return ok;
}

String IntegracaoHomeAssistant::dispositivoJson() const
{
    return "{\"identifiers\":[" + jsonTexto(id) + "],\"name\":\"Monitor Ambiental Mini\","
           "\"model\":\"ESP32C3Mini\",\"manufacturer\":\"Monitor Ambiental\",\"sw_version\":" +
           jsonTexto(esp_app_get_description()->version) + "}";
}

bool IntegracaoHomeAssistant::publicarDescoberta(bool remover)
{
    bool sucesso = true;
    const String comum = ",\"availability_topic\":" + jsonTexto(base + "/availability") +
                         ",\"device\":" + dispositivoJson();
    const unsigned int expira = config.intervalo * 3U < 90 ? 90 : config.intervalo * 3U;
    for (const SensorDescoberta& sensor : sensores)
    {
        String payload = "{\"name\":" + jsonTexto(sensor.nome) + ",\"unique_id\":" + jsonTexto(id + "_" + sensor.chave) +
            ",\"state_topic\":" + jsonTexto(base + "/state") + ",\"value_template\":" +
            jsonTexto("{{ value_json." + String(sensor.chave) + " }}") +
            ",\"unit_of_measurement\":" + jsonTexto(sensor.unidade) + ",\"device_class\":" + jsonTexto(sensor.classe) +
            ",\"state_class\":\"measurement\",\"expire_after\":" + String(expira) + comum + "}";
        sucesso = publicar("homeassistant/sensor/" + id + "/" + sensor.chave + "/config", remover ? "" : payload, true) && sucesso;
    }
    const String tela = "{\"name\":\"Tela\",\"unique_id\":" + jsonTexto(id + "_lcd") +
        ",\"command_topic\":" + jsonTexto(base + "/lcd/set") + ",\"state_topic\":" + jsonTexto(base + "/lcd/state") +
        ",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"retain\":false" + comum + "}";
    sucesso = publicar("homeassistant/switch/" + id + "/lcd/config", remover ? "" : tela, true) && sucesso;
    const String atualizar = "{\"name\":\"Atualizar clima de Porto Alegre\",\"unique_id\":" + jsonTexto(id + "_weather_refresh") +
        ",\"command_topic\":" + jsonTexto(base + "/weather/refresh") + ",\"payload_press\":\"PRESS\"" + comum + "}";
    sucesso = publicar("homeassistant/button/" + id + "/weather_refresh/config", remover ? "" : atualizar, true) && sucesso;
    return sucesso;
}

bool IntegracaoHomeAssistant::publicarEstado()
{
    const LeituraAmbiental leitura = monitor->lerSensor();
    const String estado = "{\"local_temperature\":" + numero(leitura.temperatura) +
        ",\"local_humidity\":" + numero(leitura.umidade) +
        ",\"poa_temperature\":" + numero(monitor->obterTemperaturaPoa()) +
        ",\"poa_humidity\":" + numero(monitor->obterUmidadePoa()) +
        ",\"weather_age\":" + numero(monitor->obterIdadeClimaSegundos()) +
        ",\"uptime\":" + numero(esp_timer_get_time() / 1000000.0) +
        ",\"wifi_rssi\":" + numero(WiFi.RSSI()) + "}";
    bool sucesso = publicar(base + "/state", estado, false);
    sucesso = publicar(base + "/lcd/state", monitor->telaLigada() ? "ON" : "OFF", true) && sucesso;
    return sucesso;
}

void IntegracaoHomeAssistant::receber(char* topico, uint8_t* payload, unsigned int tamanho)
{
    ++recebidas;
    if (tamanho > 16) return;
    String texto;
    for (unsigned int i = 0; i < tamanho; ++i) texto += char(payload[i]);
    if (String(topico) == "homeassistant/status" && texto == "online") redescobrir();
    else if (String(topico) == base + "/lcd/set")
    {
        if (texto == "ON") comandoTela = 1;
        else if (texto == "OFF") comandoTela = 0;
    }
    else if (String(topico) == base + "/weather/refresh" && texto == "PRESS") atualizarClima = true;
}

void IntegracaoHomeAssistant::processar()
{
    if (!monitor || !bufferPronto) return;
    if (reconfigurar)
    {
        if (mqtt.connected())
        {
            publicar(base + "/availability", "offline", true);
            publicarDescoberta(true);
            mqtt.disconnect();
        }
        tcp.stop();
        config = proxima;
        mqtt.setServer(config.host, config.porta);
        reconfigurar = false;
        primeiraTentativa = true;
        comandoTela = -1;
        atualizarClima = false;
    }
    if (!config.ativo || WiFi.status() != WL_CONNECTED)
    {
        if (mqtt.connected())
        {
            publicar(base + "/availability", "offline", true);
            mqtt.disconnect();
        }
        tcp.stop();
        return;
    }
    const uint32_t agora = millis();
    if (!mqtt.connected())
    {
        if (!primeiraTentativa && agora - ultimaTentativa < 10000) return;
        primeiraTentativa = false;
        ultimaTentativa = agora;
        const String disponibilidade = base + "/availability";
        if (!mqtt.connect(id.c_str(), config.usuario[0] ? config.usuario : nullptr,
                          config.usuario[0] ? config.senha : nullptr,
                          disponibilidade.c_str(), 1, true, "offline"))
        {
            LOG_WARN(("[HA] MQTT indisponivel, codigo " + String(mqtt.state())).c_str());
            return;
        }
        publicar(base + "/availability", "offline", true);
        bool inscrito = mqtt.subscribe((base + "/lcd/set").c_str(), 1);
        inscrito = mqtt.subscribe((base + "/weather/refresh").c_str(), 1) && inscrito;
        inscrito = mqtt.subscribe("homeassistant/status", 1) && inscrito;
        if (!inscrito)
        {
            publicar(base + "/availability", "offline", true);
            mqtt.disconnect();
            return;
        }
        descobertaPendente = true;
        ultimaDescoberta = agora - 5000;
        publicarAgora = true;
        LOG_INFO("[HA] MQTT conectado");
    }
    mqtt.loop();
    if (!mqtt.connected()) return;
    if (comandoTela >= 0)
    {
        monitor->definirTelaLigada(comandoTela == 1);
        comandoTela = -1;
        publicarAgora = true;
    }
    if (atualizarClima)
    {
        atualizarClima = false;
        monitor->forcarSincronizacaoClima();
        publicarAgora = true;
    }
    if (descobertaPendente && agora - ultimaDescoberta >= 5000)
    {
        ultimaDescoberta = agora;
        descobertaPendente = !publicarDescoberta();
        publicarAgora = true;
    }
    if (publicarAgora || agora - ultimaPublicacao >= uint32_t(config.intervalo) * 1000)
    {
        ultimaPublicacao = agora;
        publicarAgora = false;
        if (publicarEstado() && !descobertaPendente) publicar(base + "/availability", "online", true);
    }
}

String IntegracaoHomeAssistant::status()
{
    if (!bufferPronto) return "Indisponível: falha ao alocar buffer MQTT";
    if (!config.ativo) return "Desativada";
    if (WiFi.status() != WL_CONNECTED) return "Aguardando Wi-Fi";
    if (mqtt.connected()) return descobertaPendente ? "Conectada, publicando descoberta" : "Conectada ao broker";
    return "Aguardando broker (código " + String(mqtt.state()) + ")";
}

void IntegracaoHomeAssistant::configurarRotas(WebServer& web)
{
    web.on("/homeassistant", HTTP_GET, [this, &web]() { prometheus.atender([this, &web]() { pagina(web); }); });
    web.on("/homeassistant/salvar", HTTP_POST, [this, &web]() { prometheus.atender([this, &web]() { salvar(web); }); });
}

void IntegracaoHomeAssistant::pagina(WebServer& web)
{
    String html = R"HTML(<!doctype html><html lang="pt-BR"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Home Assistant | Monitor Ambiental</title><style>
    :root{font-family:system-ui,sans-serif;color:#203d35;background:#f5f6f0;color-scheme:light}*{box-sizing:border-box}main{max-width:740px;margin:32px auto;padding:0 18px}a{color:#286649}h1{font-size:32px;letter-spacing:-1px}p,small{color:#65796f;line-height:1.6}form,section{padding:24px;border:1px solid #dce5de;background:white;border-radius:16px;margin:20px 0}label{display:block;font-size:14px;font-weight:600;margin:16px 0 8px}input:not([type=checkbox]){width:100%;font:inherit;padding:12px;border:1px solid #bdcebf;border-radius:8px}button{font:inherit;padding:13px 20px;background:#286649;color:white;border:0;border-radius:8px;cursor:pointer;margin-top:20px}small{display:block;margin-top:8px}.check{display:flex;align-items:center;gap:9px}input[type=checkbox]{width:18px;height:18px;accent-color:#286649}.aviso{padding:14px;background:#e4efdf;border-radius:10px}input:focus-visible,button:focus-visible,a:focus-visible{outline:3px solid #b17c28;outline-offset:3px}code{overflow-wrap:anywhere}</style></head><body><main><a href="/">← Voltar ao painel</a><h1>Home Assistant</h1><p>Configure o broker MQTT usado pelo seu Home Assistant. Os sensores e o controle da tela serão descobertos automaticamente.</p>)HTML";
    if (web.arg("salvo") == "1") html += "<p class='aviso' role='status'>Configuração salva. A conexão será atualizada automaticamente.</p>";
    html += "<section><strong>Estado: " + htmlTexto(status()) + "</strong><p>Identificador: <code>" + htmlTexto(id) + "</code></p><p>Publicadas: " + String(enviadas) + " · Recebidas: " + String(recebidas) + " · Falhas de publicação: " + String(erros) + "</p><small>Atualize a página para consultar o estado atual.</small></section>";
    html += "<form action='/homeassistant/salvar' method='post'><label class='check'><input name='ativo' type='checkbox' value='1'" + String(config.ativo ? " checked" : "") + ">Ativar integração MQTT</label>";
    html += "<label for='host'>Endereço do broker</label><input id='host' name='host' maxlength='127' placeholder='192.168.0.10 ou mqtt.local' value='" + htmlTexto(config.host) + "'><small>IP ou nome, sem mqtt://, http:// ou caminho.</small>";
    html += "<label for='porta'>Porta MQTT</label><input id='porta' name='porta' type='number' min='1' max='65535' required value='" + String(config.porta) + "'><small>Normalmente 1883. A interface web continua na porta 80.</small>";
    html += "<label for='usuario'>Usuário (opcional)</label><input id='usuario' name='usuario' maxlength='63' autocomplete='username' value='" + htmlTexto(config.usuario) + "'>";
    html += "<label for='senha'>Senha</label><input id='senha' name='senha' type='password' maxlength='127' autocomplete='new-password' placeholder='Vazia mantém a senha salva'><label class='check'><input type='checkbox' name='limpar_senha' value='1'>Apagar senha salva</label>";
    html += "<label for='intervalo'>Publicar a cada (segundos)</label><input id='intervalo' name='intervalo' type='number' min='10' max='3600' required value='" + String(config.intervalo) + "'><button>Salvar configuração</button></form>";
    html += "<section><h2>No Home Assistant</h2><p>Configure a integração MQTT com este mesmo broker e mantenha a descoberta ativa com prefixo <code>homeassistant</code>. Procure o dispositivo <strong>Monitor Ambiental Mini</strong>.</p><p>Inclui temperatura e umidade locais, temperatura e umidade de Porto Alegre, idade dos dados externos, uptime, sinal Wi-Fi, interruptor da tela e botão para atualizar o clima.</p><small>Conexão MQTT sem TLS, destinada à rede local. As credenciais são salvas nas Preferences e a senha não é exibida nesta página. Para remover as entidades, desative enquanto o broker estiver conectado; se estiver inacessível, remova os tópicos de descoberta antigos no broker.</small></section></main></body></html>";
    web.sendHeader("Cache-Control", "no-store");
    prometheus.responder(web, 200, "text/html; charset=utf-8", html);
}

void IntegracaoHomeAssistant::salvar(WebServer& web)
{
    Configuracao nova{};
    nova.versao = 1;
    nova.ativo = web.arg("ativo") == "1";
    String host = web.arg("host"); host.trim();
    const String usuario = web.arg("usuario");
    String senha = web.arg("senha");
    if (senha.isEmpty()) senha = config.senha;
    if (web.arg("limpar_senha") == "1") senha = "";
    if (!web.hasArg("host") || !web.hasArg("usuario") || !web.hasArg("senha") ||
        (!host.isEmpty() && !hostValido(host)) || (nova.ativo && host.isEmpty()) ||
        !inteiro(web.arg("porta"), nova.porta, 1, 65535) ||
        !inteiro(web.arg("intervalo"), nova.intervalo, 10, 3600) ||
        usuario.length() > 63 || senha.length() > 127 ||
        strlen(usuario.c_str()) != usuario.length() || strlen(senha.c_str()) != senha.length())
    {
        prometheus.responder(web, 400, "text/plain; charset=utf-8", "Configuração inválida. Confira endereço, porta, intervalo de 10 a 3600 segundos e tamanho das credenciais.");
        return;
    }
    host.toCharArray(nova.host, sizeof(nova.host));
    usuario.toCharArray(nova.usuario, sizeof(nova.usuario));
    senha.toCharArray(nova.senha, sizeof(nova.senha));
    Preferences prefs;
    bool salvo = prefs.begin("homeassistant", false);
    if (salvo)
    {
        Configuracao lida{};
        salvo = prefs.putBytes("config", &nova, sizeof(nova)) == sizeof(nova) &&
            prefs.getBytes("config", &lida, sizeof(lida)) == sizeof(lida) && memcmp(&nova, &lida, sizeof(nova)) == 0;
        prefs.end();
    }
    if (!salvo)
    {
        prometheus.responder(web, 500, "text/plain; charset=utf-8", "Não foi possível gravar a configuração MQTT. A conexão atual foi mantida.");
        return;
    }
    proxima = nova;
    reconfigurar = true;
    web.sendHeader("Location", "/homeassistant?salvo=1");
    prometheus.responder(web, 303, "text/plain; charset=utf-8", "Configuração salva.");
}
