/*
 * error_code_builtins.h — Single source of truth for the compiler-owned
 * builtin ErrorCode variant set. Both the typechecker and codegen expand
 * this list to number the open ErrorCode enum; slot order is list order,
 * with Unknown fixed at slot 0.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_ERROR_CODE_BUILTINS_H
#define GRAYC_ERROR_CODE_BUILTINS_H

/* X(variant_name). Unknown MUST stay first (slot 0): "no error" is nil, one
 * level up; a .code that is Unknown means an unclassified real error. */
#define GRAY_ERROR_CODE_BUILTINS(X) \
    X(Unknown) \
    X(NotFound) \
    X(AlreadyExists) \
    X(PermissionDenied) \
    X(InvalidInput) \
    X(OutOfRange) \
    X(Unsupported) \
    X(Timeout) \
    X(Interrupted) \
    X(Closed) \
    X(WouldBlock) \
    X(Unavailable) \
    X(IoFailure) \
    X(ParseFailure) \
    X(EncodingFailure) \
    X(ConversionFailure) \
    X(NotAuthenticated) \
    X(ConnectionRefused) \
    X(ConnectionReset) \
    X(AddressInUse) \
    X(BrokenPipe) \
    X(WriteZero)

#endif /* GRAYC_ERROR_CODE_BUILTINS_H */
