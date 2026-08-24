#pragma once

#include "esp_err.h"

#define ESP_RETURN_ON_ERROR(expression, tag, message, ...) \
  do {                                                     \
    (void)(tag);                                           \
    const esp_err_t result = (expression);                 \
    if (result != ESP_OK) return result;                   \
  } while (false)
