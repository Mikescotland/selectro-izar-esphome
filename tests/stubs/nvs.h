#pragma once
#include <cstdint>
using esp_err_t=int;static constexpr int ESP_OK=0;static constexpr int ESP_ERR_NVS_NOT_FOUND=2;inline esp_err_t nvs_erase_key(uint32_t,const char*){return ESP_OK;} inline esp_err_t nvs_commit(uint32_t){return ESP_OK;}
