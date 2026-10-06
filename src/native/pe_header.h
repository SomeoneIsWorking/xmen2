#ifndef X2_PE_HEADER_H
#define X2_PE_HEADER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Check the on-disk header accepted by the x86 PE loader.  This deliberately
 * does not map or execute the image; install selection uses it to refuse a
 * corrupt or non-x86 file before promoting it into private storage. */
int x2_pe32_validate_file(const char *path, char *reason,
                          unsigned reason_capacity);

#ifdef __cplusplus
}
#endif

#endif /* X2_PE_HEADER_H */
