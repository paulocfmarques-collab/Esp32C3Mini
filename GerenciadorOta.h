#pragma once

#include "MonitorAmbiental.h"

class GerenciadorOta
{
  public:
    GerenciadorOta(MonitorAmbiental& monitor);
    void iniciar();
    void atualizar();

  private:
    MonitorAmbiental& _monitor;
};
