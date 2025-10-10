#ifndef FAAS_COMPILER_H
#define FAAS_COMPILER_H

// Extract a field value from JSON string
// Parameters:
//   json - JSON string to parse
//   field - Field name to extract (with quotes, e.g., "\"method\"")
// Returns:
//   Allocated string with the value (caller must free), or NULL if not found
char* extract_json_field(const char* json, const char* field);

// Compile a function from /tmp/progfile to WASM
// Parameters:
//   uuid - Unique identifier for the function
// Returns:
//   0 on success
//   Non-zero error code on failure
int compile_function(const char* uuid);

#endif

