#include "DisplayAmbiental.h"
#include "Log.h"
#include <math.h>

DisplayAmbiental::DisplayAmbiental() : lcd(0x27, 20, 4)
{
}

void DisplayAmbiental::iniciar()
{
    LOG_INFO("Iniciando display");
    lcd.init();
    invalidarLinhas();
    definirLigada(true);
    uint8_t icones[7][8] = {
        {0x04, 0x0A, 0x0A, 0x0E, 0x0E, 0x1F, 0x1F, 0x0E},
        {0x04, 0x04, 0x0A, 0x0A, 0x11, 0x11, 0x1F, 0x0E},
        {0x1F, 0x00, 0x0E, 0x00, 0x04, 0x00, 0x04, 0x00},
        {0x00, 0x04, 0x02, 0x1F, 0x02, 0x04, 0x00, 0x00},
        {0x04, 0x0E, 0x15, 0x04, 0x04, 0x04, 0x04, 0x00},
        {0x00, 0x04, 0x04, 0x04, 0x04, 0x15, 0x0E, 0x04},
        {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}};
    for (uint8_t i = 0; i < 7; ++i)
        lcd.createChar(i + 1, icones[i]);
    layoutDesenhado = false;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("====================");
    lcd.setCursor(0, 1);
    lcd.print("  MONITOR AMBIENTAL ");
    lcd.setCursor(0, 2);
    lcd.print("   CONECTANDO REDE  ");
    lcd.setCursor(0, 3);
    lcd.print("[                  ]");
}

void DisplayAmbiental::definirLigada(bool ativa)
{
    ligada = ativa;
    if (ligada)
    {
        lcd.display();
        lcd.backlight();
    }
    else
    {
        lcd.noDisplay();
        lcd.noBacklight();
    }
}

void DisplayAmbiental::invalidarLinhas()
{
    for (uint8_t i = 0; i < 4; ++i) linhasAnteriores[i] = "\x01";
}

void DisplayAmbiental::escreverLinha(uint8_t linha, String texto)
{
    texto = texto.substring(0, 20);
    while (texto.length() < 20) texto += ' ';
    if (linhasAnteriores[linha] == texto) return;
    lcd.setCursor(0, linha);
    lcd.print(texto);
    linhasAnteriores[linha] = texto;
}

void DisplayAmbiental::mostrarPagina(uint8_t pagina, const String& dataHora,
                                    const LeituraAmbiental& local, float temperaturaExterna,
                                    float umidadeExterna, const String& ip, const String& ssid,
                                    int rssi, bool conectado, bool localEmCache)
{
    if (!ligada) return;
    auto coluna = [](String texto)
    {
        texto = texto.substring(0, 10);
        while (texto.length() < 10) texto += ' ';
        return texto;
    };
    auto temperatura = [](float valor)
    {
        String texto = isfinite(valor) ? String(valor, 1) : String("--");
        texto += char(223);
        return texto + "C";
    };
    if (pagina == 0)
    {
        // Slots CGRAM 1 e 2, carregados em iniciar(): termômetro e gota.
        const String termometro = String(char(1)) + " ";
        const String umidade = String(char(2)) + " ";
        escreverLinha(0, dataHora);
        escreverLinha(1, coluna(localEmCache ? "LOCAL*" : "LOCAL") + "POA");
        escreverLinha(2, coluna(termometro + temperatura(local.temperatura)) + coluna(termometro + temperatura(temperaturaExterna)));
        escreverLinha(3, coluna(umidade + (isfinite(local.umidade) ? String(local.umidade, 0) : String("--")) + "%") +
                         coluna(umidade + (isfinite(umidadeExterna) ? String(umidadeExterna, 0) : String("--")) + "%"));
    }
    else
    {
        escreverLinha(0, conectado ? "REDE: CONECTADA" : "REDE: SEM CONEXAO");
        escreverLinha(1, "IP: " + (ip.isEmpty() || ip == "0.0.0.0" ? String("indisponivel") : ip));
        escreverLinha(2, "WiFi: " + (ssid.isEmpty() ? String("Portal AP") : ssid));
        escreverLinha(3, conectado ? "Sinal: " + String(rssi) + " dBm" : String("Sem sinal STA"));
    }
}

void DisplayAmbiental::exibirProgressoConexao(uint8_t passo)
{
    if (passo > 18)
        passo = 18;
    if (passo == 0)
    {
        lcd.setCursor(0, 3);
        lcd.print("[                  ]");
    }
    if (passo < 18)
    {
        lcd.setCursor(1 + passo, 3);
        lcd.write(7);
    }
}

void DisplayAmbiental::mostrarLeituraComDadosExternos(const LeituraAmbiental& leitura,
                                                     int tendencia, float temperaturaExterna,
                                                     float umidadeExterna)
{
    if (!layoutDesenhado)
    {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("[ LOCAL ]  [ POA ]");
        layoutDesenhado = true;
    }
    lcd.setCursor(0, 1);
    lcd.print("                    ");
    lcd.setCursor(0, 1);
    lcd.write(1);
    lcd.print("T:");
    lcd.print(leitura.temperatura, 1);
    lcd.write(223);
    if (tendencia > 0)
        lcd.write(5);
    else if (tendencia < 0)
        lcd.write(6);
    lcd.setCursor(11, 1);
    lcd.write(1);
    lcd.print("T:");
    lcd.print(isfinite(temperaturaExterna) ? String(temperaturaExterna, 1) : "--");
    lcd.write(223);

    lcd.setCursor(0, 2);
    lcd.print("                    ");
    lcd.setCursor(0, 2);
    lcd.write(2);
    lcd.print("U:");
    lcd.print(leitura.umidade, 0);
    lcd.print("%");
    lcd.setCursor(11, 2);
    lcd.write(2);
    lcd.print("U:");
    lcd.print(isfinite(umidadeExterna) ? String(umidadeExterna, 0) : "--");
    lcd.print("%");
}

void DisplayAmbiental::mostrarLeituraComTendencia(const LeituraAmbiental& leitura, int tendencia)
{
    mostrarLeituraComDadosExternos(leitura, tendencia, NAN, NAN);
}

void DisplayAmbiental::mostrarLeitura(const LeituraAmbiental& leitura)
{
    mostrarLeituraComTendencia(leitura, 0);
}

void DisplayAmbiental::iniciarAtualizacaoOta()
{
    invalidarLinhas();
    layoutDesenhado = false;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("====================");
    lcd.setCursor(0, 1);
    lcd.print(" GRAVANDO FIRMWARE  ");
    lcd.setCursor(0, 2);
    lcd.print("[                  ]");
    mostrarProgressoOta(0);
}

void DisplayAmbiental::mostrarProgressoOta(uint8_t porcentagem)
{
    if (porcentagem > 100)
        porcentagem = 100;
    lcd.setCursor(7, 3);
    lcd.printf(" %3u%% ", static_cast<unsigned int>(porcentagem));
    uint8_t blocos = (porcentagem * 18) / 100;
    for (uint8_t i = 0; i < 18; ++i)
    {
        lcd.setCursor(1 + i, 2);
        if (i < blocos)
            lcd.write(7);
        else
            lcd.print(" ");
    }
}

void DisplayAmbiental::atualizarLinhaStatus(const String& texto)
{
    lcd.setCursor(0, 3);
    lcd.print("                    ");
    lcd.setCursor(0, 3);
    lcd.write(4);
    lcd.print(" ");
    uint8_t colunaTexto = 2;
    if (texto != "PORTAL AP" && texto != "SINC. NTP" && texto != "CONECTANDO")
    {
        lcd.write(3);
        lcd.print(" ");
        colunaTexto = 4;
    }
    lcd.print(texto.substring(0, 20 - colunaTexto));
}
