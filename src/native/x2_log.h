#ifndef X2_NATIVE_LOG_H
#define X2_NATIVE_LOG_H

#if defined(__GNUC__) || defined(__clang__)
#define X2_PRINTF_FORMAT(format_index, argument_index)                         \
  __attribute__((format(printf, format_index, argument_index)))
#else
#define X2_PRINTF_FORMAT(format_index, argument_index)
#endif

#ifdef __cplusplus
extern "C" {
#endif

void x2_log_error(const char *format, ...) X2_PRINTF_FORMAT(1, 2);
void x2_log_info(const char *format, ...) X2_PRINTF_FORMAT(1, 2);

#ifdef __cplusplus
}
#endif

#undef X2_PRINTF_FORMAT

#endif /* X2_NATIVE_LOG_H */
