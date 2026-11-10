// Host stand-in for vitashark.h (the real one pulls the shacccg headers): only what gles.c uses.
#pragma once
#include <stdint.h>
// no psp2/gxm.h: it clashes with the host vitaGL.h macros; gles.c ignores the result
typedef enum shark_type { SHARK_VERTEX_SHADER, SHARK_FRAGMENT_SHADER } shark_type;
typedef enum shark_log_level { SHARK_LOG_INFO, SHARK_LOG_WARNING, SHARK_LOG_ERROR } shark_log_level;
typedef enum shark_warn_level { SHARK_WARN_SILENT, SHARK_WARN_LOW, SHARK_WARN_MEDIUM, SHARK_WARN_HIGH, SHARK_WARN_MAX } shark_warn_level;
void shark_install_log_cb(void (*cb)(const char *msg, shark_log_level msg_level, int line));
void shark_set_warnings_level(shark_warn_level level);
void *shark_compile_shader(const char *src, uint32_t *size, shark_type type);
