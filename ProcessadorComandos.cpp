#include "ProcessadorComandos.h"
#include "Log.h"
#include "CrashDiagnostics.h"
#include "IntegracaoHomeAssistant.h"
#include <WiFi.h>
#include <string.h>
#include <esp_system.h> // Necessário para extrair a causa real do reset

extern IntegracaoHomeAssistant homeAssistant;

namespace
{
class RespostaUdpMonitorada : public Print
{
  public:
    explicit RespostaUdpMonitorada(WiFiUDP& udp) : udp(udp) {}
    bool completa = true;
    size_t write(uint8_t valor) override { return write(&valor, 1); }
    size_t write(const uint8_t* dados, size_t tamanho) override
    {
        size_t escritos = udp.write(dados, tamanho);
        if (escritos != tamanho) completa = false;
        return escritos;
    }
  private:
    WiFiUDP& udp;
};
}

ProcessadorComandos::ProcessadorComandos(MonitorAmbiental& monitor) : _monitor(monitor)
{
    _bufferSerial.reserve(64);
}

void ProcessadorComandos::iniciar()
{
    _ultimaTentativaUdp = millis();
    _udpAtivo = _udp.begin(_portaUdp) == 1;
    if (_udpAtivo)
        LOG_INFO(("Servidor UDP de comandos iniciado na porta " + String(_portaUdp)).c_str());
    else
        LOG_ERROR("Falha ao abrir porta UDP de comandos; nova tentativa em 10 segundos");
}

void ProcessadorComandos::atualizar()
{
    // 1. Escuta comandos vindo da Serial USB
    while (Serial.available() > 0)
    {
        char caractere = Serial.read();
        if (caractere == '\n' || caractere == '\r')
        {
            _bufferSerial.trim();
            if (_bufferSerial.length() > 0)
            {
                processarString(_bufferSerial, Serial);
                _bufferSerial = "";
            }
        }
        else
        {
            _bufferSerial += caractere;
        }
    }

    // 2. Escuta comandos vindo da Rede via pacotes UDP
    if (!_udpAtivo && millis() - _ultimaTentativaUdp >= 10000)
    {
        _ultimaTentativaUdp = millis();
        _udpAtivo = _udp.begin(_portaUdp) == 1;
    }
    if (!_udpAtivo) return;
    int packetSize = _udp.parsePacket();
    if (packetSize > 0)
    {
        ++_udpRecebidos;
        const IPAddress remoto = _udp.remoteIP();
        const uint16_t portaRemota = _udp.remotePort();
        int len = _udp.read(_packetBuffer, 255);
        _packetBuffer[len > 0 ? len : 0] = 0;
        while (_udp.available() > 0) _udp.read();
        String comandoUdp = String(_packetBuffer);
        comandoUdp.trim();
        const bool invalido = packetSize > 255 || len != packetSize ||
                              (len > 0 && memchr(_packetBuffer, '\0', len) != nullptr);
        if (invalido || comandoUdp.length() > 0)
        {
            if (_udp.beginPacket(remoto, portaRemota) != 1)
            {
                ++_udpErros;
                return;
            }
            RespostaUdpMonitorada resposta(_udp);
            if (invalido) resposta.println("[ERRO] Pacote UDP invalido, incompleto ou maior que 255 bytes.");
            else processarString(comandoUdp, resposta);
            const bool enviado = _udp.endPacket() == 1;
            if (enviado && resposta.completa) ++_udpRespostas;
            else ++_udpErros;
        }
    }
}

void ProcessadorComandos::processarString(const String& entrada, Print& resposta)
{
    int indiceSeparador = entrada.indexOf(':');
    String cmd = (indiceSeparador == -1) ? entrada : entrada.substring(0, indiceSeparador);
    String args = (indiceSeparador == -1) ? "" : entrada.substring(indiceSeparador + 1);

    cmd.trim();
    args.trim();

    if (cmd == "wifi_add")
    {
        tratarWifiAdd(args, resposta);
    }
    else
    {
        executarComandoBase(cmd, args, resposta);
    }
}

void ProcessadorComandos::tratarWifiAdd(const String& args, Print& resposta)
{
    int divisor = args.indexOf('|');
    String ssid = "";
    String senha = "";

    if (divisor == -1)
    {
        ssid = args;
    }
    else
    {
        ssid = args.substring(0, divisor);
        senha = args.substring(divisor + 1);
    }

    ssid.trim();
    senha.trim();

    if (ssid.isEmpty())
    {
        resposta.println("[ERRO] SSID invalido para registro.");
        return;
    }

    bool cadastrado = false;
    for (int i = 0; i < 5; i++)
    {
        if (_monitor.wifi.redes[i].ssid.isEmpty() || _monitor.wifi.redes[i].ssid == ssid)
        {
            _monitor.wifi.redes[i].ssid = ssid;
            _monitor.wifi.redes[i].senha = senha;
            cadastrado = true;

            resposta.printf("[INFO] Slot %d preenchido na RAM. Gravando na particao NVS...\n",
                            i + 1);

            if (_monitor.wifi.salvarRedes())
            {
                resposta.println("[OK] Credenciais armazenadas permanentemente com sucesso!");
                resposta.println("[SISTEMA] Reiniciando o monitor em 2 segundos para conectar...");

                // Aguarda o envio dos buffers de rede/serial e reinicia
                delay(2000);
                ESP.restart();
            }
            else
            {
                resposta.println("[ERRO] Falha de hardware/particao ao salvar no NVS.");
            }
            break;
        }
    }

    if (!cadastrado)
    {
        resposta.println("[ERRO] Sem espaco livre na lista de 5 redes.");
    }
}

String ProcessadorComandos::obterMotivoReset()
{
    esp_reset_reason_t motivo = esp_reset_reason();
    switch (motivo)
    {
    case ESP_RST_POWERON:
        return "POWERON_RESET";
    case ESP_RST_EXT:
        return "EXTERNAL_PIN_RESET";
    case ESP_RST_SW:
        return "SOFTWARE_RESET";
    case ESP_RST_PANIC:
        return "HARDWARE_PANIC_CRASH";
    case ESP_RST_INT_WDT:
        return "INTERNAL_WATCHDOG_RESET";
    case ESP_RST_TASK_WDT:
        return "TASK_WATCHDOG_RESET";
    case ESP_RST_WDT:
        return "WATCHDOG_RESET";
    case ESP_RST_DEEPSLEEP:
        return "EXIT_DEEP_SLEEP";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT_VOLTAGE_RESET";
    default:
        return "UNKNOWN_RESET_REASON";
    }
}

void ProcessadorComandos::executarComandoBase(const String& cmd, const String& args,
                                              Print& resposta)
{
    // ---- CONFIGURAÇÃO DO MENU HELP ----
    if (cmd == "help")
    {
        resposta.println("COMANDOS");
        resposta.println("help");
        resposta.println("info");
        resposta.println("status");
        resposta.println("version");
        resposta.println("alive");
        resposta.println("uptime");
        resposta.println("reboot");
        resposta.println("reset_counters");
        resposta.println("crash_info");
        resposta.println("name");
        resposta.println("temp");
        resposta.println("cpu");
        resposta.println("chip_info");
        resposta.println("ram");
        resposta.println("heap");
        resposta.println("heap_min");
        resposta.println("flash");
        resposta.println("tela_on");
        resposta.println("lcd_on");
        resposta.println("tela_off");
        resposta.println("lcd_off");
        resposta.println("tela_status");
        resposta.println("lcd_status");
        resposta.println("net_info");
        resposta.println("wifi_status");
        resposta.println("ssid");
        resposta.println("mac");
        resposta.println("rssi");
        resposta.println("ip");
        resposta.println("wifi_add:SSID|SENHA");
        resposta.println("wifi_clear");
        resposta.println("wifi_clear:SSID");
        resposta.println("ha_status");
        resposta.println("ha_user");
        resposta.println("ha_password");
        resposta.println("ha_discovery");
        resposta.println("time");
        resposta.println("date");
        resposta.println("ntp_status");
        resposta.println("set_fuso:N");
        resposta.println("dst_on");
        resposta.println("dst_off");
        resposta.println("dst_status");
        resposta.println("clima");
        resposta.println("clima_sync");
        resposta.println("clima_age");
        resposta.println("age");
    }
    else if (cmd == "tela_on" || cmd == "lcd_on" || cmd == "tela_off" || cmd == "lcd_off")
    {
        bool ligada = cmd == "tela_on" || cmd == "lcd_on";
        _monitor.definirTelaLigada(ligada);
        resposta.println(ligada ? "[OK] Tela ligada."
                                : "[OK] Tela desligada. Monitoramento continua ativo.");
    }
    else if (cmd == "tela_status" || cmd == "lcd_status")
    {
        resposta.println(_monitor.telaLigada() ? "[TELA] Ligada." : "[TELA] Desligada.");
    }
    else if (cmd == "ha_user")
    {
        resposta.println(homeAssistant.obterUsuarioCadastrado());
    }
    else if (cmd == "ha_password")
    {
        resposta.println(homeAssistant.obterSenhaCadastrada());
    }
    else if (cmd == "ha_status")
    {
        resposta.println("[HA] " + homeAssistant.status());
    }
    else if (cmd == "ha_discovery")
    {
        homeAssistant.redescobrir();
        resposta.println("[HA] Descoberta solicitada; sera publicada quando o MQTT estiver conectado e ativo.");
    }
    else if (cmd == "name")
    {
        resposta.println("ESP32C3Mini");
    }
    else if (cmd == "temp")
    {
        resposta.println("CPU Temp: " + String(temperatureRead()) + " C");
    }
    else if (cmd == "reset_counters")
    {
        resposta.println(_monitor.zerarContadores()
                             ? "[OK] Contadores NVS de boot/watchdog zerados."
                             : "[ERRO] Falha ao zerar contadores NVS.");
    }
    else if (cmd == "clima")
    {
        LeituraAmbiental local = _monitor.sensor.ler();
        resposta.println("=== Comparativo Climatico ===");
        resposta.printf(" -> Sensor Local  | Temp: %s°C | Umid: %s%%\n",
                        isfinite(local.temperatura) ? String(local.temperatura, 1).c_str() : "--.-",
                        isfinite(local.umidade) ? String(local.umidade, 0).c_str() : "--");
        if (isfinite(_monitor.obterTemperaturaPoa()) && isfinite(_monitor.obterUmidadePoa()))
            resposta.printf(" -> Porto Alegre  | Temp: %.1f°C | Umid: %.0f%%\n",
                            _monitor.obterTemperaturaPoa(), _monitor.obterUmidadePoa());
        else
            resposta.println(" -> Porto Alegre: dados indisponiveis");
    }
    else if (cmd == "clima_age" || cmd == "age")
    {
        const double idade = _monitor.obterIdadeClimaSegundos();
        if (isfinite(idade))
        {
            resposta.printf("[CLIMA] Porto Alegre: ultima consulta valida ha %.1f segundos (%.1f minutos).\n",
                            idade, idade / 60.0);
        }
        else
        {
            resposta.println("[CLIMA] Idade indisponivel: nenhuma consulta externa valida desde a inicializacao.");
        }
    }
    else if (cmd == "clima_sync")
    {
        resposta.println("[API] Solicitando atualizacao remota dos dados de Porto Alegre...");
        if (_monitor.forcarSincronizacaoClima())
            resposta.printf("[OK] Dados atualizados! Temperatura atual em POA: %.1f°C\n",
                            _monitor.obterTemperaturaPoa());
        else
            resposta.println(
                "[ERRO] Falha na consulta; ultima leitura valida preservada, se houver.");
    }
    else if (cmd == "wifi_clear")
    {
        String alvo = args;
        alvo.trim();

        if (alvo.isEmpty())
        {
            resposta.println("[INFO] Limpando todas as 5 redes cadastradas...");
            for (int i = 0; i < 5; i++)
            {
                _monitor.wifi.redes[i].ssid = "";
                _monitor.wifi.redes[i].senha = "";
            }

            if (_monitor.wifi.salvarRedes())
            {
                resposta.println("[OK] Particao NVS limpa com sucesso!");
                resposta.println("[SISTEMA] Reiniciando em modo Portal AP em 2 segundos...");
                delay(2000);
                ESP.restart();
            }
            else
            {
                resposta.println("[ERRO] Falha ao limpar o banco de dados NVS.");
            }
        }
        else
        {
            bool encontrada = false;
            resposta.printf("[INFO] Procurando pela rede '%s' para remocao...\n", alvo.c_str());

            for (int i = 0; i < 5; i++)
            {
                if (_monitor.wifi.redes[i].ssid == alvo)
                {
                    _monitor.wifi.redes[i].ssid = "";
                    _monitor.wifi.redes[i].senha = "";
                    encontrada = true;
                    resposta.printf("[OK] Rede '%s' removida do slot %d.\n", alvo.c_str(), i + 1);
                    break;
                }
            }

            if (encontrada)
            {
                resposta.println("[REDE] Compactando slots para evitar espacos vazios...");
                int slotDestino = 0;
                for (int slotOrigem = 0; slotOrigem < 5; slotOrigem++)
                {
                    if (!_monitor.wifi.redes[slotOrigem].ssid.isEmpty())
                    {
                        if (slotOrigem != slotDestino)
                        {
                            _monitor.wifi.redes[slotDestino].ssid =
                                _monitor.wifi.redes[slotOrigem].ssid;
                            _monitor.wifi.redes[slotDestino].senha =
                                _monitor.wifi.redes[slotOrigem].senha;
                            _monitor.wifi.redes[slotOrigem].ssid = "";
                            _monitor.wifi.redes[slotOrigem].senha = "";
                        }
                        slotDestino++;
                    }
                }

                resposta.println("[REDE] Nova estrutura de slots:");
                for (int i = 0; i < 5; i++)
                {
                    if (!_monitor.wifi.redes[i].ssid.isEmpty())
                    {
                        resposta.printf(" -> Slot %d: %s\n", i + 1,
                                        _monitor.wifi.redes[i].ssid.c_str());
                    }
                    else
                    {
                        resposta.printf(" -> Slot %d: [VAZIO]\n", i + 1);
                    }
                }

                if (_monitor.wifi.salvarRedes())
                {
                    resposta.println("[OK] Lista compactada e salva na Flash com sucesso.");
                }
                else
                {
                    resposta.println("[ERRO] Falha ao persistir a compactacao na particao NVS.");
                }
            }
            else
            {
                resposta.printf("[AVISO] Nenhuma rede com o nome '%s' foi encontrada.\n",
                                alvo.c_str());
            }
        }
    }
    // ---- COMANDO: SET_FUSO ----
    else if (cmd == "set_fuso")
    {
        String parametro = args;
        parametro.trim();

        if (parametro.isEmpty())
        {
            resposta.println("[ERRO] Uso correto: set_fuso:N (ex: -3, 0, 1)");
            return;
        }

        int novoFuso = parametro.toInt();
        if (novoFuso < -12 || novoFuso > 14)
        {
            resposta.println("[ERRO] Fuso invalido (use entre -12 e 14).");
            return;
        }
        // Salva permanentemente na NVS
        Preferences prefs;
        if (prefs.begin("wifi", false))
        {
            prefs.putInt("fuso", novoFuso);
            prefs.end();

            // Aplica mantendo o estado atual do DST
            _monitor.relogio.configurarFuso(novoFuso, _monitor.relogio.obterDst());

            resposta.printf("[OK] Fuso GMT %d salvo permanentemente na Flash.\n", novoFuso);
            resposta.printf("[HORA] %s\n", _monitor.relogio.obterDataHora().c_str());
        }
        else
        {
            resposta.println("[ERRO] Falha ao acessar a particao NVS.");
        }
    }
    // ---- COMANDO: DST_ON ----
    else if (cmd == "dst_on")
    {
        Preferences prefs;
        if (prefs.begin("wifi", false))
        {
            prefs.putBool("dst", true);
            prefs.end();

            _monitor.relogio.definirDst(true);

            resposta.println("[OK] Horario de Verao ATIVADO (+1h) e salvo na Flash.");
            resposta.printf("[HORA] %s\n", _monitor.relogio.obterDataHora().c_str());
        }
        else
        {
            resposta.println("[ERRO] Falha ao acessar a particao NVS.");
        }
    }
    // ---- COMANDO: DST_OFF ----
    else if (cmd == "dst_off")
    {
        Preferences prefs;
        if (prefs.begin("wifi", false))
        {
            prefs.putBool("dst", false);
            prefs.end();

            _monitor.relogio.definirDst(false);

            resposta.println("[OK] Horario de Verao DESATIVADO e salvo na Flash.");
            resposta.printf("[HORA] %s\n", _monitor.relogio.obterDataHora().c_str());
        }
        else
        {
            resposta.println("[ERRO] Falha ao acessar a particao NVS.");
        }
    }
    else if (cmd == "dst_status")
    {
        bool dstAtivo = _monitor.relogio.obterDst();
        resposta.printf("[DST] Horario de Verao %s.\n", dstAtivo ? "ATIVADO (+1h)" : "DESATIVADO");
    }
    else if (cmd == "crash_info")
    {
        resposta.println("--- Executando Diagnostico de Falhas (Core Dump) ---");
        String motivo = obterMotivoReset();
        String relatorio = CrashDiagnostics::lerEApagar(motivo);
        resposta.print(relatorio);
    }
    else if (cmd == "info" || cmd == "status")
    {
        resposta.printf("[STATUS] Monitor ONLINE: %s\n",
                        _monitor.wifi.conectado() ? "STA Modo" : "Portal AP");
    }
    else if (cmd == "version" || cmd == "build")
    {
        resposta.println("[VERSAO] Firmware wifi\\wifi v1.5.0 (ESP32)");
    }
    else if (cmd == "alive")
    {
        resposta.println("[ALIVE] Operando normalmente.");
    }
    else if (cmd == "uptime")
    {
        resposta.printf("[UPTIME] %lu segundos ativos.\n", millis() / 1000);
    }
    else if (cmd == "reboot")
    {
        resposta.println("[AVISO] Reiniciando hardware de forma remota...");
        delay(300);
        ESP.restart();
    }
    // ---- GRUPO: HARDWARE ----
    else if (cmd == "heap" || cmd == "ram")
    {
        resposta.printf("[RAM] Livre: %u bytes\n", ESP.getFreeHeap());
    }
    else if (cmd == "heap_min")
    {
        resposta.printf("[RAM] Minimo historico registrado: %u bytes\n", ESP.getMinFreeHeap());
    }
    else if (cmd == "chip_info" || cmd == "cpu")
    {
        resposta.printf("[CPU] %s (%d Cores) @ %d MHz\n", ESP.getChipModel(), ESP.getChipCores(),
                        ESP.getCpuFreqMHz());
    }
    else if (cmd == "flash")
    {
        resposta.printf("[FLASH] Tamanho integrado: %u MB\n",
                        ESP.getFlashChipSize() / (1024 * 1024));
    }
    // ---- GRUPO: REDE ----
    else if (cmd == "net_info" || cmd == "wifi_status")
    {
        resposta.println("--- Informacoes de Rede ---");
        resposta.printf("Estado: %s\n",
                        _monitor.wifi.conectado() ? "CONECTADO" : "DESCONECTADO / PORTAL ATIVO");
        resposta.printf("IP Local: %s | MAC: %s\n",
                        _monitor.wifi.obterIp().isEmpty() ? "0.0.0.0"
                                                          : _monitor.wifi.obterIp().c_str(),
                        WiFi.macAddress().c_str());
        resposta.printf("SSID Atual: %s | Sinal: %d dBm\n",
                        WiFi.SSID().isEmpty() ? "Nenhum" : WiFi.SSID().c_str(), WiFi.RSSI());

        resposta.println("\n[Slots de Memoria NVS]");
        for (int i = 0; i < 5; i++)
        {
            if (!_monitor.wifi.redes[i].ssid.isEmpty())
            {
                String senhaMascarada =
                    _monitor.wifi.redes[i].senha.isEmpty() ? "[Aberta]" : "****";
                resposta.printf(" -> Slot %d: SSID: '%s' | Senha: %s\n", i + 1,
                                _monitor.wifi.redes[i].ssid.c_str(), senhaMascarada.c_str());
            }
            else
            {
                resposta.printf(" -> Slot %d: [LIVRE]\n", i + 1);
            }
        }
    }
    else if (cmd == "ip")
    {
        resposta.printf("[IP] %s\n", _monitor.wifi.obterIp().isEmpty()
                                         ? "0.0.0.0"
                                         : _monitor.wifi.obterIp().c_str());
    }
    else if (cmd == "mac")
    {
        resposta.printf("[MAC] %s\n", WiFi.macAddress().c_str());
    }
    else if (cmd == "rssi")
    {
        resposta.printf("[RSSI] %d dBm\n", WiFi.RSSI());
    }
    else if (cmd == "ssid")
    {
        resposta.printf("[SSID] %s\n", WiFi.SSID().c_str());
    }
    // ---- GRUPO: HORA ----
    else if (cmd == "time")
    {
        // Correção/Garantia de Sincronismo dinâmico:
        if (_monitor.wifi.conectado() && !_monitor.relogio.estaSincronizado())
        {
            _monitor.relogio.iniciar();
        }
        resposta.printf("[HORA] %s\n", _monitor.relogio.obterHora().c_str());
    }
    else if (cmd == "date")
    {
        resposta.printf("[DATA] %s\n", _monitor.relogio.obterData().c_str());
    }
    else if (cmd == "ntp_status")
    {
        resposta.printf("[NTP] Sincronizado: %s\n",
                        _monitor.relogio.estaSincronizado() ? "SIM" : "NAO");
    }
    else if (cmd == "set_fuso")
    {
        String parametro = args;
        parametro.trim();

        if (parametro.isEmpty())
        {
            resposta.println("[ERRO] Uso correto: set_fuso:N (onde N e o fuso, ex: -3, 0, 1)");
            return;
        }

        // Converte o argumento para inteiro (valida fusos comuns entre -12 e +14)
        int novoFuso = parametro.toInt();

        if (novoFuso < -12 || novoFuso > 14)
        {
            resposta.println("[ERRO] Fuso horario invalido. Digite um valor entre -12 e 14.");
            return;
        }

        // Aplica o fuso no subsistema de relógio NTP
        _monitor.relogio.configurarFuso(novoFuso);

        resposta.printf("[OK] Fuso horario alterado com sucesso para GMT %d!\n", novoFuso);
        resposta.printf("[HORA ATUAL] %s\n", _monitor.relogio.obterDataHora().c_str());
    }
    else
    {
        resposta.printf("[ERRO] Comando '%s' nao reconhecido. Digite 'help'.\n", cmd.c_str());
    }
}
