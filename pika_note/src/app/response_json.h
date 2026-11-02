#pragma once
#include <ArduinoJson.h>

// Responses API echoes the output JSON schema inside text.format.schema.
// A plan with nested operations/fields exceeds ArduinoJson's default depth 10.
// Keep a finite bound while accepting the complete response envelope.
template <typename Input>
DeserializationError parseAiResponse(JsonDocument& document,const Input& input){
  return deserializeJson(document,input,DeserializationOption::NestingLimit(32));
}
