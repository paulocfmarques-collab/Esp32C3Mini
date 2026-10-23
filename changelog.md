# Histórico do ESP32C3Mini

## Ícones na tela inicial — 2026-10-21

- Substituídos T: e U: pelos ícones personalizados de termômetro e gota,
  usando os slots CGRAM 1 e 2 já carregados na inicialização.
- Mantidas as colunas LOCAL/POA, unidades, indicação LOCAL* e limite de
  dez caracteres por coluna. Sem alteração das leituras ou do botão.
- Compilação e conferência física do LCD ficam com o usuário.

## Cabeçalho POA e falhas pontuais do DHT22 — 2026-10-21

- Abreviado o título externo no LCD para POA, com coluna local de dez posições.
- Somente no LCD, falhas pontuais reaproveitam uma amostra válida de menos
  de dez segundos, com indicação LOCAL*. Ao expirar o prazo, volta a `--`.
- Timestamp atualizado apenas na aquisição válida real, por timer monotônico.
  APIs, MQTT e Prometheus preservam o comportamento de amostra inválida.
- Não alterado o leitor para desabilitar interrupções ou retirar limites de
  tempo. A causa física/temporal de leituras descartadas ainda exige teste
  na placa. Compilação e gravação ficam com o usuário.

## Documentação dos pinos — 2026-10-21

- Criado [pinos_e_diagrama.md](pinos_e_diagrama.md), com GPIOs utilizados,
  alimentação/GND, ligação do botão, diagrama de sinais e diagrama funcional
  das integrações de rede. Mapeamento conferido no código atual.
- Documentadas as restrições de níveis I²C e boot, sem presumir a alimentação
  ou o esquema elétrico dos módulos físicos. Sem alteração do firmware.

## Retorno automático do LCD — 2026-10-21

- Página de rede retorna ao início após 15 segundos sem interação física.
- Eventos do botão reiniciam o prazo. A regra só atua com tela ligada e fora
  da página inicial, sem apagar a iluminação ou interromper outras funções.
- Usada subtração de tempo sem sinal para tolerar overflow de millis;
  retorno aplicado no loop principal. Compilação e teste ficam com o usuário.

## Botão externo e duas páginas LCD — 2026-10-21

- Configurado botão normalmente aberto no GPIO 3, entre GPIO e GND,
  com pull-up interno e debounce de 40 ms.
- Segurar por cinco segundos apaga a tela; toque com ela apagada acende e
  volta ao início. O mesmo gesto de despertar não dispara outra ação.
- Toque curto com tela acesa alterna entre duas páginas: condições ambientais
  e status da rede. Gesto longo dispara uma vez e soltar não religa.
- Tela inicial mostra data/hora e temperatura/umidade locais e externas em
  quatro linhas. Segunda página mostra conexão, IP, SSID e sinal Wi-Fi.
- Clock renderizado sem espera por NTP, a cada segundo; escritas I2C apenas
  nas linhas alteradas, com preenchimento/truncamento em 20 colunas.
- Sensor local continua sendo lido sem Wi-Fi. Removido o carrossel automático
  de IP/hora/data da tela principal.
- Detecção do botão em tarefa com fila, sem I2C fora do loop principal;
  fallback para leitura no loop se faltar memória. Chamadas síncronas de rede
  ainda podem atrasar a aplicação de eventos. Eventos ignorados durante OTA,
  com retomada da navegação se a atualização falhar.
- Compilação e validação física ficam com o usuário.
- Simulação das funções extraídas conferiu debounce, limite de cinco segundos,
  disparo único, despertar sem ação adicional, alternância de duas páginas,
  bloqueio durante OTA e overflow de millis. Textos conferidos para 20×4;
  essa verificação não substitui compilação nem teste do botão/LCD na placa.

## Consulta de credenciais Home Assistant — 2026-10-21

- Adicionados `ha_user` e `ha_password` à ajuda e ao processador compartilhado
  por serial, UDP e web. Retornam o valor cadastrado sem prefixo; a senha é
  devolvida em texto claro por solicitação do usuário.
- A consulta considera a configuração recém-salva aguardando reconexão e
  funciona com MQTT desativado. Valores vazios geram linha vazia.
- Não alterada a persistência; credenciais continuam fora das métricas e
  dos logs automáticos. Compilação e teste na placa ficam com o usuário.

## 2026-10-21 — Formato parseável do help

- Padronizada a resposta de `help` com o cabeçalho `COMANDOS` e um comando
  por linha, para facilitar o parseamento automático.
- Atualizado o guia de comandos.

## Home Assistant e métricas UDP — 2026-10-19

- Adicionada integração MQTT Discovery, desativada por padrão, configurável
  por `/homeassistant` e persistida em `homeassistant/config` nas Preferences.
- Nova dependência: PubSubClient >= 2.8, de Nick O'Leary, para instalar na IDE.
- Descoberta de sete sensores, switch LCD e botão para consulta climática;
  identificadores por MAC, disponibilidade Last Will, expiração dos sensores,
  reconexão espaçada e redescoberta após nascimento MQTT do Home Assistant.
- Adicionados `ha_status`, `ha_discovery` e publicação real das métricas MQTT.
- Adicionadas `esp_udp_listener_up`, `esp_udp_packets_received_total`,
  `esp_udp_replies_sent_total`, `esp_udp_send_errors_total`.
- Abertura UDP agora valida resultado e tenta novamente a cada dez segundos
  em caso de falha; envio acompanha beginPacket, escrita e endPacket.
- Datagrama incompleto/maior que 255 bytes é descartado com resposta de erro,
  sem executar comando truncado. Buffer de leitura não reaproveita texto antigo.
- Documentados limites e roteiro de teste; compilação e placa ficam com o usuário.
- Conferidos os JSONs das nove entidades, IDs únicos, tópicos, classes,
  expiração para intervalos de 10/30/3600 segundos e tamanho no buffer MQTT.
  Isso não substitui compilação nem teste com o broker/placa.

## Painéis SD sem séries — 2026-10-19

- Removida a emissão de todas as séries `sdcard_*`, incluindo capacidade,
  utilização, logs e erros. Não são enviados zeros nem NaN para o grupo SD,
  permitindo representar ausência de dados nos painéis do Grafana.
- Preservado `esp_metric_supported{group="sdcard"} 0` como indicador de suporte.
- Histórico já coletado permanece no Prometheus. Compilação e teste dos
  painéis ficam com o usuário.

## Idade do clima externo — 2026-10-19

- Adicionada `esp_weather_data_age_seconds`, compatível com o nome do M5NanoC6.
- Adicionados `clima_age` e alias `age` ao processador compartilhado e à ajuda.
- Idade calculada por timer monotônico de 64 bits desde a última consulta
  bem-sucedida; falhas não zeram a idade. Antes do primeiro sucesso, retorna
  NaN na métrica e mensagem de indisponibilidade no comando.
- Documentação atualizada. Compilação e testes na placa ficam com o usuário.

## Labels de clima externo para Grafana — 2026-10-19

- Adicionados `source="open_meteo"` e `location="porto_alegre"` às séries
  `sensor_temperature_celsius` e `sensor_humidity_percent`.
- Mantidos os labels globais `device` (MAC) e `esp_info{model="ESP32C3Mini"}`,
  permitindo filtrar as leituras externas com `and on(instance, job)`.
- Consultas por `model="$device"` exigem que a variável contenha o modelo,
  não o MAC. Documentados os exemplos em [prometheus.md](prometheus.md).
- Compilação e teste na placa ficam com o usuário.

## Prometheus — não lançado — 2026-10-19

- Adicionado `ServicoPrometheus`, com `/metrics` em porta exclusiva (9120
  por padrão); dashboard permanece na porta 80.
- Página `/prometheus` salva endereço de referência do servidor, porta do
  exporter e intervalo de coleta (5–3600 segundos, padrão 15) em um registro
  versionado `prometheus/config` nas Preferences. Valida os campos e confere
  gravação por leitura.
- Alteração da porta aplicada no loop principal sem reiniciar o dispositivo
  ou a interface web; portas 80 e 3232 reservadas.
- Coleta pelo Prometheus, conforme escolhido pelo usuário; sem Pushgateway
  ou envio ativo. `/prometheus/config.yml` gera o trecho com o intervalo
  configurado; endereço salvo é referência, sem filtragem de clientes.
- Exportadas identificação/build, estado Wi-Fi, uptime, motivo do reset,
  contadores existentes de boots/watchdogs, sinal/canal/TX Wi-Fi, tarefas,
  frequência CPU, heap, PSRAM e ocupação da partição de aplicação.
- Todas as séries Prometheus incluem `device` com o MAC Wi-Fi, permitindo
  seleção do Mini em dashboards Grafana que filtram por esse label.
- Incluídas temperatura CPU interna, temperatura/umidade DHT22 local, cache de
  Porto Alegre e estado LCD.
- Publicados nomes compatíveis para métricas SD/log como `NaN`; o Mini não tem
  montagem SD nem instrumentação de gravação de logs.
- Instrumentado `esp_cpu_usage_percent` com estatísticas de runtime FreeRTOS;
  a série requer aquecimento e fica indisponível se o SDK não oferecer suporte.
- Instrumentados handlers HTTP da porta 80 (requisições, HTTP ≥ 400 e duração).
  Coletas do exporter não incrementam esses contadores.
- Métricas sem integração (SD, energia, pressão, qualidade do ar, relés,
  GPIO e MQTT) retornam `NaN`; `esp_metric_supported`
  identifica grupos implementados. Não foram atribuídos pinos desconhecidos.
- Documentação: [prometheus.md](prometheus.md) e atualização de [comandos.md](comandos.md).
- Compilação, gravação e testes na placa ficam com o usuário. Pendentes
  coleta real, troca de porta, persistência após reset e validação de contadores.

Este documento descreve o código do Mini, sem atribuir números de versão que
não foram definidos no projeto. Referência dos comandos: [comandos.md](comandos.md).

## Alteração de firmware — 2026-10-19

- Adicionado o registro NVS `telemetry/boots` para contar inicializações e
  resets identificados como watchdog interno, de tarefa ou genérico.
- Adicionado `reset_counters`, disponível por Serial, UDP e HTTP, para zerar
  os contadores sem apagar perfis Wi-Fi, fuso ou DST.
- Em caso de falha ao registrar ou zerar os contadores, o firmware informa o
  erro; a falha de registro na inicialização também é enviada ao log serial.
- Os contadores começam a ser acumulados após instalar este firmware. Não
  realizada compilação completa nem validação na placa nesta alteração.

## Revisão documental — 2026-10-18

- Atualizadas as rotas web: dashboard em `/`, configuração em `/config`;
  removida a referência incorreta a `/dashboard`.
- Documentados os controles da tela, os intervalos distintos de atualização
  do painel, DHT22 e clima de Porto Alegre e os limites de validação em hardware.

- Documentado `name`, que retorna `ESP32C3Mini` para identificar a placa.
- Sincronizado o catálogo geral com os handlers de `wifi_clear`,
  `wifi_clear:SSID`, `clima` e `clima_sync`, já presentes nos fontes.
- Mantido `temp` como item apenas da ajuda, sem handler próprio.
- Diferenciado o apagamento de core dump após leitura do fluxo de
  arquivamento em SD usado em outras placas.
- Referência: [histórico consolidado](../../ESP32_Documents/changelog.md).
  Nenhuma alteração de firmware, compilação ou gravação nesta revisão.

## Não lançado — 2026-10-18

### Dashboard e controle da tela

- Reformulado o dashboard com fundo claro, leituras em destaque, layout para
  celular e ferramentas avançadas recolhidas.
- Substituída a dependência externa do gráfico por SVG gerado no navegador.
- Acrescentados estado da comunicação, última atualização e aviso sobre
  dados anteriores quando a API não responde.
- Consultas automáticas agendadas dois segundos após o término da anterior,
  com timeout e sem sobreposição entre consultas periódicas do mesmo painel.
- Adicionados `tela_on`, `tela_off`, `tela_status` e aliases `lcd_on`,
  `lcd_off`, `lcd_status` à ajuda e ao processador compartilhado por serial,
  UDP e HTTP.
- Acrescentados botões de ligar/desligar no dashboard e `tela_ligada` na API.
- Desligar a tela oculta caracteres e apaga a iluminação, preservando as
  medições e a rede. O estado não é persistido: reinicia ligada.

### Leitura do DHT22 e watchdog

- O log enviado em 18/10 mostrou um interrupt watchdog entre o início e o fim
  da leitura do sensor ao desconectar a serial, no firmware `ebbf0238c`.
  Esse registro evidencia um panic durante a leitura, e não apenas uma
  reconexão da porta USB.
- Substituída a leitura pela biblioteca DHT por leitura local do protocolo
  DHT22, sem desabilitar interrupções, com limites de espera por pulso e por
  quadro, checksum e validação de faixa.
- Centralizado o intervalo mínimo de dois segundos entre tentativas para
  atender display, comandos e API com o mesmo resultado.
- Leituras inválidas são descartadas; interrupções podem causar perda de
  uma amostra. A estabilidade e precisão precisam de teste na placa.

### Validação

- Verificada a sintaxe do JavaScript do dashboard e a presença dos elementos
  usados pelo controle da tela.
- Não realizada compilação completa ou gravação por estas alterações.
- Pendentes testes de abrir/fechar a serial, desconectar a USB com alimentação
  independente, leituras do DHT22 e comandos/botões do LCD na placa.

## Não lançado — 2026-10-17

### Inicialização do NTP e diagnóstico de panic

- Corrigido o panic `assert failed: udp_new_ip_type ... Required to lock TCPIP
  core functionality!` na tarefa `loopTask`. A análise do ELF da falha
  (SHA256 `3b198814f…`) mostrou o caminho `configTime` → `sntp_init` →
  `udp_new_ip_type`: `configurarFuso` roda no `setup` antes do Wi-Fi e, enquanto
  a tarefa TCP/IP ainda não está registrada, `configTime` pula o
  `LOCK_TCPIP_CORE`.
- Substituídas as chamadas de `configTime` pela API `esp_sntp_*`, que executa
  as operações na própria tarefa TCP/IP (`tcpip_callback`/`tcpip_api_call`).
- Mantida a inicialização da pilha de rede antes de iniciar o SNTP, com
  mensagem explícita em caso de erro.
- Centralizada a configuração do NTP para preservar fuso e DST salvos também
  na sincronização após conectar ao Wi-Fi; antes ela forçava GMT-3 sem DST.
- O fuso usa a convenção POSIX e o DST manual acrescenta uma hora quando ativo.
- Compilação e teste na placa ficam com o usuário.

### Integração do clima

- Padronizados os nomes de arquivo e includes como `ServicoMeteorologico`.
- Corrigido o nome do método `obterClimaPortoAlegre`.
- Substituída a consulta à página HTML genérica por uma consulta JSON à
  Open-Meteo para Porto Alegre.
- Adicionadas validação HTTPS pelo pacote de certificados do core, limites
  de tempo de conexão/leitura e validação dos campos de temperatura/umidade.
- Removidos valores fixos apresentados como se fossem medições externas.
- Adicionadas consultas automáticas a cada 15 minutos e atualização manual
  pelo comando `clima_sync`, com resposta explícita em caso de falha.
- Cache climático movido para o monitor; preserva a última leitura válida
  quando uma consulta posterior falha.
- Painel e histórico passaram a usar o mesmo cache externo do monitor.
- Dados externos indisponíveis aparecem como `--` no LCD e `null` no JSON.

### Display e organização

- Restaurada a implementação de `DisplayAmbiental.cpp`, que continha
  definições de `MonitorAmbiental` no lugar da classe de display.
- Implementado o layout comparativo LOCAL/POA, mantendo os ícones e a
  tendência de temperatura do sensor local.
- Mantidos a inicialização do LCD, o progresso de conexão, a tela de OTA e
  o status com IP completo.
- Limitada a animação de conexão ao interior dos colchetes da barra.
- Incluídos os documentos locais de comandos e histórico.

### Validação e pendências

- Não houve upload de firmware nesta alteração.
- A compilação completa, a disponibilidade do serviço HTTPS na rede local,
  o layout físico e a atualização OTA ainda precisam ser verificados na placa.
- A verificação sintática de todas as unidades do Mini foi iniciada; este
  registro não afirma sucesso antes da conclusão dessa verificação.
- A consulta meteorológica é síncrona; avaliar a responsividade durante
  falhas de rede e durante a negociação TLS.
- O cache preservado pode estar desatualizado e ainda não tem indicador
  explícito de idade no LCD.

## Alterações anteriores — resumo

### Estrutura orientada a objetos

- Separados sensor DHT22, display LCD e monitor ambiental em classes.
- Mantidos DHT22 no GPIO 2, I²C SDA 8/SCL 9 e LCD 20×4 no endereço `0x27`.
- Integrados gerenciamento Wi-Fi, relógio NTP, comandos e painel web.

### Correções de compilação e encapsulamento

- Corrigidos includes dos tipos usados pelo monitor.
- Adicionado diagnóstico de crash local com leitura e limpeza condicionada
  do resumo de core dump.
- Declarado o handler de comandos web e usada uma implementação de `Print`
  para capturar a resposta textual.
- Centralizado o relógio no gerenciador Wi-Fi.
- Adicionado acesso público à leitura do sensor, sem tornar o membro público.
- Corrigidos os sete ícones do LCD para arrays de oito bytes.
- Substituídos acessos OTA a campos privados por métodos de monitor/display.
- Corrigido o cálculo de progresso OTA para evitar divisão por zero e
  estouro na multiplicação.
- Ajustada a linha inferior para mostrar IPv4 completo, sem mudar DHCP.
- Adicionadas proteção de strings e representação `null` para valores não
  finitos na API JSON.

Essas correções tiveram verificações sintáticas pontuais em etapas anteriores;
isso não equivale a teste integrado do estado atual ou aprovação em hardware.
