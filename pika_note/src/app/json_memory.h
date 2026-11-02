#pragma once
#include <ArduinoJson.h>
#if defined(ARDUINO)
#include <esp_heap_caps.h>
class PikaJsonPsramAllocator:public ArduinoJson::Allocator {
 public:void* allocate(size_t n)override{return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
 void deallocate(void* p)override{heap_caps_free(p);}
 void* reallocate(void* p,size_t n)override{return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
};
inline ArduinoJson::Allocator* pikaJsonPsram(){static PikaJsonPsramAllocator allocator;return &allocator;}
#define PIKA_JSON_DOCUMENT(name) JsonDocument name(pikaJsonPsram())
#else
#define PIKA_JSON_DOCUMENT(name) JsonDocument name
#endif
