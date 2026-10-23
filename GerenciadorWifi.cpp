#include "GerenciadorWifi.h"
#include "Log.h"
#include "MonitorAmbiental.h"
#include "ProcessadorComandos.h"
#include "ServicoPrometheus.h"
#include "IntegracaoHomeAssistant.h"

extern MonitorAmbiental monitor;
extern ProcessadorComandos processador;
extern ServicoPrometheus prometheus;
extern IntegracaoHomeAssistant homeAssistant;

namespace
{
const int MAX_HISTORICO = 20;
float histTemperaturaLocal[MAX_HISTORICO] = {0};
float histTemperaturaPoa[MAX_HISTORICO] = {0}; // Novo array de histórico externo
String histHora[MAX_HISTORICO] = {""};
int totalRegistros = 0;

void adicionarAoHistorico(float tLocal, float tPoa, String hora)
{
    if (totalRegistros < MAX_HISTORICO)
    {
        histTemperaturaLocal[totalRegistros] = tLocal;
        histTemperaturaPoa[totalRegistros] = tPoa;
        histHora[totalRegistros] = hora;
        totalRegistros++;
    }
    else
    {
        for (int i = 0; i < MAX_HISTORICO - 1; i++)
        {
            histTemperaturaLocal[i] = histTemperaturaLocal[i + 1];
            histTemperaturaPoa[i] = histTemperaturaPoa[i + 1];
            histHora[i] = histHora[i + 1];
        }
        histTemperaturaLocal[MAX_HISTORICO - 1] = tLocal;
        histTemperaturaPoa[MAX_HISTORICO - 1] = tPoa;
        histHora[MAX_HISTORICO - 1] = hora;
    }
}
} // namespace

namespace
{
class RespostaWeb : public Print
{
  public:
    String texto;

    size_t write(uint8_t byte) override
    {
        return texto.concat(static_cast<char>(byte)) ? 1 : 0;
    }
};

String textoJson(String texto)
{
    texto.replace("\\", "\\\\");
    texto.replace("\"", "\\\"");
    String resultado = "\"";
    for (size_t i = 0; i < texto.length(); ++i)
    {
        const unsigned char c = texto[i];
        if (c < 32)
        {
            char escape[7];
            snprintf(escape, sizeof(escape), "\\u%04x", c);
            resultado += escape;
        }
        else
            resultado += static_cast<char>(c);
    }
    return resultado + "\"";
}
} // namespace

GerenciadorWifi::GerenciadorWifi() : servidor(80), portalAtivo(false)
{
}

void GerenciadorWifi::carregarRedes()
{
    prefs.begin("wifi", true);

    for (int i = 0; i < 5; i++)
    {
        redes[i].ssid = prefs.getString(("ssid" + String(i)).c_str(), "");

        redes[i].senha = prefs.getString(("senha" + String(i)).c_str(), "");
    }

    prefs.end();
}

bool GerenciadorWifi::salvarRedes()
{
    if (!prefs.begin("wifi", false))
    {
        return false;
    }
    bool salvo = true;

    for (int i = 0; i < 5; i++)
    {
        prefs.putString(("ssid" + String(i)).c_str(), redes[i].ssid);

        prefs.putString(("senha" + String(i)).c_str(), redes[i].senha);
        salvo = (prefs.getString(("ssid" + String(i)).c_str()) == redes[i].ssid &&
                 prefs.getString(("senha" + String(i)).c_str()) == redes[i].senha) &&
                salvo;
    }

    prefs.end();
    return salvo;
}

bool GerenciadorWifi::conectar()
{
    LOG_INFO("[WiFi] Carregando redes salvas");
    carregarRedes();
    LOG_INFO("[WiFi] Redes carregadas");

    for (int i = 0; i < 5; i++)
    {
        if (redes[i].ssid.isEmpty())
        {
            continue;
        }

        LOG_INFO(("Tentando rede: " + redes[i].ssid).c_str());

        LOG_INFO("[WiFi] Ativando modo estacao");
        WiFi.mode(WIFI_STA);

        LOG_INFO("[WiFi] Iniciando conexao");
        WiFi.begin(redes[i].ssid.c_str(), redes[i].senha.c_str());
        LOG_INFO("[WiFi] Aguardando conexao");

        unsigned long inicio = millis();

        while (WiFi.status() != WL_CONNECTED && millis() - inicio < 10000)
        {
            delay(10);
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            LOG_INFO("[WiFi] Conectado");
            LOG_INFO(("[WiFi] Endereco IP: " + WiFi.localIP().toString()).c_str());
            LOG_INFO(("[WiFi] SSID: " + redes[i].ssid).c_str());

            relogio.iniciar();
            LOG_INFO("[WiFi] Inicializacao concluida");

            return true;
        }
        LOG_WARN("[WiFi] Tempo de conexao esgotado para esta rede");
    }

    return false;
}

void GerenciadorWifi::paginaPrincipal()
{
    String html;
    html.reserve(9500);

    html += "<!doctype html><html lang='pt-BR'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>Configurar Wi-Fi - Monitor Ambiental</title>";
    html += R"HTML(<style>
    :root { color-scheme: dark light; font-family: system-ui, -apple-system, sans-serif; background: #0f172a; color: #f8fafc; }
    body { max-width: 650px; margin: auto; padding: 80px 16px 24px 16px; }
    *{ box-sizing: border-box; }
    
    /* Menu de Navegação Superior */
    nav { position: fixed; top: 0; left: 0; right: 0; height: 60px; background: rgba(30, 41, 59, 0.7); backdrop-filter: blur(12px); border-bottom: 1px solid #334155; display: flex; align-items: center; justify-content: space-between; padding: 0 24px; z-index: 1000; box-shadow: 0 4px 30px rgba(0, 0, 0, 0.1); }
    nav .logo { font-weight: 700; font-size: 1.1rem; color: #38bdf8; display: flex; align-items: center; gap: 8px; }
    nav .links a { color: #94a3b8; text-decoration: none; font-weight: 600; font-size: 0.95rem; padding: 8px 14px; border-radius: 8px; transition: 0.2s; }
    nav .links a:hover, nav .links a.active { color: #fff; background: #334155; }

    /* Componentes Gerais e Alertas */
    .card { background: #1e293b; padding: 24px; border-radius: 16px; margin-bottom: 16px; border: 1px solid #334155; }
    h1 { font-size: 1.75rem; margin: 0 0 8px 0; letter-spacing: -0.5px; }
    p { color: #94a3b8; line-height: 1.6; margin: 0 0 16px 0; font-size: 0.95rem; }
    .aviso { padding: 16px; background: rgba(56, 189, 248, 0.1); border: 1px solid rgba(56, 189, 248, 0.2); border-radius: 12px; margin-bottom: 20px; color: #38bdf8; font-size: 0.9rem; }
    .aviso strong { color: #fff; display: block; margin-bottom: 4px; }
    .sucesso { padding: 16px; background: rgba(16, 185, 129, 0.15); border: 1px solid #10b981; border-radius: 12px; margin-bottom: 20px; color: #10b981; }

    /* Formulários e Slots */
    .redes { display: grid; grid-template-columns: 1fr; gap: 16px; }
    fieldset { border: 1px solid #334155; border-radius: 12px; padding: 20px; background: #0f172a; margin: 0; }
    legend { font-weight: 700; font-size: 0.9rem; letter-spacing: 0.05em; text-transform: uppercase; color: #38bdf8; padding: 0 8px; }
    label { display: block; font-size: 0.85rem; font-weight: 600; margin: 12px 0 6px 0; color: #94a3b8; }
    input:not([type=checkbox]) { width: 100%; min-height: 44px; border: 1px solid #334155; background: #1e293b; border-radius: 8px; padding: 10px 14px; font: inherit; color: #fff; transition: border-color 0.2s; }
    input:not([type=checkbox]):focus { outline: none; border-color: #38bdf8; }
    
    /* Checkbox Customizado */
    .mostrar { display: flex; align-items: center; gap: 8px; font-weight: 400; color: #94a3b8; margin-top: 12px; cursor: pointer; font-size: 0.85rem; user-select: none; }
    input[type=checkbox] { width: 16px; height: 16px; accent-color: #38bdf8; cursor: pointer; }
    
    /* Ações */
    .acoes { margin-top: 20px; display: flex; flex-direction: column; gap: 12px; }
    button { background: #38bdf8; color: #0f172a; border: 0; border-radius: 10px; padding: 14px 22px; font: inherit; font-weight: 700; cursor: pointer; transition: background 0.2s; width: 100%; font-size: 1rem; }
    button:hover { background: #0ea5e9; }
    footer { text-align: center; font-size: 0.8rem; color: #64748b; margin-top: 24px; padding: 12px; }
    @media(max-width: 560px){ body { padding-top: 74px; } }
    </style>)HTML";
    html += "</head><body>";

    // Barra de Navegação Superior Integrada
    html += "<nav><div class='logo'>🍃 Monitor Ambiental</div><div class='links'>";
    html += "<a href='/'>Dashboard</a>";
    html += "<a href='/config' class='active'>Configurar Wi-Fi</a>";
    html += "</div></nav>";

    html += "<main><div class='card'>";
    html += "<h1>Gerenciador de Conexões</h1>";
    html += "<p>Cadastre até 5 redes Wi-Fi conhecidas. O dispositivo tentará a conexão obedecendo "
            "a ordem de prioridade dos slots.</p>";

    if (servidor.arg("salvo") == "1")
    {
        html += "<div class='sucesso' role='status'><strong>✓ Configurações salvas!</strong><p "
                "style='color:#10b981;margin:4px 0 0 0;font-size:0.875rem;'>Reinicie o dispositivo "
                "para aplicar as novas redes.</p></div>";
    }

    html += "<div class='aviso'><strong>Requisitos de Rede</strong>O módulo opera exclusivamente "
            "em redes sem fio de 2,4 GHz. Deixe os campos em branco nos slots que deseja desativar "
            "ou apagar.</div>";
    html += "</div>";

    html += "<form action='/salvar' method='post' accept-charset='UTF-8'><div class='redes'>";

    for (int i = 0; i < 5; i++)
    {
        html += "<fieldset><legend>Slot de Rede " + String(i + 1) + "</legend>";

        html += "<label for='ssid" + String(i) + "'>Nome da Rede (SSID)</label>";
        html += "<input maxlength='32' autocomplete='off' autocapitalize='none' spellcheck='false' "
                "id='ssid" +
                String(i) + "' name='ssid" + String(i) + "' value='";

        String ssidSeguro = redes[i].ssid;
        ssidSeguro.replace("&", "&amp;");
        ssidSeguro.replace("'", "&#39;");
        ssidSeguro.replace("\"", "&quot;");
        ssidSeguro.replace("<", "&lt;");
        ssidSeguro.replace(">", "&gt;");
        html += ssidSeguro;
        html += "' placeholder='Ex: MinhaRede_2G'>";

        html += "<label for='senha" + String(i) + "'>Senha de Acesso</label>";
        html += "<input type='password' maxlength='64' autocomplete='new-password' id='senha" +
                String(i) + "' name='senha" + String(i) + "' placeholder='";
        html += redes[i].senha.isEmpty() ? "Deixe em branco para rede aberta"
                                         : "Preencha para alterar a senha salva";
        html += "'>";

        html += "<label class='mostrar'><input type='checkbox' data-senha='senha" + String(i) +
                "'>Exibir caracteres digitados</label></fieldset>";
    }

    html += "</div><div class='card acoes'>";
    html += "<button type='submit'>Gravar Alterações permanentemente</button>";
    html += "<p style='margin:4px 0 0 0; text-align:center; font-size:0.85rem;'>Após o salvamento "
            "com sucesso, efetue o Reboot através do Dashboard.</p></div>";
    html += "</form>";

    html += "<footer>Conexão direta local via HTTP &bull; Porta 80</footer></main>";

    html += R"HTML(<script>
    document.querySelectorAll('[data-senha]').forEach(function(c){
        c.addEventListener('change', function(){
            document.getElementById(c.dataset.senha).type = c.checked ? 'text' : 'password';
        });
    });
    </script></body></html>)HTML";

    servidor.sendHeader("Cache-Control", "no-store");
    prometheus.responder(servidor, 200, "text/html; charset=utf-8", html);
}

void GerenciadorWifi::salvarConfiguracao()
{
    // Verifique o formulário inteiro antes de alterar qualquer rede.
    for (int i = 0; i < 5; i++)
    {
        String indice = String(i);
        if (!servidor.hasArg("ssid" + indice) || !servidor.hasArg("senha" + indice) ||
            servidor.arg("ssid" + indice).length() > 32 ||
            servidor.arg("senha" + indice).length() > 64)
        {
            prometheus.responder(servidor, 400, "text/plain; charset=utf-8",
                          "Formulário inválido. Volte e confira os campos (SSID: até 32 bytes; "
                          "senha: até 64 bytes).");
            return;
        }
    }

    for (int i = 0; i < 5; i++)
    {
        String ssid = servidor.arg("ssid" + String(i));
        String senha = servidor.arg("senha" + String(i));
        if (ssid.isEmpty() || ssid != redes[i].ssid || !senha.isEmpty())
        {
            redes[i].senha = ssid.isEmpty() ? "" : senha;
        }
        redes[i].ssid = ssid;
    }

    if (!salvarRedes())
    {
        prometheus.responder(servidor, 500, "text/plain; charset=utf-8",
                      "Não foi possível gravar as configurações. Volte e tente salvar novamente "
                      "antes de reiniciar.");
        return;
    }

    servidor.sendHeader("Location", "/?salvo=1");
    prometheus.responder(servidor, 303, "text/plain; charset=utf-8", "Configuração salva. Reinicie o monitor.");
}

void GerenciadorWifi::configurarRotas()
{
    // O Dashboard agora é a página de entrada principal
    servidor.on("/", HTTP_GET, [this]() { prometheus.atender([this]() { paginaDashboard(); }); });

    // Nova rota para a configuração de Wi-Fi repaginada
    servidor.on("/config", HTTP_GET, [this]() { prometheus.atender([this]() { paginaPrincipal(); }); });
    servidor.on("/salvar", HTTP_POST, [this]() { prometheus.atender([this]() { salvarConfiguracao(); }); });

    // Rotas de API e execução de comandos
    servidor.on("/api/dados", HTTP_GET, [this]() { prometheus.atender([this]() { apiDados(); }); });
    servidor.on("/api/comando", HTTP_GET, [this]() { prometheus.atender([this]() { executarComandoWeb(); }); });
    prometheus.configurarRotas(servidor);
    homeAssistant.configurarRotas(servidor);
    servidor.onNotFound([this]()
    {
        prometheus.atender([this]() { prometheus.responder(servidor, 404, "text/plain; charset=utf-8", "Página não encontrada."); });
    });
}

void GerenciadorWifi::iniciarPortal()
{
    LOG_INFO("Iniciando portal WiFi (Modo Access Point)");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("MonitorAmbiental", "12345678");

    configurarRotas();
    servidor.begin();
    portalAtivo = true; // Ativa para o Portal AP
    LOG_INFO(WiFi.softAPIP().toString().c_str());
}

void GerenciadorWifi::iniciar()
{
    if (conectar())
    {
        LOG_INFO("[Web] Inicializando Servidor HTTP na rede local");
        configurarRotas();
        servidor.begin();
        portalAtivo = true; // CORREÇÃO: Garante que o servidor processe requisições no modo Estação
    }
    else
    {
        iniciarPortal();
    }
}

void GerenciadorWifi::processar()
{
    if (portalAtivo)
    {
        servidor.handleClient();
    }
}

bool GerenciadorWifi::conectado() const
{
    return WiFi.status() == WL_CONNECTED;
}

String GerenciadorWifi::obterIp() const
{
    if (!conectado())
    {
        return "";
    }

    return WiFi.localIP().toString();
}

void GerenciadorWifi::paginaDashboard()
{
    String html;
    html.reserve(14500);
    html += R"HTML(<!doctype html><html lang="pt-BR"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Ambiente | Monitor Ambiental</title>
<style>
:root{color-scheme:light;--ink:#203d35;--muted:#65796f;--line:#dce5de;--green:#286649;--paper:#f5f6f0;font-family:system-ui,-apple-system,"Segoe UI",sans-serif;background:var(--paper);color:var(--ink)}
*{box-sizing:border-box}body{margin:0}a{color:inherit;text-decoration:none}button,input{font:inherit}button,a,input{touch-action:manipulation}button:focus-visible,a:focus-visible,input:focus-visible,summary:focus-visible{outline:3px solid #b17c28;outline-offset:4px}
nav{border-bottom:1px solid var(--line);background:#fafbf7}.nav-inner{max-width:1120px;margin:auto;display:flex;align-items:center;justify-content:space-between;gap:20px;padding:19px 26px}.brand{display:flex;align-items:center;gap:10px;font-size:15px;font-weight:700}.mark{display:grid;place-items:center;background:var(--green);color:white;width:30px;height:30px;border-radius:10px}.links{display:flex;gap:6px;font-size:13px}.links a{padding:9px 13px;border-radius:7px}.links .active{background:#e5ecdf;color:var(--green);font-weight:650}.links a:hover{background:#e5ecdf}
main{max-width:1120px;margin:auto;padding:42px 26px 24px}.eyebrow{font-size:11px;letter-spacing:2px;font-weight:700;text-transform:uppercase;color:var(--muted)}.intro{display:flex;justify-content:space-between;align-items:center;gap:20px;margin-bottom:30px}h1{font-size:clamp(30px,5vw,44px);font-weight:550;letter-spacing:-1.7px;margin:8px 0}p{color:var(--muted);font-size:14px;line-height:1.6;margin:8px 0}.connection{text-align:right}.badge{display:inline-flex;align-items:center;gap:7px;background:#e7ece3;color:#53664d;border:1px solid #d3decd;border-radius:99px;padding:7px 12px;font-size:12px;font-weight:600}.badge:before{content:'';height:6px;width:6px;border-radius:50%;background:currentColor}.badge.ok{background:#e4efdf;color:#386a35}.badge.error{background:#f9eadd;color:#965725}.connection p{font-size:12px}
.metrics{display:grid;grid-template-columns:1.2fr 1fr 1fr;gap:16px}.card{background:#fff;border:1px solid var(--line);border-radius:18px;padding:24px}.metric{min-height:210px;display:flex;flex-direction:column}.metric.primary{background:#254d3a;color:#f5f8ef;border-color:#254d3a}.metric.primary .eyebrow,.metric.primary p{color:#cad8c7}.metric-top{display:flex;justify-content:space-between;align-items:center}.symbol{font-size:20px;opacity:.6}.reading{font-size:clamp(38px,5vw,58px);letter-spacing:-2px;font-weight:550;line-height:1.2;margin:22px 0 12px;font-variant-numeric:tabular-nums}.reading small{font-size:22px;letter-spacing:0;font-weight:400;opacity:.7}.metric p{margin-top:auto;font-size:12px}.below{display:grid;grid-template-columns:minmax(0,1fr) 285px;gap:16px;margin-top:16px}h2{font-size:17px;font-weight:650;margin:0;letter-spacing:-.3px}.card-head{display:flex;align-items:center;justify-content:space-between;gap:16px}.legend{display:flex;gap:16px;font-size:11px;color:var(--muted)}.legend span:before{content:'';display:inline-block;width:7px;height:7px;border-radius:50%;background:#43865c;margin-right:6px}.legend span:last-child:before{background:#b58044}.chart{width:100%;height:220px;display:block;margin-top:18px}.chart text{font:11px system-ui;fill:#738176}.chart-note{font-size:11px;margin-top:0}.network dl{margin:22px 0 0}.network dt{font-size:11px;color:var(--muted);margin:17px 0 6px}.network dd{margin:0;font-size:14px;font-weight:550;overflow-wrap:anywhere}.track{height:5px;border-radius:9px;background:#e6ece3;margin-top:10px;overflow:hidden}.track div{height:100%;width:0;background:#638253;transition:width .3s}.network a{display:inline-block;margin-top:24px;font-size:12px;color:var(--green);text-decoration:underline;text-underline-offset:4px}
details{margin-top:24px;border-top:1px solid var(--line);padding:20px 0}summary{cursor:pointer;font-size:13px;font-weight:600;color:var(--muted)}.tools{padding-top:14px}.buttons{display:flex;gap:8px;flex-wrap:wrap}button{border:1px solid #cdd9ce;border-radius:8px;background:#fff;color:var(--ink);padding:10px 14px;cursor:pointer;font-size:13px}button:hover{background:#eaf0e5}.danger{color:#965340}.command{display:flex;gap:8px;margin-top:12px}.command input{min-width:0;flex:1;border:1px solid #cdd9ce;border-radius:8px;padding:11px;background:white;font-size:13px}.command button{background:var(--green);color:white}.terminal{background:#edf1e9;color:#294e37;padding:16px;border-radius:10px;font:12px/1.6 monospace;white-space:pre-wrap;overflow-wrap:anywhere;max-height:260px;overflow:auto}footer{display:flex;justify-content:space-between;border-top:1px solid var(--line);padding:18px 0;font-size:11px;color:var(--muted)}
@media(max-width:800px){.below{grid-template-columns:1fr}.network dl{display:grid;grid-template-columns:1fr 1fr;gap:12px}.network dt{margin-top:0}.network a{margin-top:16px}.legend{gap:10px}}
@media(max-width:560px){.nav-inner{padding:15px 18px}.brand{font-size:12px}.links a{padding:8px;font-size:11px}main{padding:28px 18px 16px}.intro{align-items:flex-start;flex-direction:column;gap:12px}.connection{text-align:left}.connection p{display:inline;margin-left:10px}.metrics{grid-template-columns:1fr 1fr;gap:12px}.metric{padding:20px;min-height:175px}.metric.primary{grid-column:1/-1;min-height:200px}.primary .reading{font-size:64px}.reading{font-size:38px}.reading small{font-size:18px}.card-head{align-items:flex-start;flex-direction:column;gap:10px}.card{padding:20px}.eyebrow{font-size:10px;letter-spacing:1.3px}footer{gap:12px;flex-wrap:wrap}}
.nav-inner,.links{flex-wrap:wrap}
@media(prefers-reduced-motion:reduce){*{transition:none!important}}
</style></head><body>
<nav aria-label="Navegação principal"><div class="nav-inner"><a href="/" class="brand"><span class="mark" aria-hidden="true">↗</span> Monitor Ambiental</a><div class="links"><a class="active" aria-current="page" href="/">Visão geral</a><a href="/config">Wi-Fi</a><a href="/prometheus">Prometheus</a><a href="/homeassistant">Home Assistant</a></div></div></nav>
<main><header class="intro"><div><div class="eyebrow">Seu espaço, em tempo real</div><h1>Como está o ambiente?</h1><p>As condições de agora, dentro e fora de casa.</p></div><div class="connection"><span id="status" class="badge" role="status">Conectando</span><p id="datetime">Aguardando dados</p></div></header>
<section class="metrics" aria-label="Leituras atuais">
<article class="card metric primary"><div class="metric-top"><span class="eyebrow">Temperatura local</span><span class="symbol" aria-hidden="true">°</span></div><div class="reading"><span id="temp">—</span> <small>°C</small></div><p id="tendencia">Aguardando leitura do sensor</p></article>
<article class="card metric"><div class="metric-top"><span class="eyebrow">Umidade local</span><span class="symbol" aria-hidden="true">◌</span></div><div class="reading"><span id="umid">—</span> <small>%</small></div><p>Umidade relativa do ar</p></article>
<article class="card metric"><div class="metric-top"><span class="eyebrow">Porto Alegre</span><span class="symbol" aria-hidden="true">↗</span></div><div class="reading"><span id="externa">—</span> <small>°C</small></div><p>Última leitura externa disponível</p></article>
</section>
<div class="below"><section class="card"><div class="card-head"><h2>Temperatura ao longo do tempo</h2><div class="legend"><span>Seu ambiente</span><span>Porto Alegre</span></div></div><svg id="chart" class="chart" viewBox="0 0 600 220" role="img" aria-label="Histórico de temperaturas em graus Celsius"></svg><p class="chart-note" id="chartNote">Aguardando as primeiras amostras.</p></section>
<aside class="card network"><h2>Conexão do monitor</h2><dl><div><dt>Rede Wi-Fi</dt><dd id="ssid">—</dd></div><div><dt>Endereço local</dt><dd id="ip">—</dd></div><div><dt>Intensidade do sinal</dt><dd id="rssi">—</dd><div class="track"><div id="signal"></div></div></div><div><dt>Última atualização</dt><dd id="updated">—</dd></div></dl><a href="/config">Gerenciar redes Wi-Fi →</a></aside></div>
<section class="card" style="margin-top:16px" aria-label="Controle da tela"><div class="card-head"><h2>Tela do monitor</h2><span id="telaEstado" role="status">Consultando estado…</span></div><p>Apague a tela sem interromper as medições ou a conexão Wi-Fi.</p><div class="buttons"><button type="button" onclick="controlarTela(true)">Ligar tela</button><button type="button" onclick="controlarTela(false)">Desligar tela</button></div><p id="telaResposta" role="status"></p></section>
<details><summary>Ferramentas do dispositivo</summary><div class="tools"><p>Consulte o diagnóstico ou envie um comando ao monitor.</p><div class="buttons"><button onclick="enviarComando('help')">Ver comandos</button><button onclick="enviarComando('crash_info')">Diagnóstico de falhas</button><button class="danger" onclick="confirmarReboot()">Reiniciar monitor</button></div><form class="command" id="commandForm"><input id="cmdLivre" aria-label="Comando do dispositivo" placeholder="Digite um comando, como uptime" autocomplete="off" spellcheck="false"><button>Enviar</button></form><pre id="console" class="terminal" aria-live="polite" hidden></pre></div></details>
<footer><span>Monitor Ambiental · ESP32</span><span>Atualização automática a cada 2 segundos</span></footer></main>
)HTML";
    html += injetarScriptsDashboard();
    html += "</body></html>";
    servidor.sendHeader("Cache-Control", "no-store");
    prometheus.responder(servidor, 200, "text/html; charset=utf-8", html);
}

String GerenciadorWifi::injetarScriptsDashboard()
{
    return R"HTML(<script>
const el=id=>document.getElementById(id);
const numero=v=>typeof v==='number'&&Number.isFinite(v);
const valor=v=>numero(v)?v.toLocaleString('pt-BR',{minimumFractionDigits:1,maximumFractionDigits:1}):'—';
let ultimo=null;
function desenhar(d){
 const svg=el('chart'),ns='http://www.w3.org/2000/svg';svg.replaceChildren();
 const horas=Array.isArray(d.historico_hora)?d.historico_hora:[];
 const local=Array.isArray(d.historico_local)?d.historico_local:[];
 const poa=Array.isArray(d.historico_poa)?d.historico_poa:[];
 const valores=local.concat(poa).filter(numero);
 function add(tag,attrs,text){const n=document.createElementNS(ns,tag);Object.entries(attrs).forEach(([k,v])=>n.setAttribute(k,v));if(text!==undefined)n.textContent=text;svg.appendChild(n);return n;}
 if(!valores.length){add('text',{x:300,y:110,'text-anchor':'middle'},'O histórico aparece com as primeiras leituras.');el('chartNote').textContent='Aguardando sensor e sincronização do relógio.';return;}
 const min=Math.floor(Math.min(...valores)-1),max=Math.ceil(Math.max(...valores)+1),n=Math.max(local.length,poa.length);
 const x=i=>44+(n>1?i/(n-1):0.5)*540,y=v=>180-(v-min)/(max-min)*160;
 for(let i=0;i<=4;i++){const v=min+(max-min)*i/4,py=y(v);add('line',{x1:44,y1:py,x2:584,y2:py,stroke:'#e8ede5'});add('text',{x:34,y:py+4,'text-anchor':'end'},v.toFixed(0)+'°');}
 [local,poa].forEach((serie,k)=>{let path='';let conectado=false;serie.forEach((v,i)=>{if(!numero(v)){conectado=false;return;}path+=(conectado?' L':' M')+x(i)+' '+y(v);conectado=true;});add('path',{d:path,fill:'none',stroke:k?'#b58044':'#43865c','stroke-width':2.5,'stroke-linejoin':'round'});serie.forEach((v,i)=>{if(numero(v))add('circle',{cx:x(i),cy:y(v),r:n>30?1.3:2.5,fill:k?'#b58044':'#43865c'});});});
 if(horas.length){add('text',{x:44,y:207},horas[0]);if(horas.length>1)add('text',{x:584,y:207,'text-anchor':'end'},horas[horas.length-1]);}
 el('chartNote').textContent='Histórico disponível no monitor · °C';
}
async function atualizar(){
 const ctrl=new AbortController(),timeout=setTimeout(()=>ctrl.abort(),10000);
 try{const r=await fetch('/api/dados',{cache:'no-store',signal:ctrl.signal});if(!r.ok)throw Error('HTTP '+r.status);const d=await r.json();
 el('temp').textContent=valor(d.temperatura);el('umid').textContent=valor(d.umidade);el('externa').textContent=valor(d.temp_poa);
 el('telaEstado').textContent=typeof d.tela_ligada==='boolean'?(d.tela_ligada?'Ligada':'Desligada'):'Estado indisponível';
 el('tendencia').textContent=!numero(d.temperatura)?'Sensor sem leitura disponível':d.tendencia===1?'↗ Temperatura em alta':d.tendencia===-1?'↘ Temperatura em queda':'→ Temperatura estável';
 el('datetime').textContent=d.data_hora||'Relógio não sincronizado';el('ssid').textContent=d.ssid||'Modo de configuração';el('ip').textContent=location.hostname;
 const pct=d.ssid&&numero(d.rssi)?Math.max(0,Math.min(100,2*(d.rssi+100))):0;
 el('rssi').textContent=d.ssid&&numero(d.rssi)?d.rssi+' dBm · '+(pct>=70?'Forte':pct>=40?'Moderado':'Fraco'):'Sem conexão Wi-Fi';el('signal').style.width=pct+'%';
 el('updated').textContent=new Date().toLocaleTimeString('pt-BR');el('status').textContent='Monitor conectado';el('status').className='badge ok';
 const chave=JSON.stringify([d.historico_hora,d.historico_local,d.historico_poa]);if(chave!==ultimo){desenhar(d);ultimo=chave;}
 }catch(e){el('telaEstado').textContent='Estado indisponível';el('status').textContent='Sem resposta do monitor';el('status').className='badge error';el('chartNote').textContent='Conexão interrompida. Os valores exibidos são da última atualização.';ultimo=null;}
 finally{clearTimeout(timeout);setTimeout(atualizar,2000);}
}
let comandoPendente=false;
async function enviarComando(cmd){if(comandoPendente)return;comandoPendente=true;const box=el('console');box.hidden=false;box.textContent='Enviando: '+cmd+'…\n';const ctrl=new AbortController(),timeout=setTimeout(()=>ctrl.abort(),15000);
 try{const r=await fetch('/api/comando?cmd='+encodeURIComponent(cmd),{cache:'no-store',signal:ctrl.signal});const text=await r.text();box.textContent+=text;if(!r.ok)box.textContent+='\nO monitor não concluiu o comando.';}catch(e){box.textContent+='Sem resposta. Verifique a conexão; se reiniciou o monitor, aguarde alguns segundos.';}finally{clearTimeout(timeout);comandoPendente=false;}}
async function controlarTela(ligada){
 if(comandoPendente){el('telaResposta').textContent='Aguarde o comando anterior terminar.';return;}
 el('telaResposta').textContent='Enviando comando…';
 await enviarComando(ligada?'tela_on':'tela_off');
 el('telaResposta').textContent=el('console').textContent;
}
function confirmarReboot(){if(confirm('Reiniciar o monitor agora? A conexão será interrompida por alguns segundos.'))enviarComando('reboot');}
el('commandForm').addEventListener('submit',e=>{e.preventDefault();const cmd=el('cmdLivre').value.trim();if(cmd&&!comandoPendente){enviarComando(cmd);el('cmdLivre').value='';}});
atualizar();
</script>)HTML";
}

void GerenciadorWifi::executarComandoWeb()
{
    if (!servidor.hasArg("cmd"))
    {
        prometheus.responder(servidor, 400, "text/plain; charset=utf-8", "[ERRO] Parâmetro 'cmd' ausente.");
        return;
    }

    String cmdString = servidor.arg("cmd");
    cmdString.trim();

    RespostaWeb resposta;
    processador.processarString(cmdString, resposta);

    servidor.sendHeader("Access-Control-Allow-Origin", "*");
    prometheus.responder(servidor, 200, "text/plain; charset=utf-8", resposta.texto);
}

void GerenciadorWifi::apiDados()
{
    LeituraAmbiental leitura = monitor.lerSensor();
    String horaAtual = relogio.obterHora();
    float tempAtualPoa = monitor.obterTemperaturaPoa();

    // Alimenta a fila circular se a leitura do hardware for válida
    if (isfinite(leitura.temperatura) && horaAtual != "--:--:--")
    {
        if (totalRegistros == 0 || histHora[totalRegistros - 1] != horaAtual)
        {
            adicionarAoHistorico(leitura.temperatura, tempAtualPoa, horaAtual);
        }
    }

    String json;
    json.reserve(2048);
    json += "{";
    json += "\"tela_ligada\":";
    json += monitor.telaLigada() ? "true," : "false,";
    json += "\"temperatura\":" +
            (isfinite(leitura.temperatura) ? String(leitura.temperatura, 1) : String("null")) + ",";
    json +=
        "\"umidade\":" + (isfinite(leitura.umidade) ? String(leitura.umidade, 1) : String("null")) +
        ",";
    json += "\"tendencia\":" + String(monitor.obterTendenciaTermica()) + ",";
    json += "\"temp_poa\":" +
            (isfinite(tempAtualPoa) ? String(tempAtualPoa, 1) : String("null")) + ",";
    json += "\"data_hora\":" + textoJson(relogio.obterDataHora()) + ",";
    json += "\"ssid\":" + textoJson(WiFi.SSID()) + ",";
    json += "\"rssi\":" + String(WiFi.RSSI()) + ",";

    // Envio dos arrays de histórico para montagem do gráfico comparativo
    json += "\"historico_hora\":[";
    for (int i = 0; i < totalRegistros; i++)
    {
        json += textoJson(histHora[i]) + (i == totalRegistros - 1 ? "" : ",");
    }
    json += "],\"historico_local\":[";
    for (int i = 0; i < totalRegistros; i++)
    {
        json += String(histTemperaturaLocal[i], 1) + (i == totalRegistros - 1 ? "" : ",");
    }
    json += "],\"historico_poa\":[";
    for (int i = 0; i < totalRegistros; i++)
    {
        json += (isfinite(histTemperaturaPoa[i]) ? String(histTemperaturaPoa[i], 1) : String("null")) +
                (i == totalRegistros - 1 ? "" : ",");
    }
    json += "]}";

    servidor.sendHeader("Access-Control-Allow-Origin", "*");
    prometheus.responder(servidor, 200, "application/json; charset=utf-8", json);
}
