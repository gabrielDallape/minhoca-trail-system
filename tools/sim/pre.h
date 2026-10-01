#pragma once
// Tudo que e GLOBAL, incluido ANTES de abrir o namespace do no: os headers do
// sistema e os falsos (fake/). Assim, quando o firmware os inclui de dentro do
// namespace, o #pragma once / include guard os pula - e o que fica por no e so o
// que node_env.h define.
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdarg>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include <filesystem>
#include "Arduino.h"
#include "Preferences.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_rom_sys.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "ar.h"
#include "no_api.h"
