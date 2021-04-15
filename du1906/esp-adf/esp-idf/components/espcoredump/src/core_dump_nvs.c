/*
 * Tests for switching between partitions: factory, OTAx, test.
 */

#include <esp_types.h>
#include <stdio.h>
#include <stdlib.h>
#include "string.h"
#include "esp_panic.h"
#include "esp_attr.h"
#include "esp_ota_ops.h"
#include "esp_core_dump_priv.h"

#include "nvs_flash.h"
#include "nvs.h"

extern uint32_t g_coredump_record_backtrace_num;

const static DRAM_ATTR char TAG[] __attribute__((unused)) = "esp_core_dump_nvs";

static int s_backtrace_offset_size = 0;

// nvs for coredump storage infomation
static char * nvs_name = "nvs_coredump";
static char * nvs_key = "key_coredump";

// nvs for length coredump infomation
static char * val_name = "nvs_len";
static char * val_key = "key_len";

#define NVS_CHECK_RETURN_VAL(x, msg, action) do{                                \
		if (x != ESP_OK) {                                                      \
			ESP_COREDUMP_LOGE("Line:%d, %s, code error:%x", __LINE__, msg, err);\
			action;                                                             \
		}                                                                       \
}while(0)

typedef struct __nvs_data_s
{
	void *ptr;
	uint32_t size;
} nvs_storage_t;

typedef struct _nvs_handle_s {
	nvs_handle handle;
	char *name;
	char *key;
	nvs_open_mode mode;

	XtExcFrame *frame;
	nvs_storage_t data;
} coredump_nvs_t;

static esp_err_t core_dump_length_storage(uint32_t len)
{
	nvs_handle my_handle;
    esp_err_t err = nvs_open(val_name, NVS_READWRITE, &my_handle);
    NVS_CHECK_RETURN_VAL(err, "nvs open failed", return ESP_FAIL);
    err = nvs_set_u32(my_handle, val_key, len);
    NVS_CHECK_RETURN_VAL(err, "nvs set u32 failed", return ESP_FAIL);
    err = nvs_commit(my_handle);
    NVS_CHECK_RETURN_VAL(err, "nvs commit failed", return ESP_FAIL);
    nvs_close(my_handle);
    return ESP_OK;
}

static esp_err_t core_dump_length_dump(uint32_t *len)
{
	nvs_handle my_handle;
    esp_err_t err = nvs_open(val_name, NVS_READWRITE, &my_handle);
    NVS_CHECK_RETURN_VAL(err, "nvs open failed", return ESP_FAIL);
    err = nvs_get_u32(my_handle, val_key, len);
    NVS_CHECK_RETURN_VAL(err, "nvs get u32 failed", return ESP_FAIL);
    nvs_close(my_handle);

    return ESP_OK;
}

/*
 * brief: Erase every nvs coredump demain
*/
static esp_err_t core_dump_context_erase(const char *name, const char *key)
{
	nvs_handle nvs;
	if (nvs_open(name, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_key(nvs, key);
        nvs_commit(nvs);
        nvs_close(nvs);
        return ESP_OK;
    }
    ESP_COREDUMP_LOGE("Erase nvs space failed");
    return ESP_FAIL;
}

// More detail see `panic.c`
static esp_err_t do_coredump_process(uint32_t pc, uint32_t sp, char *buf)
{
	if (pc & 0x80000000) {
        pc = (pc & 0x3fffffff) | 0x40000000;
    }
    
    sprintf(buf + s_backtrace_offset_size, " 0x%x", pc);
    s_backtrace_offset_size += 11;
    sprintf(buf + s_backtrace_offset_size, ":0x%x", sp);
    s_backtrace_offset_size += 11;
    ESP_COREDUMP_LOGI("@@pc:%x, sp:%x", pc, sp);
	return ESP_OK;
}

static esp_err_t esp_core_dump_nvs_storage_data(void *cfg)
{
	coredump_nvs_t* coredump = (coredump_nvs_t *)cfg;
	XtExcFrame *frame = coredump->frame;

	char *buf = (char *)coredump->data.ptr;
	uint32_t i = 0, pc = frame->pc, sp = frame->a1;
    /* Do not check sanity on first entry, PC could be smashed. */
    do_coredump_process(pc, sp, buf);
    while (i++ < 100) {
        uint32_t psp = sp;
        if (!esp_stack_ptr_is_sane(sp) || i++ > 100) {
            break;
        }
        sp = *((uint32_t *) (sp - 0x10 + 4));
        do_coredump_process(pc - 3, sp, buf); // stack frame addresses are return addresses, so subtract 3 to get the CALL address
        pc = *((uint32_t *) (psp - 0x10));
        if (pc < 0x40000000) {
	            break;
        }
    }
     int *regs = (int *)frame;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    char* task_name = pcTaskGetTaskName(task);
    char task_buf[64] = { 0 };
    snprintf(task_buf, 64, "  current task: %s, reason:%d, excvaddr:%08x", task_name, (int)frame->exccause, regs[21]);
    snprintf(buf + s_backtrace_offset_size, strlen(task_buf) + 1, "%s ", task_buf);
    ESP_COREDUMP_LOGI("coredump: %s - %d", buf, strlen(buf));
    return ESP_OK;
}

static esp_err_t esp_core_dump_nvs_write_prepare(void *cfg, uint32_t *data_len)
{
	if (*data_len <= 0 && !cfg) {
		ESP_COREDUMP_LOGE("Invalid parameters(%d)", __LINE__);
		return ESP_FAIL;
	}
	coredump_nvs_t* coredump = (coredump_nvs_t *)cfg;
	core_dump_context_erase(nvs_name, nvs_key);
	core_dump_context_erase(val_name, val_key);
	esp_err_t err = nvs_open(coredump->name, coredump->mode, &coredump->handle);
	NVS_CHECK_RETURN_VAL(err, "nvs open failed", return ESP_FAIL);
	coredump->data.size = *data_len + 100;
	coredump->data.ptr = (char *)calloc(1, coredump->data.size);
	if (!coredump->data.ptr) {
		ESP_COREDUMP_LOGI("malloc failed(%d)", __LINE__);
		return ESP_FAIL;
	} 
	esp_core_dump_nvs_storage_data((void *)coredump);
    return ESP_OK;
}

static esp_err_t esp_core_dump_nvs_write_start(void *priv)
{
	ESP_COREDUMP_LOGI("================= CORE DUMP START =================");
    return ESP_OK;
}

static esp_err_t esp_core_dump_nvs_write_end(void *pri)
{
	core_dump_write_config_t* write = (core_dump_write_config_t *)pri;

	coredump_nvs_t *coredump = (coredump_nvs_t *)write->priv;
	esp_err_t err = ESP_FAIL;
	err = nvs_commit(coredump->handle);
	NVS_CHECK_RETURN_VAL(err, "nvs_coredump end failed", return ESP_FAIL);
	nvs_close(coredump->handle);

	core_dump_length_storage(coredump->data.size);
	ESP_COREDUMP_LOGI("================= CORE DUMP END =================");
	return ESP_OK;
}

esp_err_t esp_core_dump_nvs_write_data(void *pri, void * data, uint32_t data_len)
{
	core_dump_write_config_t* write = (core_dump_write_config_t *)pri;

	coredump_nvs_t *coredump = (coredump_nvs_t *)write->priv;
	esp_err_t err = nvs_set_blob(coredump->handle, coredump->key, data, data_len);
	NVS_CHECK_RETURN_VAL(err, "nvs set blob failed", return ESP_FAIL);
	return ESP_OK;
}

esp_err_t esp_core_dump_to_nvs_process(void *priv, void *config)
{
	coredump_nvs_t* coredump = (coredump_nvs_t *)config;
	core_dump_write_config_t* write = (core_dump_write_config_t *)priv;

	enum {
		NVS_COREDUMP_START,
		NVS_COREDUMP_PREPARE,
		NVS_COREDUMP_WRITE,
		NVS_COREDUMP_END,
	};
	int state = NVS_COREDUMP_PREPARE;
	bool _run_status = true;
	ESP_COREDUMP_LOGI("g_coredump_record_backtrace_num = %d", g_coredump_record_backtrace_num);
	while (_run_status) {
		switch(state) {
			case NVS_COREDUMP_PREPARE:
				ESP_COREDUMP_LOGD("NVS_COREDUMP_PREPARE...");
				write->prepare(config, &g_coredump_record_backtrace_num);
				ESP_COREDUMP_LOGD("coredump->data.ptr: %p, coredump->data.size: %d", coredump->data.ptr, coredump->data.size);
				state = NVS_COREDUMP_START;
			case NVS_COREDUMP_START:
				ESP_COREDUMP_LOGD("NVS_COREDUMP_START...");
				write->start(priv);
				state = NVS_COREDUMP_WRITE;
			case NVS_COREDUMP_WRITE:
				ESP_COREDUMP_LOGD("NVS_COREDUMP_WRITE...");
				write->write(priv, coredump->data.ptr, coredump->data.size);
				state = NVS_COREDUMP_END;
			case NVS_COREDUMP_END:
				ESP_COREDUMP_LOGD("NVS_COREDUMP_END...");
				write->end(priv);
				_run_status = 0;
			default:
				break;
		}
	}

	ESP_COREDUMP_LOGI("Finised the coredump");
	return ESP_OK;
}

#ifdef CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
#define esp_core_dump_to_nvs esp_core_dump_to_nvs_inner
#endif // CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY

void esp_core_dump_to_nvs(XtExcFrame *frame)
{
	ESP_COREDUMP_LOGI("Enter core dump to NVS");
    esp_log_level_set("*", ESP_LOG_NONE);
	spi_flash_guard_set(&g_flash_guard_no_os_ops);

	core_dump_write_config_t wr_cfg;
	coredump_nvs_t w_nvs = {
		.name = nvs_name,
		.key = nvs_key,
		.mode = NVS_READWRITE,
		.frame = frame,
	};
	memset(&wr_cfg, 0, sizeof(wr_cfg));
    wr_cfg.prepare = esp_core_dump_nvs_write_prepare;
    wr_cfg.start = esp_core_dump_nvs_write_start;
    wr_cfg.end = esp_core_dump_nvs_write_end;
    wr_cfg.write = esp_core_dump_nvs_write_data;
    wr_cfg.priv = &w_nvs;

    esp_core_dump_to_nvs_process(&wr_cfg, &w_nvs);
}


/*
 * brief: get coredump infomation from nvs
*/
esp_err_t esp_core_dump_get_nvs_data(char **buf)
{
	nvs_handle my_handle;
	esp_err_t err = nvs_open(nvs_name, NVS_READWRITE, &my_handle);
	if (err != ESP_OK) {
		ESP_COREDUMP_LOGE("nvs open failed, error code: %0xx(%d)", err, __LINE__);
		return ESP_FAIL;
	}
	uint32_t len;
	core_dump_length_dump(&len);
	ESP_COREDUMP_LOGI("get coredump size is %d", len);
	char *storage = (char *)calloc(1, len+1);
	err = nvs_get_blob(my_handle, nvs_key, storage, &len);
	NVS_CHECK_RETURN_VAL(err, "nvs set blob failed", return ESP_FAIL);
	
	nvs_close(my_handle);
	storage[len] = '\0';
	*buf = storage;
	return ESP_OK;
}


