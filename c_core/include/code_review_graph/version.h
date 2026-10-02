#ifndef CRG_C_CORE_VERSION_H
#define CRG_C_CORE_VERSION_H

#include "code_review_graph/c_core_export.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NUL-terminated static string; never NULL. Borrowed by the caller. */
CRG_API const char *crg_c_core_version(void);

#ifdef __cplusplus
}
#endif

#endif /* CRG_C_CORE_VERSION_H */
