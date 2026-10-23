# Home Assistant — ESP32C3Mini

Implementação: [IntegracaoHomeAssistant.cpp](IntegracaoHomeAssistant.cpp).
Configuração web: `http://IP_DA_PLACA/homeassistant`, também acessível pelo
menu do dashboard. A web permanece na porta 80; Prometheus mantém sua porta.

## Preparação

1. Na IDE Arduino, abra o gerenciador de bibliotecas e instale **PubSubClient**,
   de **Nick O'Leary**, versão **2.8 ou superior**. É uma nova dependência.
2. Compile e envie os arquivos atuais do projeto.
3. Tenha um broker MQTT acessível pela rede, por exemplo Mosquitto. No Home
   Assistant, configure a integração **MQTT** para usar esse broker.
4. Na página `/homeassistant`, informe endereço, porta (padrão 1883), usuário
   e senha do broker. Marque **Ativar integração MQTT** e salve.
5. Procure o dispositivo **Monitor Ambiental Mini** na integração MQTT.

A descoberta usa prefixo fixo `homeassistant`, que deve ser o mesmo no Home
Assistant. Não basta informar o endereço do Home Assistant: o destino é o
broker MQTT. O firmware não instala/configura o broker ou o Home Assistant.

## Configuração e persistência

- Integração desativada por padrão, sem conexões MQTT até ser habilitada.
- Host IPv4 ou DNS, sem protocolo/caminho; porta 1–65535.
- Usuário opcional: se vazio, conexão anônima. A senha só é usada com usuário.
- Senha vazia no formulário preserva a anterior; **Apagar senha salva** a remove.
- Limites em bytes: host 127, usuário 63, senha 127.
- Publicação a cada 30 segundos por padrão, configurável entre 10 e 3600.
- Registro versionado em Preferences/NVS: `homeassistant/config`. Gravação
  conferida por leitura antes de alterar a conexão ativa.
- Mudanças aplicadas no loop, sem reiniciar a placa. A página mostra status
  e contadores ao ser recarregada; não devolve a senha armazenada.
- Transporte MQTT TCP sem TLS, destinado à rede local. Credenciais são salvas
  na NVS e transmitidas conforme o protocolo MQTT sem TLS; não são exibidas
  em logs nem em métricas.

## Entidades descobertas

| Entidade | Classe/unidade | Origem |
| --- | --- | --- |
| Temperatura local | temperature / °C | DHT22 compartilhado |
| Umidade local | humidity / % | DHT22 compartilhado |
| Temperatura Porto Alegre | temperature / °C | Cache Open-Meteo |
| Umidade Porto Alegre | humidity / % | Cache Open-Meteo |
| Idade dos dados de Porto Alegre | duration / s | Última consulta externa bem-sucedida |
| Tempo ligado | duration / s | Timer monotônico de 64 bits |
| Sinal Wi-Fi | signal_strength / dBm | RSSI STA |
| Tela | switch | Liga/desliga caracteres e iluminação do LCD |
| Atualizar clima de Porto Alegre | button | Solicita consulta externa |

Ligar a tela volta à página inicial (data/hora, temperaturas e umidades local
e externa). O botão físico no GPIO 3 pode apagar/acender e alternar as duas
páginas; seu estado continua refletido no switch MQTT na próxima publicação.

As sete entidades numéricas usam `state_class: measurement`. Valores inválidos
são publicados como JSON `null`, para estado desconhecido, sem valores
fictícios. A publicação usa o cache externo; o intervalo MQTT não altera os
15 minutos das consultas climáticas. O botão solicita atualização imediata.
Alterações da tela feitas pela web/serial aparecem na publicação periódica;
comandos MQTT provocam publicação do estado após execução.

Todos os IDs incluem o MAC da placa, sem dois-pontos e em minúsculas, evitando
colisão entre vários Minis. O modelo permanece `ESP32C3Mini`.

## Tópicos e disponibilidade

`ID` tem formato `esp32c3mini_aabbccddeeff`; `BASE` é `monitorambiental/ID`.

| Tópico | Uso |
| --- | --- |
| `homeassistant/sensor/ID/CHAVE/config` | Descoberta retida dos sensores |
| `homeassistant/switch/ID/lcd/config` | Descoberta retida da tela |
| `homeassistant/button/ID/weather_refresh/config` | Descoberta retida do botão |
| `BASE/state` | JSON de sensores, publicação não retida |
| `BASE/lcd/state` | `ON`/`OFF`, estado retido |
| `BASE/lcd/set` | Comando `ON`/`OFF`, envie sem retain |
| `BASE/weather/refresh` | Comando `PRESS`, envie sem retain |
| `BASE/availability` | `online`/`offline`, retido |
| `homeassistant/status` | Mensagem de nascimento `online` provoca redescoberta |

Configura Last Will `offline` retido (QoS 1), keepalive 30 segundos e
reconexões espaçadas em 10 segundos. Publicações normais são QoS 0; sucesso
local não é confirmação de processamento pelo broker/Home Assistant.
Sensores expiram após `max(90, intervalo × 3)` segundos sem publicação.
Disponibilidade online só é anunciada após descoberta e publicação de estado.

Ao desativar ou mudar a configuração, tenta publicar offline e apagar os nove
tópicos de descoberta retidos usando a conexão antiga, se ela ainda existir.
Se o broker antigo estiver inacessível, remova os tópicos antigos manualmente
no broker; não há garantia de limpeza remota nessa situação.

## Comandos e métricas

- `ha_status`: informa desativada, aguardando Wi-Fi/broker ou conectada.
- `ha_user`: devolve o usuário MQTT salvo, sem prefixo.
- `ha_password`: devolve a senha MQTT salva em texto claro, sem prefixo.
- Credencial vazia devolve uma linha vazia. As consultas funcionam mesmo
  com integração desativada e priorizam configuração salva aguardando aplicação.
  A senha pode ser consultada por serial, UDP e comandos web; o formulário
  continua sem preencher o campo de senha e não há publicação dela em métricas.
- `ha_discovery`: agenda republicação de descoberta e estado quando conectado.
- Disponíveis na serial, UDP e no campo de comandos web.
- Prometheus passa a publicar dados reais em `mqtt_connected`,
  `mqtt_messages_sent`, `mqtt_messages_received`, `mqtt_publish_errors`.
- Contadores são voláteis, desde boot. Enviadas/falhas contam chamadas publish,
  inclusive descoberta, limpeza e disponibilidade; recebidas contam callbacks
  de mensagens, não ACKs/pings MQTT.

## Validação pendente na placa

Compile com PubSubClient instalada; teste descoberta de nove entidades,
comando da tela, botão climático, perda de Wi-Fi/broker, reinício do Home
Assistant, persistência e desativação. Atendimento é cooperativo: conexão TCP,
DNS, consulta climática e outros comandos síncronos podem atrasar o loop.
Timeout MQTT de leitura é dois segundos; não representa limite global de DNS/TCP.

Referências oficiais: [MQTT Discovery](https://www.home-assistant.io/integrations/mqtt/),
[sensores MQTT](https://www.home-assistant.io/integrations/sensor.mqtt/),
[switch MQTT](https://www.home-assistant.io/integrations/switch.mqtt/),
[PubSubClient](https://github.com/knolleary/pubsubclient).
