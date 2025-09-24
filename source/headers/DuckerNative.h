#pragma once

#include <cstdint>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
    #define DUCKER_API __declspec(dllexport)
#else
    #define DUCKER_API __attribute__((visibility("default")))
#endif

typedef enum {
    UNIFORM_FLOAT,
    UNIFORM_VEC2,
    UNIFORM_VEC3,
    UNIFORM_VEC4,
    UNIFORM_INT
} UniformType;

/*
    Режимы линии, прямой (Соединение точек) и кривой
        С помощью кривых Безье
*/

enum class LineMode {
    Straight,
    Curved
};

struct Vec2 {
    float x, y;
    
    Vec2 operator-(const Vec2& other) const {
        return {x - other.x, y - other.y};
    }
};

typedef struct Vec3 { float x, y, z; } Vec3;
typedef struct Vec4 { float x, y, z, w; } Vec4;
typedef struct RectF { float x, y, w, h; } RectF;

typedef void* (*GLADloadproc)(const char* name);

DUCKER_API void DuckerNative_SetupGlad(GLADloadproc loader);

DUCKER_API void DuckerNative_Initialize(int screenWidth, int screenHeight);
DUCKER_API void DuckerNative_Shutdown();
DUCKER_API void DuckerNative_Render();
DUCKER_API void DuckerNative_SetScreenSize(int screenWidth, int screenHeight);
DUCKER_API void DuckerNative_Clear();

DUCKER_API uint32_t DuckerNative_AddRect(RectF bounds, Vec4 color, int zIndex, uint32_t textureId, RectF uvRect, float borderWidth, Vec4 borderColor);
DUCKER_API uint32_t DuckerNative_AddRoundedRect(RectF bounds, Vec2 shapeSize, Vec4 color, float cornerRadius, float blur, bool inset, int zIndex, uint32_t textureId, RectF uvRect, float borderWidth, Vec4 borderColor);
DUCKER_API uint32_t DuckerNative_AddCircle(RectF bounds, Vec4 color, float radius, float blur, bool inset, int zIndex, uint32_t textureId, float borderWidth, Vec4 borderColor);
DUCKER_API uint32_t DuckerNative_AddLine(Vec2 start, Vec2 end, Vec4 color, float width, LineMode mode, const Vec2* controls, int numControls, int zIndex);
DUCKER_API void DuckerNative_RemoveObject(uint32_t objectId);

DUCKER_API uint32_t DuckerNative_LoadFont(const char* filepath, float size);
DUCKER_API void DuckerNative_DrawText(uint32_t fontId, const char* text, Vec2 position, Vec4 color, int zIndex, float rotation, Vec2 origin);
DUCKER_API Vec2 DuckerNative_GetTextSize(uint32_t fontId, const char* text);
DUCKER_API void DuckerNative_DeleteFont(uint32_t fontId);

DUCKER_API uint32_t DuckerNative_LoadTexture(const char* filepath, int* outWidth, int* outHeight);
DUCKER_API void DuckerNative_DeleteTexture(uint32_t textureId);

DUCKER_API uint32_t DuckerNative_CreateShader(const char* fragmentShaderSource);
DUCKER_API void DuckerNative_SetObjectBorder(uint32_t objectId, float borderWidth, Vec4 borderColor);
DUCKER_API void DuckerNative_DeleteShader(uint32_t shaderId);
DUCKER_API void DuckerNative_SetObjectShader(uint32_t objectId, uint32_t shaderId);
DUCKER_API void DuckerNative_SetObjectUniform(uint32_t objectId, const char* name, UniformType type, const void* data);
DUCKER_API void DuckerNative_SetObjectCornerRadius(uint32_t objectId, float radius);
DUCKER_API void DuckerNative_SetObjectShadowColor(uint32_t objectId, Vec4 color);
DUCKER_API void DuckerNative_SetObjectRotation(uint32_t objectId, float rotation);
DUCKER_API void DuckerNative_SetObjectRotationOrigin(uint32_t objectId, Vec2 origin);
DUCKER_API void DuckerNative_SetObjectRotationAndOrigin(uint32_t objectId, float rotation, Vec2 origin);
DUCKER_API void DuckerNative_SetObjectElevation(uint32_t objectId, int elevation);

DUCKER_API void DuckerNative_BeginContainer(RectF bounds);
DUCKER_API void DuckerNative_EndContainer();


DUCKER_API void DuckerNative_SetResourcePath(const char* path);

#ifdef __cplusplus
}
#endif