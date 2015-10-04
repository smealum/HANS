#ifndef PATCH_H
#define PATCH_H

typedef struct
{
    u32 start, end;
}function_s;


typedef bool (*validationCallback_t)(u32* code_data32, u32 code_size32, function_s candidate, u32 ref);

function_s findFunction(u32* code_data32, u32 code_size32, u32 start);
function_s findPooledCommandFunction(u8* code_data, u32 code_size, u32 pooled_value);
function_s findFunctionReferenceFunction(u8* code_data, u32 code_size, function_s ref, validationCallback_t callback);

#endif
