#include "CrashDiagnostics.h"
#include <esp_core_dump.h>

String CrashDiagnostics::lerEApagar(const String& motivoReset)
{
    String resultado = "Ultimo reset: " + motivoReset + "\n";
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
    esp_err_t erro = esp_core_dump_image_check();
    if (erro != ESP_OK)
        return resultado + "Sem core dump valido: " + esp_err_to_name(erro) + "\n";

    char motivo[200] = {};
    erro = esp_core_dump_get_panic_reason(motivo, sizeof(motivo));
    if (erro == ESP_OK)
        resultado += "Panic salvo: " + String(motivo) + "\n";
    else
        resultado += "Motivo do panic indisponivel: " + String(esp_err_to_name(erro)) + "\n";

    esp_core_dump_summary_t resumo = {};
    erro = esp_core_dump_get_summary(&resumo);
    if (erro != ESP_OK)
        return resultado + "Falha ao ler resumo: " + esp_err_to_name(erro) +
               "\nRegistro interno preservado.\n";

    resultado += "Tarefa: ";
    for (size_t i = 0; i < sizeof(resumo.exc_task) && resumo.exc_task[i]; ++i)
        resultado += resumo.exc_task[i];
    char endereco[32];
    snprintf(endereco, sizeof(endereco), "\nPC: 0x%08lx\n", (unsigned long)resumo.exc_pc);
    resultado += endereco;
    resultado += "ELF SHA256: ";
    for (size_t i = 0; i < sizeof(resumo.app_elf_sha256) && resumo.app_elf_sha256[i]; ++i)
        resultado += static_cast<char>(resumo.app_elf_sha256[i]);
    resultado += "\n";

    erro = esp_core_dump_image_erase();
    resultado += erro == ESP_OK ? "Registro de crash apagado apos leitura do resumo.\n" :
        "Falha ao apagar registro de crash: " + String(esp_err_to_name(erro)) + "\n";
#else
    resultado += "Core dump ELF em flash nao habilitado neste firmware. Registro nao apagado.\n";
#endif
    return resultado;
}
