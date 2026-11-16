#ifndef C_INTEROP_TYPEDEF_FN_PTR_PARAM_H
#define C_INTEROP_TYPEDEF_FN_PTR_PARAM_H

typedef int (*ctf_callback_t)(int);

static inline int ctf_apply(int value, ctf_callback_t callback) { return callback(value); }

#endif
