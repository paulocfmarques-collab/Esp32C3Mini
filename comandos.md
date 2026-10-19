# Comandos do ESP32C3Mini

Referência do código em 19/10/2026. Implementação:
[ProcessadorComandos.cpp](ProcessadorComandos.cpp).
Catálogo de todas as placas: [COMANDOS.md](../../ESP32_Documents/COMANDOS.md).

## Transportes e sintaxe

- Serial USB: **115200 baud**, comando terminado por CR ou LF.
- UDP: **porta 4210**, resposta ao endereço e porta de origem.
- Web: **porta 80**, `GET /api/comando?cmd=COMANDO`.
- Os transportes usam o mesmo processador; escreva os nomes em minúsculas.
- Argumentos começam após `:`. Na URL, codifique caracteres especiais.
- Exemplo de consulta: `http://IP_DA_PLACA/api/comando?cmd=uptime`.

## Sistema e diagnóstico

| Comando | Função |
| --- | --- |
| `help` | Lista a ajuda disponível no firmware. |
| `name` | Identifica o dispositivo; responde `ESP32C3Mini`. |
| `info`, `status` | Informa o modo de rede STA/AP. |
| `version`, `build` | Retorna o texto de versão definido no handler; atualmente ainda identifica `wifi\wifi`, não a data de compilação. |
| `alive` | Confirma que o processador de comandos responde. |
| `uptime` | Consulta o tempo desde a inicialização. |
| `reboot` | Reinicia a placa. |
| `reset_counters` | Zera os contadores NVS de inicialização e watchdog. |
| `crash_info` | Informa motivo do reset e, quando habilitado, resumo do core dump ELF em flash. |
| `cpu`, `chip_info` | Consulta informações do processador/chip. |
| `ram`, `heap` | Consulta memória heap disponível. |
| `heap_min` | Consulta o mínimo de heap livre registrado. |
| `flash` | Consulta informações da memória flash. |

`crash_info` apaga o core dump somente após obter seu resumo com sucesso.
Não arquiva em SD. Quando o suporte ELF em flash não está habilitado, informa
essa condição e não apaga o registro.

O firmware registra inicializações e resets por watchdog no namespace NVS
`telemetry`, no registro `boots` (watchdog interno, de tarefa ou genérico).
`reset_counters` grava esses contadores como zero sem remover os perfis Wi-Fi,
o fuso ou o estado de DST. Os contadores começam a ser acumulados após instalar
este firmware.

## Tela LCD

| Comando | Função |
| --- | --- |
| `tela_on`, `lcd_on` | Liga o conteúdo e a iluminação da tela. |
| `tela_off`, `lcd_off` | Apaga o conteúdo e a iluminação; medições e rede continuam ativas. |
| `tela_status`, `lcd_status` | Consulta se a tela está ligada. |

O dashboard possui botões para ligar e desligar a tela. O estado também aparece
em `tela_ligada` na API `/api/dados`. Ao reiniciar, a tela volta ligada.
O estado não é gravado na NVS. Desligar a tela oculta os caracteres e apaga
a iluminação; não corta a alimentação elétrica do módulo LCD.

## Wi-Fi

| Comando | Função |
| --- | --- |
| `net_info`, `wifi_status` | Consulta o estado da conexão. |
| `ip` | Consulta o endereço IP. |
| `mac` | Consulta o endereço MAC. |
| `rssi` | Consulta a intensidade do sinal. |
| `ssid` | Consulta o nome da rede. |
| `wifi_add:SSID\|SENHA` | Salva um perfil e reinicia após gravação bem-sucedida. |
| `wifi_add:SSID` | Cadastra uma rede aberta. |
| `wifi_clear` | Remove todos os perfis, salva e reinicia após sucesso. |
| `wifi_clear:SSID` | Remove o perfil indicado e compacta os slots restantes. |

Há cinco slots de perfis. O cadastro usa o primeiro slot vazio ou com SSID
correspondente; se todos estiverem ocupados por outros SSIDs, retorna erro.
O comando remove espaços das extremidades do SSID e da senha.
As configurações são armazenadas nas Preferences/NVS.

Sem conexão a uma rede salva, o portal cria o AP **MonitorAmbiental**, com
senha **12345678**. Consulte o IP na serial.

## Relógio

| Comando | Função |
| --- | --- |
| `time` | Consulta a hora. |
| `date` | Consulta a data. |
| `ntp_status` | Consulta o estado da sincronização NTP. |
| `set_fuso:N` | Configura o deslocamento UTC entre −12 e +14 horas. |
| `dst_on` | Ativa o acréscimo de uma hora para horário de verão. |
| `dst_off` | Desativa esse acréscimo. |
| `dst_status` | Consulta o estado de DST. |

Exemplo: `set_fuso:-3`. Fuso e DST são persistidos no namespace `wifi`.

## Clima e sensor local

| Comando | Função |
| --- | --- |
| `clima` | Compara temperatura/umidade do DHT22 local com o cache de Porto Alegre. |
| `clima_sync` | Solicita uma consulta imediata à API; informa sucesso ou erro. |
| `clima_age`, `age` | Informa segundos e minutos desde a última consulta externa válida de Porto Alegre. |

A idade começa após uma consulta válida e só é zerada por outra consulta
bem-sucedida. Falhas preservam o cache e deixam a idade crescer. Antes do
primeiro sucesso após ligar, o comando informa que a idade está indisponível.
O mesmo valor aparece na métrica Prometheus `esp_weather_data_age_seconds`.

A Open-Meteo fornece os dados externos para as coordenadas de Porto Alegre.
Há uma consulta ao conectar e novas tentativas a cada **15 minutos**.
Não é necessário cadastrar uma chave de API.

O DHT22 local usa uma leitura com interrupções habilitadas, limites de espera
por pulso e validação de checksum e faixa. Display, API web e comandos
compartilham o resultado: há no máximo uma tentativa a cada **2 segundos**,
inclusive após falhas. Amostras inválidas retornam `NAN` internamente e `null`
na API; não são substituídas pela leitura local anterior. Ao ligar, o sensor
aguarda ao menos dois segundos antes da primeira tentativa.

O acesso usa HTTPS com o pacote de certificados do core ESP32 e requer
conectividade e relógio adequado à validação TLS. Cada consulta é síncrona e
pode atrasar temporariamente o atendimento das outras funções.

Até a primeira resposta válida, o LCD mostra `--` e o JSON retorna `null` para
o clima externo. Falhas posteriores preservam a última leitura válida, que
pode estar desatualizada. Não são usados valores climáticos fixos de contingência.

`temp` aparece na ajuda, mas não tem handler próprio no código consultado.
Use `clima` ou a API ambiental para obter a leitura local.

## Interface web

Todas as rotas abaixo usam a **porta 80**.

| Método e rota | Função |
| --- | --- |
| `GET /` | Dashboard ambiental, gráfico e controle da tela. |
| `GET /config` | Formulário de configuração Wi-Fi. |
| `POST /salvar` | Salva os perfis; reinicie conforme a orientação da página. |
| `GET /api/dados` | JSON com leituras locais, temperatura de Porto Alegre, tendência, rede, relógio, históricos e `tela_ligada` (booleano). |
| `GET /api/comando?cmd=COMANDO` | Executa comandos e retorna texto. |
| `GET /prometheus` | Configura servidor de referência, porta de métricas e intervalo de coleta. |
| `POST /prometheus/salvar` | Valida e grava configuração nas Preferences. |
| `GET /prometheus/config.yml` | Gera o trecho para configurar a coleta no servidor. |

Em uma **porta separada**, configurável na página `/prometheus` (padrão
**9120**), `GET /metrics` expõe o formato Prometheus. A mesma página configura
o endereço de referência do servidor (IPv4/DNS, sem URL) e o intervalo de
coleta (5–3600 segundos; padrão **15**). O YAML gerado usa esse intervalo.
O dashboard permanece na porta **80**; as portas 80 e 3232 não podem ser usadas
pelo exporter. A porta usual do servidor Prometheus (**9090**) não é uma porta
da placa. Consulte [prometheus.md](prometheus.md) para catálogo, configuração e
limites.

O servidor funciona tanto conectado ao roteador (**STA**) quanto no portal
**AP**. No modo STA, consulte `ip` e abra `http://IP_DA_PLACA/` em um dispositivo
na mesma rede. A rota `/dashboard` não está cadastrada no código atual.

O painel possui layout claro adaptado ao celular, leituras LOCAL/Porto Alegre,
gráfico SVG sem bibliotecas externas e indicadores de conexão e última
atualização. Os comandos avançados ficam em **Ferramentas do dispositivo**;
os botões **Ligar tela** e **Desligar tela** ficam visíveis no painel.

Uma nova consulta de dados é agendada dois segundos após a anterior terminar.
Isso atualiza a tela com o cache disponível, sem forçar uma nova consulta
meteorológica a cada ciclo. Se a comunicação falhar, o painel sinaliza que
os valores exibidos pertencem à última atualização.

Após enviar um firmware com mudanças no painel, atualize o navegador com
`Ctrl+F5` ou reabra a página no celular.

O LCD 20×4 compara **LOCAL / POA**, indica tendência térmica local e alterna
IP, hora e data na linha inferior. O layout comporta IPv4 de até 15 caracteres.

## Atualização OTA

- Nome de descoberta: **MonitorAmbiental-OTA**.
- ArduinoOTA: porta padrão **3232**, enquanto o Wi-Fi estiver conectado.
- O LCD mostra percentual e barra de 18 blocos.
- Este projeto não chama `ArduinoOTA.setPassword()` no código atual.
- A senha fixa `123456` do projeto `wifi_Eth_SDCard` **não se aplica** ao Mini.
- OTA requer uma tabela de partições compatível e uma imagem para a placa correta.

## Cuidados e limites

- Comandos HTTP e UDP não têm autenticação nem criptografia; use rede confiável.
- Não envie credenciais em URLs em redes compartilhadas.
- `reboot`, `wifi_add`, `wifi_clear` e `crash_info` têm efeitos persistentes ou
  destrutivos; não os use como simples teste de conectividade.
- O Mini implementa exporter Prometheus com porta exclusiva. Não implementa
  armazenamento SD; séries `sdcard_*` não são exportadas, para os painéis
  receberem ausência de dados (`No data`). O indicador de suporte SD vale 0.
- Consulte [changelog.md](changelog.md) para o histórico das alterações.
- Esta referência não substitui testes de compilação completa ou validação na placa.
- `wifi_clear` e `wifi_clear:SSID` têm handlers no código atual; a informação
  antiga do catálogo geral de que existiam somente no `help` foi corrigida.
