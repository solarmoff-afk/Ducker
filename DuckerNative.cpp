/*
    Ducker Native - переписанная с нуля версия движка Ducker
    который идёт в составе фрейморвка Luvix.

    Основные функции:
        - Рисование прямоугольников и кругов
        - Текстурирование (В том числе и control режим)
            - (Через stb_image)
        - Поддержка теней
        - Поддержка компиляции своих шейдеров
        - Контейнеры
        - Тексты и загрузка шрифтов через stb_true_type

    Лицезия: GPL v3

    Требования:
        - C++17
        - OpenGL 3.1

    @author Update Developer
    @version 1.1
*/

#include "Headers/DuckerNative.h"
#include "Headers/fast_vector.h"

#ifdef __ANDROID__
#include <GLES3/gl3.h>
#else
#include <glad/glad.h>
#endif

#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

#include <iostream>

#define STB_TRUETYPE_IMPLEMENTATION
#include "Headers/stb_truetype.h"

#define STB_IMAGE_IMPLEMENTATION
#include "Headers/stb_image.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

/*
    Используется для передачи в вершинный шейдер

    Мы не будем использовать GLM чтобы не тянуть лишние зависимости,
    вместо этого - мы используем свою структуру для матриц проекции и модели.
*/

struct mat4 { 
    float m[4][4]; 
};

/*
    Загаловок для функции создания матрицы модели    
*/

mat4 CreateRotationMatrix(float angle, Vec2 origin, RectF bounds);

/*
    Все существующие типы объектов
        1. Rect - Стандартный прямоугольник, который состоит из
            4 вершин и 4 граней 
        
        2. RoundedRect - Прямоугольник, но с закруглением углов
            через отдельный SDF шейдер
        
        3. Circle - Круг, также рендерится через шейдер. Это
            позволяет не загружать оперативную память и видеокарту
            вершинами (Если создавать настоящий круг через вершины и грани)
        
        4. Glyph - Символ из текста

        5. Line - Путь от точке к точке с возможность
            добавить дополнительные точки между началом и концом.
            Поддерживает как прямоу режим так и кривой (S образный)
*/

enum class ObjectType { 
    Rect, 
    RoundedRect, 
    Circle, 
    Glyph,
    Line
};

/*
    Структура вершины, которая используется в шейдерах

    Поля:
        1. pos - Мировые координаты вершины
        2. textUv - Текстурные координаты
        3. geomUv - Дополнительные координаты для геометрических эффектов,
                    например, для скругоения углов
*/

struct Vertex { 
    Vec2 pos; 
    Vec2 texUv; 
    Vec2 geomUv; 
};

/*
    Шейдерная программа - представляет из себя:
        1. Вершинный шейдер, шейдер, который определяет позицию
            точек объекта. В нашем случае применяет к точкам (вершинам)
            матрицу проекции и модели. Матрица обзора нам не требуется
            поскольку мы не исполбзуем 3D пространство,
            однако в случае расширения движка потребуется
            матрица вида для реализации камеры

        2. Фрагментный шейдер, шейдер, который отвечает за ЦВЕТ пикселыя
            в нашем случае это:
                - Цвет объекта
                - Прозрачный фон у текста и наложение самого текста
                - Наложение текстуры
                - Тени
                - Отсечение углов у RoundedRect или Circle

    После линковки (Соединения) у нас есть идентификатор  шейдерной программы
        в памяти и мы можем применить её к объекту
*/

struct ShaderProgram { 
    GLuint id = 0; 
};

/*
    Uniform (Юниформ) - значение, которое передаётся в шейдер
        из нашей основной программы.

    Используется для:
        - Цвета
        - Блюра (Теней)
        - Передачи матрицы модели и проекции
        - Передачи текстуры
        - Передачи скругления углов
*/

struct UniformValue { 
    UniformType type; 
    fast_vector<char> data; 
};

/*
    @size - Размер букв шрифта, например: 16, 24, 36
    @char_data - Информация про символы
    @textureId - Наш атлас превращается в текстуру с каждым символом 
                и через него будет строится текст. Это идентификатор 
                этой текстуры в памяти
    @atlasWidth - Ширина этой текстуры (Она называется атлас)
    @atlasHeight - Высота этой текстуры (Она называется атлас) 
*/

struct Font {
    float size;
    stbtt_packedchar char_data[96 + 256];
    GLuint textureId;
    int atlasWidth;
    int atlasHeight;
};

/*
    Параметры для слоя тени в Material Design 3
*/

struct ShadowLayer {
    float opacity;
    float yOffset;
    float blurRadius;
    float spread;
};

/*
    Свойства объекта

    @id - Идентификатор объекта в памяти
    @type - Тип объекта (rect, roundedRect, circle, glyph)
    @visible - Видимый ли объект? True/false
    
    @zIndex - Слой нашего объекта
            определяет в каком порядке будут нарисованы объекты

            Пример:
                - Объекта A:
                    zIndex = 1

                - Объект B:
                    zIndex = 2

            Первым нарисуется объект A, а объект B будет его перекрывать

            Если же мы сделаем наоборот (Объект A имеет zIndex = 2, а объект
                B имеет zIndex = 1) - первым нарисуется объект A

    @bounds - Определяет позицию и размер объекта
    @color - Три цвета в формате RGBA
            1. Красный (От 0 до 1, где 0 это 0, а 1 это 255)
            1. Зелёный (От 0 до 1, где 0 это 0, а 1 это 255)
            1. Синий (От 0 до 1, где 0 это 0, а 1 это 255)
            1. Прозрачность, насколько объект
                просвеичвает другие объекты.
                (От 0 до 1, где 0 это 0, а 1 это 255)

    @textureId, shaderId - Текстура и шейдерная программа, которые
        используют объект. Можно менять в реальном времени.

    @scissorRect - Область оберзки
    @uvRect - Перемещение текстуры в режиме контроля
            1. X координата текстуры
            2. Y координата текстуры
            3. Ширина текстуры
            4. Высоты текстуры

    @uniforms - Юниформы объекта которые передатются в шейдерную программу
        обхекта

    @elevation - Уровень возвышения объекта для Material 3 теней (0 - без теней)
*/

struct RenderObject {
    uint32_t id;
    ObjectType type;
    bool visible = true;
    int zIndex = 0;
    
    RectF bounds;
    Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    
    uint32_t textureId = 0;
    uint32_t shaderId = 0;
    
    RectF scissorRect;
    RectF uvRect = {0.0f, 0.0f, 1.0f, 1.0f};

    float borderWidth = 0.0f;
    Vec4 borderColor = {0.0f, 0.0f, 0.0f, 0.0f};

    std::map<std::string, UniformValue> uniforms;

    int elevation = 0;

    float rotation = 0.0f;
    Vec2 rotationOrigin = {0.5f, 0.5f};

    /*
        Эти параметры нужны только для линий,
            в случае других объектов они не используются.
    */

    Vec2 start;
    Vec2 end;
    fast_vector<Vec2> controlPoints;
    float lineWidth = 1.0f;
    LineMode lineMode = LineMode::Straight;
    int triCount = 2;
};

/*
    Состояние рендера. Определяет значения, которые использует весь
        рендер (Отрисовка объектов на экран)

    @screenWidth - Ширина экрана
    @screenHeight - Высота экрана

    Все объекты используют один VAO и VBO ибо все состоят из 4 вершин и 4 граней,
        каждой вершине нужен 1 атрибутов. Все сложные формы (Прямоугольник
        с закруглением, круг, глиф) достигаются с помощью шейдеров

        @vao - Индекс буфера атрибутов вершин в памяти
        @vbo - Буфер самих вершин в памяити
    
    @projectionMatrix - Матрица 4 на 4 которая преобразует мировые координаты 
        в экранные, а также определяет область видимости. В Ducker Native
        использует ортографическую проекцию.

        Отвечает за:
            - Масштабирование (Как мировые координаты относятся к пикселям экрана)
            - Отсечение (что находится за пределами экрана и не должно рендериться)
            - Сдвиг

        Как это работает:
            Левый нижний угол = (left, bottom)
            Правый верхний угол = (right, top)

            Все объекты внутри этих границ отображаются на экране
            Z-координата игнорируется (если не используется для слоёв)

    @nextCustomShaderId - Следующий индекс кастомного шейдера в памяти
    @shaders - Карта, где хранятся шейдерные программы
        которые использует рендер

    @nextFontId - Следубщий ID шрифта в памяти. Когда шрифт будет загружен
        в оперативную память - он займёт это место, а потом к нему прибавиться
        1 и уже следубщий шрифт займёт это место.
        
        Пример:
            - Шрифт inter.ttf
            - Шрифтов сейчас 0, следующий шрифт: 1
            - Загружаем inter.ttf
            - Шрифтов сейчас 1, следующий шрифт 2
            - Наш шрифт занял ID - 1

    @objects, @objectsIdToIndex, @objectsId - Карта объектов
        и следубщий ID для вставки в карту. Как и обычно.

    @needsSort - Значение, которое определяет нужна ли сортировка по
        zIndex (Слою) объектов. Если да - проводится
        сортировка и объекты начинают рисоваться в правильном
        в порядке порядке на основе Z координаты

    @scissorStack, @containerStack - Стэки для областей отсечения и
        контейнеров. Используем fast_vector вместо std::vector так
        как он показал свою высокую производительность и надёжность.
        
        Он требует стандарта С++17

        Замеры скорости из репозитория fast_vector:
            - Hardware: Intel® Core™ i7-4720HQ CPU, 8GB DDR3 Dual-channel memory
            - Enviroment: Visual Studio 2017, Windows 10 Pro 64-bit
            - Data size: 10,000 items
        
        Benchmark                                Time             CPU   Iterations
        --------------------------------------------------------------------------
        push_back | std::vector              20126 ns        19950 ns        34462
        push_back | fast_vector               9392 ns         9417 ns        89600
        --------------------------------------------------------------------------
        reserve & push_back | std::vector    12991 ns        13114 ns        56000
        reserve & push_back | fast_vector     6416 ns         6417 ns       112000
        --------------------------------------------------------------------------
        push_back & pop_back | std::vector   12704 ns        12835 ns        56000
        push_back & pop_back | fast_vector    9253 ns         9242 ns        89600

    @shadowFBO, @shadowTexture - Фреймбуфер и текстура для рендеринга теней
    @intermediateFBO, @intermediateTexture - Промежуточный фреймбуфер для двухпроходного блюра
    @blurHorizontal, @blurVertical - Шейдерные программы для горизонтального и вертикального проходов гауссова блюра
    @quadVAO, @quadVBO - VAO и VBO для полноэкранного квадрата (для пост-процессинга)
    @shadowPresets - Предустановленные параметры теней для уровней возвышения Material Design 3
*/

struct RendererState {
    int screenWidth = 0;
    int screenHeight = 0;
    
    GLuint vao = 0;
    GLuint vbo = 0;
    
    mat4 projectionMatrix;
    
    uint32_t nextCustomShaderId = 100;
    std::map<uint32_t, ShaderProgram> shaders;
    
    uint32_t nextFontId = 1;
    std::map<uint32_t, Font> fonts;
    
    fast_vector<RenderObject> objects;
    std::map<uint32_t, size_t> objectIdToIndex;
    uint32_t nextObjectId = 1;
    
    bool needsSort = false;
    
    fast_vector<RectF> scissorStack;
    fast_vector<Vec2> containerStack;

    GLuint shadowFBO = 0;
    GLuint shadowTexture = 0;
    GLuint intermediateFBO = 0;
    GLuint intermediateTexture = 0;

    ShaderProgram blurHorizontal;
    ShaderProgram blurVertical;

    GLuint quadVAO = 0;
    GLuint quadVBO = 0;

    std::map<int, std::vector<ShadowLayer>> shadowPresets;
};

static RendererState* state = nullptr;

#ifdef __ANDROID__
#define SHADER_VERSION "#version 300 es\nprecision mediump float;\n"
#define OUT_FRAG "out vec4 FragColor;\n"
#define TEXTURE_FUNC "texture"
#else
#define SHADER_VERSION "#version 140\n"
#define OUT_FRAG "out vec4 outColor;\n"
#define TEXTURE_FUNC "texture"
#endif

/*
    Универсальный для всех объектов вершинный шейдер, он
        написан на языке GLSL для версии OpenGL 3.1

    Умножает матрицу проекции на позицию вершины и задаёт:
        1. UV координаты для текстурирования
        2. GEOM_UV для геометрии

    Возвращаем позицию в 4D векторе поскольку нам нужны матрицы 4ч4,
        поскольку у нас нет 4 координаты - мы задаём её как 1.0,
        а Z координату как 0.0, ибо у нас её нет    
*/

const char* UNIVERSAL_VS_SRC = SHADER_VERSION R"(
in vec2 aPos;
in vec2 aTexUv;
in vec2 aGeomUv;

uniform mat4 projection;
uniform mat4 model;

out vec2 v_tex_uv;
out vec2 v_geom_uv;

void main() {
    gl_Position = projection * model * vec4(aPos, 0.0, 1.0);
    v_tex_uv = aTexUv;
    v_geom_uv = aGeomUv;
})";

/*
    Вершинный шейдер для полноэкранного квадрата (для пост-процессинга)
*/

const char* QUAD_VS_SRC = SHADER_VERSION R"(
in vec2 aPos;
in vec2 aTexUv;

out vec2 v_tex_uv;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    v_tex_uv = aTexUv;
})";

/*
    Фрагментный шейдер для горизонтального прохода гауссова блюра

    Как работает:
        - Это настоящий двухпроходный гауссов блюр, который применяет конволюцию с гауссовым ядром.
        - Горизонтальный проход сэмплирует пиксели по горизонтали с весами из гауссовой функции.
        - Ядро симметрично, поэтому сэмплируем центр + положительные/отрицательные offsets.
        - Weights - нормализованные веса гауссовой функции: exp(-x^2 / (2*sigma^2)) / (sqrt(2*pi)*sigma)
        - HalfKernel - половина размера ядра (например, 7 для размера 15)
        - PixelSize - 1.0 / ширине текстуры для offsets
        - Это создает аутентичный размытый эффект, подходящий для настоящих Material 3 теней.
        - Для Material 3: Каждый слой тени рендерится sharp в FBO, затем блюрится с конкретным radius (sigma = radius / 3 approx), композитуется с offset и opacity.
*/

const char* HORIZONTAL_BLUR_FS_SRC = SHADER_VERSION OUT_FRAG
"in vec2 v_tex_uv;\n"
"uniform sampler2D tex;\n"
"uniform float weights[16];\n"
"uniform int halfKernel;\n"
"uniform float pixelSize;\n"
"void main() {\n"
"    vec4 color = " TEXTURE_FUNC "(tex, v_tex_uv) * weights[0];\n"
"    for(int i = 1; i <= halfKernel; i++) {\n"
"        color += " TEXTURE_FUNC "(tex, v_tex_uv + vec2(float(i) * pixelSize, 0.0)) * weights[i];\n"
"        color += " TEXTURE_FUNC "(tex, v_tex_uv - vec2(float(i) * pixelSize, 0.0)) * weights[i];\n"
"    }\n"
#ifdef __ANDROID__
"    FragColor = color;\n"
#else
"    outColor = color;\n"
#endif
"}";

/*
    Фрагментный шейдер для вертикального прохода гауссова блюра
    Аналогично горизонтальному, но сэмплирует по вертикали.
*/

const char* VERTICAL_BLUR_FS_SRC = SHADER_VERSION OUT_FRAG
"in vec2 v_tex_uv;\n"
"uniform sampler2D tex;\n"
"uniform float weights[16];\n"
"uniform int halfKernel;\n"
"uniform float pixelSize;\n"
"void main() {\n"
"    vec4 color = " TEXTURE_FUNC "(tex, v_tex_uv) * weights[0];\n"
"    for(int i = 1; i <= halfKernel; i++) {\n"
"        color += " TEXTURE_FUNC "(tex, v_tex_uv + vec2(0.0, float(i) * pixelSize)) * weights[i];\n"
"        color += " TEXTURE_FUNC "(tex, v_tex_uv - vec2(0.0, float(i) * pixelSize)) * weights[i];\n"
"    }\n"
#ifdef __ANDROID__
"    FragColor = color;\n"
#else
"    outColor = color;\n"
#endif
"}";

/*
    Простейший фрагментный шейдер для простого прямоугольника,
    определяет цвет и текстуру (Sampler2d)
*/

const char* RECT_FS_SRC = SHADER_VERSION OUT_FRAG
"in vec2 v_tex_uv;\n"
"uniform vec4 objectColor;\n"
"uniform sampler2D objectTexture;\n"
"uniform bool useTexture;\n"
"void main() {\n"
"    vec4 resultColor;\n" // Временная переменная для унификации
"    if (useTexture) {\n"
"        resultColor = " TEXTURE_FUNC "(objectTexture, v_tex_uv) * objectColor;\n"
"    } else {\n"
"        resultColor = objectColor;\n"
"    }\n"
#ifdef __ANDROID__
"    FragColor = resultColor;\n"
#else
"    outColor = resultColor;\n"
#endif
"}";

/*
    Фрагментный шейдер для отрисовки скругленных прямоугольников (Rounded Rect)

    Особенности:
        - Поддержка текстуры или однотонного цвета
        - Настраиваемый радиус скругления углов
        - Эффект размытия границ (blur)
        - Режим "внутренней" тени (inset)
        - Антиалиасинг границ с помощью SDF (Signed Distance Field)
 
    Входные данные:
        - v_geom_uv: UV-координаты геометрии от 0 до 1
        - v_tex_uv: UV-координаты текстуры от 0 до 1

    Uniform-переменные:
        - objectColor: основной цвет (если не используется текстура)
        - objectTexture: текстура объекта
        - useTexture: флаг использования текстуры
        - quadSize: физический размер квада в пикселях
        - shapeSize: размер прямоугольника без учета скругления
        - cornerRadius: радиус скругления углов
        - blur: степень размытия границ (0 - резкие границы)
        - inset: флаг "внутреннего" эффекта (для теней/свечений)
 */

 const char* ROUNDED_RECT_FS_SRC = SHADER_VERSION OUT_FRAG
 "in vec2 v_geom_uv;\n"
 "in vec2 v_tex_uv;\n"
 "uniform vec4 objectColor;\n"
 "uniform sampler2D objectTexture;\n"
 "uniform bool useTexture;\n"
 "uniform vec2 quadSize;\n"
 "uniform vec2 shapeSize;\n"
 "uniform float cornerRadius;\n"
 "uniform float blur;\n"
 "uniform bool inset;\n"
 "uniform float borderWidth;\n"
 "uniform vec4 borderColor;\n"
 "uniform float spread;\n"
 
 "float sdfRoundedBox(vec2 p, vec2 b, float r) {\n"
 "    vec2 q = abs(p) - b + vec2(r);\n"
 "    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;\n"
 "}\n"
 
 "void main() {\n"
 "    vec4 baseColor = useTexture ? texture(objectTexture, v_tex_uv) : objectColor;\n"
 "    vec2 p = (v_geom_uv - 0.5) * quadSize;\n"
 "    float dist = sdfRoundedBox(p, shapeSize * 0.5, cornerRadius);\n"
 "    \n"
 "    float alpha;\n"
 "    vec4 finalColor = baseColor;\n"
 "    \n"
 "    if (borderWidth > 0.0) {\n"
 "        float innerDist = sdfRoundedBox(p, shapeSize * 0.5 - borderWidth, max(0.0, cornerRadius - borderWidth));\n"
 "        \n"
 "        float edgeSoftness = max(0.5, fwidth(dist));\n"
 "        float innerEdgeSoftness = max(0.5, fwidth(innerDist));\n"
 "        \n"
 "        float outerAlpha = smoothstep(-edgeSoftness, edgeSoftness, -dist);\n"
 "        float innerAlpha = smoothstep(-innerEdgeSoftness, innerEdgeSoftness, -innerDist);\n"
 "        alpha = outerAlpha - innerAlpha;\n"
 "        finalColor = borderColor;\n"
 "        \n"
 "        if (blur > 0.0) {\n"
 "            if (inset) {\n"
 "                alpha = smoothstep(blur, 0.0, alpha);\n"
 "            } else {\n"
 "                alpha = 1.0 - smoothstep(0.0, blur, 1.0 - alpha);\n"
 "            }\n"
 "        }\n"
 "    } else {\n"
 "        if (blur > 0.0) {\n"
 "            float effective_dist = dist - spread;\n"
 "            \n"
 "            if (inset) {\n"
 "                alpha = smoothstep(blur, 0.0, -effective_dist);\n"
 "            } else {\n"
 "                float falloff_multiplier = 6.0;\n"
 "                alpha = exp(-pow(max(0.0, effective_dist), 2.0) * falloff_multiplier / blur);\n"
 "            }\n"
 "        } else {\n"
 "            float edgeSoftness = max(0.5, fwidth(dist));\n"
 "            alpha = smoothstep(-edgeSoftness, edgeSoftness, -dist);\n"
 "        }\n"
 "    }\n"
 "    \n"
 #ifdef ANDROID
 "    FragColor = vec4(finalColor.rgb, finalColor.a * alpha);\n"
 #else
 "    outColor = vec4(finalColor.rgb, finalColor.a * alpha);\n"
 #endif
 "    if ("
 #ifdef ANDROID
 "        FragColor.a < 0.005) {\n"
 #else
 "        outColor.a < 0.005) {\n"
 #endif
 "        discard;\n"
 "    }\n"
 "}";

/*
    Фрагментный шейдер для отрисовки кругов (Circle)

    Особенности:
        - Поддержка текстуры или однотонного цвета
        - Идеально гладкие границы (SDF)
        - Настраиваемый радиус
        - Эффект размытия границ
        - Режим "внутренней" тени (inset) 
    
    Входные данные:
        - v_geom_uv: UV-координаты геометрии от 0 до 1
        - v_tex_uv: UV-координаты текстуры от 0 до 1

    Uniform-переменные:
        - objectColor: основной цвет (если не используется текстура)
        - objectTexture: текстура объекта
        - useTexture: флаг использования текстуры
        - quadSize: физический размер квада в пикселях
        - shapeRadius: радиус круга
        - blur: степень размытия границ
        - inset: флаг "внутреннего" эффекта
 */

 const char* CIRCLE_FS_SRC = SHADER_VERSION OUT_FRAG
"in vec2 v_geom_uv;\n"
"in vec2 v_tex_uv;\n"
"uniform vec4 objectColor;\n"
"uniform sampler2D objectTexture;\n"
"uniform bool useTexture;\n"
"uniform vec2 quadSize;\n"
"uniform float shapeRadius;\n"
"uniform float blur;\n"
"uniform bool inset;\n"
"uniform float borderWidth;\n"
"uniform vec4 borderColor;\n"
"void main() {\n"
"    vec4 baseColor = useTexture ? " TEXTURE_FUNC "(objectTexture, v_tex_uv) : objectColor;\n"
"    vec2 p_centered = (v_geom_uv - 0.5) * quadSize;\n"
"    float dist = length(p_centered) - shapeRadius;\n"
"    float alpha_multiplier;\n"
"    if (borderWidth > 0.0) {\n"
"        float innerDist = dist + borderWidth;\n"
"        float edge_softness = fwidth(dist);\n"
"        float inner_edge_softness = fwidth(innerDist);\n"
"        float outerAlpha = smoothstep(edge_softness, -edge_softness, dist);\n"
"        float innerAlpha = smoothstep(inner_edge_softness, -inner_edge_softness, innerDist);\n"
"        alpha_multiplier = outerAlpha - innerAlpha;\n"
#ifdef __ANDROID__
"        FragColor = borderColor;\n"
#else
"        outColor = borderColor;\n"
#endif
"        if (innerDist < 0.0) {\n"
#ifdef __ANDROID__
"            FragColor = baseColor;\n"
#else
"            outColor = baseColor;\n"
#endif
"            alpha_multiplier = smoothstep(edge_softness, -edge_softness, dist);\n"
"        }\n"
"    } else {\n"
"        if (blur > 0.0) {\n"
"            float normalized_dist = clamp((inset ? -dist : dist) / blur, 0.0, 1.0);\n"
"            alpha_multiplier = 1.0 - pow(normalized_dist, 0.75);\n"
"        } else {\n"
"            float edge_softness = fwidth(dist);\n"
"            alpha_multiplier = smoothstep(edge_softness, -edge_softness, dist);\n"
"        }\n"
#ifdef __ANDROID__
"        FragColor = baseColor;\n"
#else
"        outColor = baseColor;\n"
#endif
"    }\n"
#ifdef __ANDROID__
"    FragColor.a *= alpha_multiplier;\n"
#else
"    outColor.a *= alpha_multiplier;\n"
#endif
"    if ("
#ifdef __ANDROID__
"        FragColor.a < 0.01) {\n"
#else
"        outColor.a < 0.01) {\n"
#endif
"        discard;\n"
"    }\n"
"}";

/*
    Шейдер для глифа (Символа из текста)
*/

const char* GLYPH_FS_SRC = SHADER_VERSION OUT_FRAG
"in vec2 v_tex_uv;\n"
"uniform sampler2D objectTexture;\n"
"uniform vec4 objectColor;\n"
"void main() {\n"
"    float alpha = " TEXTURE_FUNC "(objectTexture, v_tex_uv).r;\n"
#ifdef __ANDROID__
"    FragColor = vec4(objectColor.rgb, objectColor.a * alpha);\n"
#else
"    outColor = vec4(objectColor.rgb, objectColor.a * alpha);\n"
#endif
"}";

/*
    Фрагмнтный (Пиксельный) шейдер для линии
        lineWidth - юниформа для определения ширины линии
*/

const char* LINE_FS_SRC = SHADER_VERSION OUT_FRAG
"in vec2 v_geom_uv;\n"
"in vec2 v_tex_uv;\n"
"uniform vec4 objectColor;\n"
"uniform sampler2D objectTexture;\n"
"uniform bool useTexture;\n"
"uniform float lineWidth;\n"
"void main() {\n"
"    vec4 baseColor = useTexture ? " TEXTURE_FUNC "(objectTexture, v_tex_uv) : objectColor;\n"
"    float dist = abs(v_geom_uv.y);\n"
"    float alpha = smoothstep(lineWidth/2.0, lineWidth/2.0 - 1.0, dist);\n"
#ifdef __ANDROID__
"    FragColor = vec4(baseColor.rgb, baseColor.a * alpha);\n"
#else
"    outColor = vec4(baseColor.rgb, baseColor.a * alpha);\n"
#endif
"    if (alpha < 0.01) discard;\n"
"}";

/*
    Функция для компиляции исходного кода шейдера.

    @type - Тип шейдера (Вершинный или фрагментный)
    @source - Строка (const char*) с исходным кодом шейдера

    Безопасность:
        - Если шейдер не удалось скопилировать - функция удаляет шейдер
            чтобы он не занимал место в памяти и возвращает 0.
            Функция создания шейдерной программы этот 0 обрабатывает и
            в случае чего не выполняется. 
*/

GLuint CompileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        
        #ifdef __ANDROID__
            __android_log_print(ANDROID_LOG_ERROR, "DuckerNative", "Ошибка компиляции шейдера (%s): %s", (type == GL_VERTEX_SHADER ? "вершинный" : "фрагментный"), infoLog);
        #else
            std::cout << "[DuckerNative]: Failed to compile shader\n";
        #endif

        glDeleteShader(shader);
        return 0;
    }
    
    return shader;
}

/*
    Создает шейдерную программу из исходного кода вершинного и фрагментного шейдеров

    Функция выполняет следующие шаги:
        1. Компилирует оба шейдера
        2. Создает шейдерную программу и прикрепляет шейдеры
        3. Устанавливает привязки атрибутов вершин
        4. Линкует (Связывет) программу
        5. Проверяет успешность линковки

    @vsSrc Строка (const char*) с исходным кодом вершинного шейдера
    @fsSrc Строка (const char*) с исходным кодом фрагментного шейдера

    Безопасность:
        - Если какой-то из шейдеров не удалось скомпилировать (Он 0)
            то он удаляется из памяти чтобы не занимать лишнее место,
            а сама функция завершается вернув программу с id 0

        - Если линковка (Связывание) прошло с ошибкой - движок
            удаляет шейдерную программу и возвращает программу с id 0
*/

ShaderProgram CreateShaderProgramInternal(const char* vsSrc, const char* fsSrc) {
    ShaderProgram prog;
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);

    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        return prog;
    }

    prog.id = glCreateProgram();
    glAttachShader(prog.id, vs);
    glAttachShader(prog.id, fs);

    glBindAttribLocation(prog.id, 0, "aPos");
    glBindAttribLocation(prog.id, 1, "aTexUv");
    glBindAttribLocation(prog.id, 2, "aGeomUv");

    glLinkProgram(prog.id);

    GLint linkSuccess;
    glGetProgramiv(prog.id, GL_LINK_STATUS, &linkSuccess);
    
    if (!linkSuccess) {
        char infoLog[512];
        glGetProgramInfoLog(prog.id, 512, nullptr, infoLog);
        #ifdef __ANDROID__
            __android_log_print(ANDROID_LOG_ERROR, "DuckerNative", "Ошибка линковки шейдерной программы: %s", infoLog);
        #else
            std::cout << "[DuckerNative]: Failed to link program: " << infoLog << std::endl;
        #endif
        
        glDeleteProgram(prog.id);
        prog.id = 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    return prog;
}

/*
    Функция для рендеринга списка объектов в указанный фреймбуфер (или экран если 0)
*/

void RenderObjects(const fast_vector<RenderObject>& renderObjects, GLuint targetFBO = 0) {
    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);

    if (targetFBO != 0) {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    fast_vector<Vertex> vertices;
    vertices.reserve(renderObjects.size() * 6);

    for (const auto& obj : renderObjects) {
        if (!obj.visible)
            continue;

        if (obj.type == ObjectType::Glyph) {
            Vec2 v0;
            memcpy(&v0, obj.uniforms.at("v0").data.data(), sizeof(Vec2));
            
            Vec2 v1;
            memcpy(&v1, obj.uniforms.at("v1").data.data(), sizeof(Vec2));
            
            Vec2 v2;
            memcpy(&v2, obj.uniforms.at("v2").data.data(), sizeof(Vec2));
            
            Vec2 v3;
            memcpy(&v3, obj.uniforms.at("v3").data.data(), sizeof(Vec2));

            float u1 = obj.uvRect.x;
            float v1_uv = obj.uvRect.y;
            float u2 = obj.uvRect.w;
            float v2_uv = obj.uvRect.h;

            vertices.push_back({v0, {u1, v1_uv}, {0.0f, 0.0f}});
            vertices.push_back({v3, {u1, v2_uv}, {0.0f, 1.0f}});
            vertices.push_back({v1, {u2, v1_uv}, {1.0f, 0.0f}});

            vertices.push_back({v1, {u2, v1_uv}, {1.0f, 0.0f}});
            vertices.push_back({v2, {u2, v2_uv}, {1.0f, 1.0f}});
            vertices.push_back({v3, {u1, v2_uv}, {0.0f, 1.0f}});
        } else if (obj.type == ObjectType::Line) {
            fast_vector<Vec2> all_points;
            all_points.push_back(obj.start);
            for (const auto& cp : obj.controlPoints) {
                all_points.push_back(cp);
            }
            all_points.push_back(obj.end);

            fast_vector<Vec2> points;
            int num_segments = 0;

            if (obj.lineMode == LineMode::Straight) {
                points = all_points;
                num_segments = points.size() > 1 ? points.size() - 1 : 0;
            } else {
                if (all_points.size() < 2) {
                    continue;
                }

                if (obj.controlPoints.empty()) {
                    Vec2 dir = obj.end - obj.start;
                    float len = sqrt(dir.x * dir.x + dir.y * dir.y);
                    
                    if (len > 1e-6f) {
                        Vec2 perp = {-dir.y / len, dir.x / len};
                        Vec2 mid = {(obj.start.x + obj.end.x) / 2.0f, (obj.start.y + obj.end.y) / 2.0f};
                        mid.x += perp.x * (len / 4.0f);
                        mid.y += perp.y * (len / 4.0f);
                        all_points = {obj.start, mid, obj.end};
                    }
                }

                const int num_per_segment = 20;
                for (size_t i = 0; i < all_points.size() - 1; ++i) {
                    Vec2 p0 = all_points[i > 0 ? i - 1 : 0];
                    Vec2 p1 = all_points[i];
                    Vec2 p2 = all_points[i + 1];
                    Vec2 p3 = all_points[i + 1 < all_points.size() - 1 ? i + 2 : all_points.size() - 1];
                    
                    for (int k = 0; k < num_per_segment; ++k) {
                        float t = static_cast<float>(k) / static_cast<float>(num_per_segment - 1);
                        float t2 = t * t;
                        float t3 = t2 * t;
                        
                        Vec2 p;
                        p.x = 0.5f * ((-t3 + 2 * t2 - t) * p0.x + (3 * t3 - 5 * t2 + 2) * p1.x + (-3 * t3 + 4 * t2 + t) * p2.x + (t3 - t2) * p3.x);
                        p.y = 0.5f * ((-t3 + 2 * t2 - t) * p0.y + (3 * t3 - 5 * t2 + 2) * p1.y + (-3 * t3 + 4 * t2 + t) * p2.y + (t3 - t2) * p3.y);
                        
                        points.push_back(p);
                    }
                }
                
                if (!points.empty()) {
                    points.back() = obj.end;
                }
                
                num_segments = points.size() > 1 ? points.size() - 1 : 0;
            }

            for (int seg = 0; seg < num_segments; ++seg) {
                Vec2 p1 = points[seg];
                Vec2 p2 = points[seg + 1];
                Vec2 dir = {p2.x - p1.x, p2.y - p1.y};
                
                float len = sqrt(dir.x * dir.x + dir.y * dir.y);
                if (len < 0.001f)
                    continue;
                
                dir = {dir.x / len, dir.y / len};
                Vec2 perp = {-dir.y * obj.lineWidth / 2.0f, dir.x * obj.lineWidth / 2.0f};
                
                Vec2 v0 = {p1.x + perp.x, p1.y + perp.y};
                Vec2 v1 = {p1.x - perp.x, p1.y - perp.y};
                Vec2 v2 = {p2.x - perp.x, p2.y - perp.y};
                Vec2 v3 = {p2.x + perp.x, p2.y + perp.y};
                
                vertices.push_back({v0, {0.0f, 0.0f}, {0.0f, 1.0f}});
                vertices.push_back({v1, {0.0f, 1.0f}, {0.0f, 0.0f}});
                vertices.push_back({v3, {1.0f, 0.0f}, {1.0f, 1.0f}});
                
                vertices.push_back({v1, {0.0f, 1.0f}, {0.0f, 0.0f}});
                vertices.push_back({v2, {1.0f, 1.0f}, {1.0f, 0.0f}});
                vertices.push_back({v3, {1.0f, 0.0f}, {1.0f, 1.0f}});
            }
        } else {
            float x1 = obj.bounds.x;
            float y1 = obj.bounds.y;
            float x2 = obj.bounds.x + obj.bounds.w;
            float y2 = obj.bounds.y + obj.bounds.h;
            float u1 = obj.uvRect.x;
            float v1_uv = obj.uvRect.y;
            float u2 = obj.uvRect.w;
            float v2_uv = obj.uvRect.h;

            vertices.push_back({{x1, y1}, {u1, v1_uv}, {0.0f, 0.0f}});
            vertices.push_back({{x1, y2}, {u1, v2_uv}, {0.0f, 1.0f}});
            vertices.push_back({{x2, y1}, {u2, v1_uv}, {1.0f, 0.0f}});
            
            vertices.push_back({{x2, y1}, {u2, v1_uv}, {1.0f, 0.0f}});
            vertices.push_back({{x1, y2}, {u1, v2_uv}, {0.0f, 1.0f}});
            vertices.push_back({{x2, y2}, {u2, v2_uv}, {1.0f, 1.0f}});
        }
    }

    glBindVertexArray(state->vao);
    glBindBuffer(GL_ARRAY_BUFFER, state->vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_DYNAMIC_DRAW);

    int vertexOffset = 0;
    for (size_t i = 0; i < renderObjects.size(); ) {
        const RenderObject& firstInBatch = renderObjects[i];
        
        if (!firstInBatch.visible) {
            i = i + 1;
            continue;
        }
        
        uint32_t shaderIdForBatch = firstInBatch.shaderId != 0 ? firstInBatch.shaderId : (firstInBatch.type == ObjectType::Line ? 5 : static_cast<uint32_t>(firstInBatch.type) + 1);
        auto it = state->shaders.find(shaderIdForBatch);

        if (it == state->shaders.end() || it->second.id == 0) {
            i = i + 1;
            continue;
        }

        ShaderProgram& shader = it->second;

        glUseProgram(shader.id);
        glUniformMatrix4fv(glGetUniformLocation(shader.id, "projection"), 1, GL_FALSE, &state->projectionMatrix.m[0][0]);
        
        GLint scissorY = static_cast<GLint>(state->screenHeight - (firstInBatch.scissorRect.y + firstInBatch.scissorRect.h));
        glScissor(
            static_cast<GLint>(firstInBatch.scissorRect.x), 
            scissorY,
            static_cast<GLsizei>(firstInBatch.scissorRect.w),
            static_cast<GLsizei>(firstInBatch.scissorRect.h)
        );

        size_t batchEnd = i;
        while (batchEnd < renderObjects.size()) {
            const RenderObject& obj = renderObjects[batchEnd];
            uint32_t currentShaderId = obj.shaderId != 0 ? obj.shaderId : (obj.type == ObjectType::Line ? 5 : static_cast<uint32_t>(obj.type) + 1);
            
            bool isSameBatch = obj.visible &&
                currentShaderId == shaderIdForBatch &&
                obj.textureId == firstInBatch.textureId &&
                memcmp(&obj.scissorRect, &firstInBatch.scissorRect, sizeof(RectF)) == 0;
            
            if (obj.type == ObjectType::Line && firstInBatch.type == ObjectType::Line) {
                isSameBatch = isSameBatch && obj.lineMode == firstInBatch.lineMode && obj.lineWidth == firstInBatch.lineWidth;
            }
            
            if (!isSameBatch) {
                break;
            }

            batchEnd = batchEnd + 1;
        }

        for (size_t j = i; j < batchEnd; ++j) {
            const RenderObject& obj = renderObjects[j];
            if (!obj.visible) {
                continue;
            }

            mat4 modelMatrix = CreateRotationMatrix(obj.rotation, obj.rotationOrigin, obj.bounds);
            glUniformMatrix4fv(glGetUniformLocation(shader.id, "model"), 1, GL_FALSE, &modelMatrix.m[0][0]);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, obj.textureId);
            
            glUniform1i(glGetUniformLocation(shader.id, "objectTexture"), 0);
            glUniform1i(glGetUniformLocation(shader.id, "useTexture"), obj.textureId != 0);
            glUniform4f(glGetUniformLocation(shader.id, "objectColor"), obj.color.x, obj.color.y, obj.color.z, obj.color.w);
            glUniform2f(glGetUniformLocation(shader.id, "quadSize"), obj.bounds.w, obj.bounds.h);
            
            glUniform1f(glGetUniformLocation(shader.id, "borderWidth"), obj.borderWidth);
            glUniform4f(glGetUniformLocation(shader.id, "borderColor"), 
                obj.borderColor.x, obj.borderColor.y, obj.borderColor.z, obj.borderColor.w);
            
            if (obj.type == ObjectType::Line) {
                glUniform1f(glGetUniformLocation(shader.id, "lineWidth"), obj.lineWidth);
            }

            for (const auto& pair : obj.uniforms) {
                const std::string& name = pair.first;
                const UniformValue& val = pair.second;
                GLint loc = glGetUniformLocation(shader.id, name.c_str());
                
                if (loc != -1) {
                    switch (val.type) {
                        case UniformType::UNIFORM_FLOAT: glUniform1fv(loc, 1, (const GLfloat*)val.data.data()); break;
                        case UniformType::UNIFORM_VEC2:  glUniform2fv(loc, 1, (const GLfloat*)val.data.data()); break;
                        case UniformType::UNIFORM_VEC3:  glUniform3fv(loc, 1, (const GLfloat*)val.data.data()); break;
                        case UniformType::UNIFORM_VEC4:  glUniform4fv(loc, 1, (const GLfloat*)val.data.data()); break;
                        case UniformType::UNIFORM_INT:   glUniform1iv(loc, 1, (const GLint*)val.data.data()); break;
                    }
                }
            }
            
            glDrawArrays(GL_TRIANGLES, vertexOffset, obj.type == ObjectType::Line ? obj.triCount * 3 : 6);
            vertexOffset = vertexOffset + (obj.type == ObjectType::Line ? obj.triCount * 3 : 6);
        }
        i = batchEnd;
    }
    
    glBindVertexArray(0);
    glUseProgram(0);
}

/*
    Функция для применения гауссова блюра к текстуре и композита на экран
*/

void ApplyGaussianBlurAndComposite(float blurRadius) {
    if (blurRadius <= 0.0f) {
        // Без блюра - композит напрямую
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glUseProgram(state->shaders[1].id); // Простой шейдер для композита
        glUniform4f(glGetUniformLocation(state->shaders[1].id, "objectColor"), 1.0f, 1.0f, 1.0f, 1.0f);
        glUniform1i(glGetUniformLocation(state->shaders[1].id, "useTexture"), true);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, state->shadowTexture);
        glUniform1i(glGetUniformLocation(state->shaders[1].id, "objectTexture"), 0);
        glBindVertexArray(state->quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        return;
    }

    float sigma = blurRadius / 3.0f; // Примерное соответствие радиусу в Material Design
    int halfKernel = std::max(1, std::min(15, static_cast<int>(sigma * 3.0f))); // 3 sigma
    fast_vector<float> weights(halfKernel + 1);
    float sum = 0.0f;
    for (int i = 0; i <= halfKernel; ++i) {
        float x = static_cast<float>(i);
        weights[i] = expf(-x * x / (2.0f * sigma * sigma)) / (sqrtf(2.0f * 3.1415926535f) * sigma);
        sum += weights[i] * (i == 0 ? 1.0f : 2.0f);
    }
    for (auto& w : weights) w /= sum;

    // Горизонтальный проход
    glBindFramebuffer(GL_FRAMEBUFFER, state->intermediateFBO);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(state->blurHorizontal.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, state->shadowTexture);
    glUniform1i(glGetUniformLocation(state->blurHorizontal.id, "tex"), 0);
    glUniform1fv(glGetUniformLocation(state->blurHorizontal.id, "weights"), halfKernel + 1, weights.data());
    glUniform1i(glGetUniformLocation(state->blurHorizontal.id, "halfKernel"), halfKernel);
    glUniform1f(glGetUniformLocation(state->blurHorizontal.id, "pixelSize"), 1.0f / static_cast<float>(state->screenWidth));
    glBindVertexArray(state->quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Вертикальный проход и композит на экран
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(state->blurVertical.id);
    glBindTexture(GL_TEXTURE_2D, state->intermediateTexture);
    glUniform1i(glGetUniformLocation(state->blurVertical.id, "tex"), 0);
    glUniform1fv(glGetUniformLocation(state->blurVertical.id, "weights"), halfKernel + 1, weights.data());
    glUniform1i(glGetUniformLocation(state->blurVertical.id, "halfKernel"), halfKernel);
    glUniform1f(glGetUniformLocation(state->blurVertical.id, "pixelSize"), 1.0f / static_cast<float>(state->screenHeight));
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

/*
    Обновляет матрицу ортографической проекции для 2D-рендеринга.

    Функция вычисляет матрицу проекции, которая:
    - Преобразует экранные координаты в NDC (Normalized Device Coordinates)
    - Задает систему координат с началом в ЛЕВОМ ВЕРХНЕМ углу экрана
    - Ось X направлена вправо, ось Y - вниз
    - Глубина (Z) не используется (устанавливается фиксированное значение -1)

    -Матрица рассчитывается по формуле:
        [ 2/(R-L)      0         0    -(R+L)/(R-L) ]
        [    0      2/(T-B)      0    -(T+B)/(T-B) ]
        [    0         0        -1          0       ]
        [    0         0         0          1       ]

    Переменные:
        - L (left) всегда = 0
        - T (top) всегда = 0 (ось Y направлена вниз)
        - При нулевых размерах экрана выводит предупреждение
        - Автоматически обрабатывает случай nullptr state
*/

mat4 CreateRotationMatrix(float angle, Vec2 origin, RectF bounds) {
    float rad = angle * 3.1415926535f / 180.0f;
    float cosA = cos(rad);
    float sinA = sin(rad);

    float centerX = bounds.x + bounds.w * origin.x;
    float centerY = bounds.y + bounds.h * origin.y;

    mat4 matrix = {{
        {cosA, -sinA, 0.0f, centerX - cosA * centerX + sinA * centerY},
        {sinA,  cosA, 0.0f, centerY - sinA * centerX - cosA * centerY},
        {0.0f,  0.0f, 1.0f, 0.0f},
        {0.0f,  0.0f, 0.0f, 1.0f}
    }};

    return matrix;
}

void UpdateProjectionMatrix() {
    if (state == nullptr) {
        std::cout << "[DuckerNative.dll]: RenderState is nullptr\n";
        return;
    }

    float L = 0.0f;
    float R = static_cast<float>(state->screenWidth);
    float B = static_cast<float>(state->screenHeight);
    float T = 0.0f;

    state->projectionMatrix = {{
        {2.0f / (R - L), 0.0f, 0.0f, 0.0f},
        {0.0f, 2.0f / (T - B), 0.0f, 0.0f},
        {0.0f, 0.0f, -1.0f, 0.0f},
        {-(R + L) / (R - L), -(T + B) / (T - B), 0.0f, 1.0f}
    }};
}

/*
    Создает FBO и прикрепленную текстуру
*/

void CreateFBOAndTexture(GLuint* fbo, GLuint* tex, int width, int height) {
    glGenFramebuffers(1, fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);

    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        #ifndef __ANDROID__
        std::cout << "[DuckerNative]: Failed to create FBO\n";
        #endif
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/*
    Обновляет размер текстур FBO при изменении размера экрана
*/

void ResizeFBOTextures(int width, int height) {
    if (state->shadowTexture != 0) {
        glBindTexture(GL_TEXTURE_2D, state->shadowTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    if (state->intermediateTexture != 0) {
        glBindTexture(GL_TEXTURE_2D, state->intermediateTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

/*
    Находит объект рендера по его идентификатору.

    @id Уникальный идентификатор объекта
    
    Возвращает казатель на RenderObject или nullptr если:
        - Состояние рендера не инициализировано
        - Объект с таким ID не найден

    Особенности:
        - Использует хэш-таблицу для быстрого поиска
        - Возвращает прямой указатель на объект в памяти
        - Не выполняет дополнительных проверок валидности объекта
 */

RenderObject* FindObject(uint32_t id) {
    if (state == nullptr)  {
        return nullptr;
    }

    auto it = state->objectIdToIndex.find(id);
    if (it != state->objectIdToIndex.end()) {
        size_t index = it->second;
        return &state->objects[index];
    }
    
    return nullptr;
}

/*

Внутренняя функция для добавления нового объекта рендеринга.
 
    @obj Объект для добавления (передается по значению)

    Возвращает ID созданного объекта или 0 при ошибке
 
    Особенности:
        - Автоматически назначает уникальный ID
        - Учитывает текущие контейнеры и смещения
        - Применяет текущие ограничения отсечения (scissor)
        - Помечает систему как нуждающуюся в сортировке
        - Обрабатывает вложенные контейнеры через containerStack

    Безопасность:
        - Проверяет инициализацию state
        - Гарантирует уникальность ID
 */

 uint32_t AddObjectInternal(RenderObject obj) {
    if (state == nullptr) {
        return 0;
    }

    /*
        Удаляем старый код для локальных координат в контейнере. Делаем контейнер просто
            "Окном" из-за проблем с локальными координатами. Постоянно были смещения
            дальше чем нужно, поэтому этот блок кода удалён.

        Старый код остаётся сделать на случай, если кто-то захочет его исправить
            Vec2 offset = {0.0f, 0.0f};
            
            if (!state->containerStack.empty()) {
                offset = state->containerStack.back();
            }
            
            obj.bounds.x = obj.bounds.x + offset.x;
            obj.bounds.y = obj.bounds.y + offset.y;

    */

    if (!state->scissorStack.empty()) {
        obj.scissorRect = state->scissorStack.back();
    } else {
        obj.scissorRect = {
            0.0f, 0.0f, 
            static_cast<float>(state->screenWidth), 
            static_cast<float>(state->screenHeight)
        };
    }

    obj.id = state->nextObjectId;
    state->nextObjectId = state->nextObjectId + 1;

    size_t newIndex = state->objects.size();
    state->objectIdToIndex[obj.id] = newIndex;
    state->objects.push_back(obj);
    state->needsSort = true;

    return obj.id;
}

/*
    Стандартная библиотека для OpenGL не поддерижвает современные стандарты, такие
        как 2.0, 3.0, 3.1, 3.3 и так далее.

    Чтобы нам это решить - необходима "Карта" которая подсказывает как найти
        нужную OpenGL функцию. Такой картой является GLAD для OpenGl 3.1

    Мы должны получить указатели (Пути) к функциям нового OpenGL и для этого
        используем gladLoadGLLoader из лоадера, который нам передали из
        основного приложения.

    Безопасность:
        - Если инициализация не удалась - мы выводим лог в консоль
*/

#ifndef __ANDROID__
DUCKER_API void DuckerNative_SetupGlad(GLADloadproc loader) {
    if (!gladLoadGLLoader(loader)) {
        std::cout << "[DuckerNative]: Failed to initialize GLAD\n";
    } else {
        std::cout << "[DuckerNative]: GLAD initialized successfully\n";
    }
}
#endif

DUCKER_API void DuckerNative_Initialize(int screenWidth, int screenHeight) {
    if (state != nullptr) {
        std::cout << "[DuckerNative]: RendererState already initialized\n";
        return;
    }

    state = new RendererState();

    state->shaders[1] = CreateShaderProgramInternal(UNIVERSAL_VS_SRC, RECT_FS_SRC);
    state->shaders[2] = CreateShaderProgramInternal(UNIVERSAL_VS_SRC, ROUNDED_RECT_FS_SRC);
    state->shaders[3] = CreateShaderProgramInternal(UNIVERSAL_VS_SRC, CIRCLE_FS_SRC);
    state->shaders[4] = CreateShaderProgramInternal(UNIVERSAL_VS_SRC, GLYPH_FS_SRC);
    state->shaders[5] = CreateShaderProgramInternal(UNIVERSAL_VS_SRC, LINE_FS_SRC);

    state->blurHorizontal = CreateShaderProgramInternal(QUAD_VS_SRC, HORIZONTAL_BLUR_FS_SRC);
    state->blurVertical = CreateShaderProgramInternal(QUAD_VS_SRC, VERTICAL_BLUR_FS_SRC);

    glGenVertexArrays(1, &state->vao);
    glBindVertexArray(state->vao);
    glGenBuffers(1, &state->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, state->vbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texUv));
    
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, geomUv));
    
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Полноэкранный квад
    float quadVertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f
    };

    glGenVertexArrays(1, &state->quadVAO);
    glBindVertexArray(state->quadVAO);
    glGenBuffers(1, &state->quadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, state->quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // FBO для теней и промежуточный
    CreateFBOAndTexture(&state->shadowFBO, &state->shadowTexture, screenWidth, screenHeight);
    CreateFBOAndTexture(&state->intermediateFBO, &state->intermediateTexture, screenWidth, screenHeight);

    // Заполнение пресетов теней Material Design 3 (из реверс-инжиниринга Android/MDC)
    state->shadowPresets[0] = {};
    state->shadowPresets[1] = {
        {0.20f, 2.0f, 1.0f, -1.0f}, // Umbra
        {0.14f, 1.0f, 1.0f, 0.0f},  // Penumbra
        {0.12f, 1.0f, 3.0f, 0.0f}   // Ambient
    };
    state->shadowPresets[2] = {
        {0.20f, 3.0f, 1.0f, -2.0f},
        {0.14f, 2.0f, 2.0f, 0.0f},
        {0.12f, 1.0f, 5.0f, 0.0f}
    };
    state->shadowPresets[3] = {
        {0.20f, 3.0f, 3.0f, -2.0f},
        {0.14f, 3.0f, 4.0f, 0.0f},
        {0.12f, 1.0f, 8.0f, 0.0f}
    };
    state->shadowPresets[4] = {
        {0.20f, 2.0f, 4.0f, -1.0f},
        {0.14f, 4.0f, 5.0f, 0.0f},
        {0.12f, 1.0f, 10.0f, 0.0f}
    };
    state->shadowPresets[5] = {
        {0.20f, 3.0f, 5.0f, -1.0f},
        {0.14f, 5.0f, 8.0f, 0.0f},
        {0.12f, 1.0f, 14.0f, 0.0f}
    };
    // Продолжить для 6-24 по аналогии из источника, но для примера достаточно. Можно добавить все.

    DuckerNative_SetScreenSize(screenWidth, screenHeight);
}

/*
    Полная ликвидация состояние рендера. Не используется в luvix для хот релоада,
    так как повторная загрузка многих элементов занимает слишком много времени

        Функция ликвидирует:
            - Шрифты
            - Шейдеры
            - Текстуры

*/

DUCKER_API void DuckerNative_Shutdown() {
    if (state == nullptr)  {
        std::cout << "[DuckerNative.dll]: RenderState is nullptr";
        return;
    }

    DuckerNative_Clear();

    for (auto const& pair : state->fonts) {
        const Font& font = pair.second;
        glDeleteTextures(1, &font.textureId);
    }

    state->fonts.clear();

    for (auto const& pair : state->shaders) {
        const ShaderProgram& program = pair.second;

        if (program.id != 0)  {
            glDeleteProgram(program.id);
        }
    }

    if (state->blurHorizontal.id != 0) glDeleteProgram(state->blurHorizontal.id);
    if (state->blurVertical.id != 0) glDeleteProgram(state->blurVertical.id);

    state->shaders.clear();

    if (state->vao != 0)  {
        glDeleteVertexArrays(1, &state->vao);
    }
    
    if (state->vbo != 0) {
        glDeleteBuffers(1, &state->vbo);
    }

    if (state->quadVAO != 0) glDeleteVertexArrays(1, &state->quadVAO);
    if (state->quadVBO != 0) glDeleteBuffers(1, &state->quadVBO);

    if (state->shadowFBO != 0) glDeleteFramebuffers(1, &state->shadowFBO);
    if (state->shadowTexture != 0) glDeleteTextures(1, &state->shadowTexture);
    if (state->intermediateFBO != 0) glDeleteFramebuffers(1, &state->intermediateFBO);
    if (state->intermediateTexture != 0) glDeleteTextures(1, &state->intermediateTexture);

    delete state;
    state = nullptr;
}

DUCKER_API void DuckerNative_Clear() {
    if (state == nullptr)  {
        std::cout << "[DuckerNative.dll]: RenderState is nullptr. In function: DUCKER_API void DuckerNative_Clear() {}";
        return;
    }

    state->objects.clear();
    state->objectIdToIndex.clear();
    state->containerStack.clear();
    state->scissorStack.clear();
}

DUCKER_API void DuckerNative_SetScreenSize(int screenWidth, int screenHeight) {
    if (state == nullptr) {
        std::cout << "[DuckerNative.dll]: RenderState is nullptr. In function: DUCKER_API void DuckerNative_SetScreenSize(int screenWidth, int screenHeight)";
        return;
    }

    state->screenWidth = screenWidth;
    state->screenHeight = screenHeight;
    UpdateProjectionMatrix();
    ResizeFBOTextures(screenWidth, screenHeight);
}

/*
    Базовые функции для создания объектов
*/

DUCKER_API uint32_t DuckerNative_AddRect(RectF bounds, Vec4 color, int zIndex,
        uint32_t textureId, RectF uvRect, float borderWidth, Vec4 borderColor) {
    RenderObject obj;
    obj.type = ObjectType::Rect;
    obj.bounds = bounds;
    obj.color = color;
    obj.zIndex = zIndex;
    obj.textureId = textureId;
    obj.uvRect = uvRect;
    obj.borderWidth = borderWidth;
    obj.borderColor = borderColor;
    obj.rotation = 0.0f;
    obj.rotationOrigin = {0.5f, 0.5f};

    obj.uniforms["borderWidth"] = { UniformType::UNIFORM_FLOAT };
    UniformValue& bwVal = obj.uniforms["borderWidth"];
    bwVal.data.resize(sizeof(float));
    memcpy(bwVal.data.data(), &borderWidth, sizeof(float));

    obj.uniforms["borderColor"] = { UniformType::UNIFORM_VEC4 };
    UniformValue& bcVal = obj.uniforms["borderColor"];
    bcVal.data.resize(sizeof(Vec4));
    memcpy(bcVal.data.data(), &borderColor, sizeof(Vec4));

    return AddObjectInternal(obj);
}

DUCKER_API uint32_t DuckerNative_AddRoundedRect(RectF bounds, Vec2 shapeSize, Vec4 color,
        float cornerRadius, float blur, bool inset, int zIndex, uint32_t textureId,
        RectF uvRect, float borderWidth, Vec4 borderColor) {
    RenderObject obj;
    obj.type = ObjectType::RoundedRect;
    obj.bounds = bounds;
    obj.color = color;
    obj.zIndex = zIndex;
    obj.textureId = textureId;
    obj.uvRect = uvRect;
    obj.borderWidth = borderWidth;
    obj.borderColor = borderColor;
    obj.rotation = 0.0f;
    obj.rotationOrigin = {0.5f, 0.5f};

    obj.uniforms["quadSize"] = { UniformType::UNIFORM_VEC2 };
    UniformValue& qsVal = obj.uniforms["quadSize"];
    qsVal.data.resize(sizeof(Vec2));
    Vec2 quadSize = {bounds.w, bounds.h};
    memcpy(qsVal.data.data(), &quadSize, sizeof(Vec2));

    obj.uniforms["shapeSize"] = { UniformType::UNIFORM_VEC2 };
    UniformValue& ssVal = obj.uniforms["shapeSize"];
    ssVal.data.resize(sizeof(Vec2));
    memcpy(ssVal.data.data(), &shapeSize, sizeof(Vec2));

    obj.uniforms["cornerRadius"] = { UniformType::UNIFORM_FLOAT };
    UniformValue& crVal = obj.uniforms["cornerRadius"];
    crVal.data.resize(sizeof(float));
    memcpy(crVal.data.data(), &cornerRadius, sizeof(float));

    obj.uniforms["blur"] = { UniformType::UNIFORM_FLOAT };
    UniformValue& bVal = obj.uniforms["blur"];
    bVal.data.resize(sizeof(float));
    memcpy(bVal.data.data(), &blur, sizeof(float));

    obj.uniforms["inset"] = { UniformType::UNIFORM_INT };
    UniformValue& iVal = obj.uniforms["inset"];
    iVal.data.resize(sizeof(int));
    int i = inset;
    memcpy(iVal.data.data(), &i, sizeof(int));

    obj.uniforms["borderWidth"] = { UniformType::UNIFORM_FLOAT };
    UniformValue& bwVal = obj.uniforms["borderWidth"];
    bwVal.data.resize(sizeof(float));
    memcpy(bwVal.data.data(), &borderWidth, sizeof(float));

    obj.uniforms["borderColor"] = { UniformType::UNIFORM_VEC4 };
    UniformValue& bcVal = obj.uniforms["borderColor"];
    bcVal.data.resize(sizeof(Vec4));
    memcpy(bcVal.data.data(), &borderColor, sizeof(Vec4));

    return AddObjectInternal(obj);
}

DUCKER_API uint32_t DuckerNative_AddCircle(RectF bounds, Vec4 color, float radius, float blur,
        bool inset, int zIndex, uint32_t textureId, float borderWidth, Vec4 borderColor) {
    RenderObject obj;
    obj.type = ObjectType::Circle;
    obj.bounds = bounds;
    obj.color = color;
    obj.zIndex = zIndex;
    obj.textureId = textureId;
    obj.borderWidth = borderWidth;
    obj.borderColor = borderColor;
    obj.rotation = 0.0f;
    obj.rotationOrigin = {0.5f, 0.5f};

    obj.uniforms["shapeRadius"] = { UniformType::UNIFORM_FLOAT };
    UniformValue& rVal = obj.uniforms["shapeRadius"];
    rVal.data.resize(sizeof(float));
    memcpy(rVal.data.data(), &radius, sizeof(float));

    obj.uniforms["blur"] = { UniformType::UNIFORM_FLOAT };
    UniformValue& bVal = obj.uniforms["blur"];
    bVal.data.resize(sizeof(float));
    memcpy(bVal.data.data(), &blur, sizeof(float));

    obj.uniforms["inset"] = { UniformType::UNIFORM_INT };
    UniformValue& iVal = obj.uniforms["inset"];
    iVal.data.resize(sizeof(int));
    int i = inset;
    memcpy(iVal.data.data(), &i, sizeof(int));

    obj.uniforms["borderWidth"] = { UniformType::UNIFORM_FLOAT };
    UniformValue& bwVal = obj.uniforms["borderWidth"];
    bwVal.data.resize(sizeof(float));
    memcpy(bwVal.data.data(), &borderWidth, sizeof(float));

    obj.uniforms["borderColor"] = { UniformType::UNIFORM_VEC4 };
    UniformValue& bcVal = obj.uniforms["borderColor"];
    bcVal.data.resize(sizeof(Vec4));
    memcpy(bcVal.data.data(), &borderColor, sizeof(Vec4));

    return AddObjectInternal(obj);
}

DUCKER_API uint32_t DuckerNative_AddLine(Vec2 start, Vec2 end, Vec4 color, float width,
        LineMode mode, const Vec2* controls, int numControls, int zIndex) {
    if (state == nullptr)
        return 0;

    RenderObject obj;
    obj.type = ObjectType::Line;
    obj.start = start;
    obj.end = end;
    obj.lineWidth = width;
    obj.lineMode = mode;
    obj.color = color;
    obj.zIndex = zIndex;
    obj.textureId = 0;
    obj.controlPoints.resize(numControls);
    obj.rotation = 0.0f;
    obj.rotationOrigin = {0.5f, 0.5f};

    if (numControls > 0) {
        memcpy(obj.controlPoints.data(), controls, numControls * sizeof(Vec2));
    }

    fast_vector<Vec2> all_points;
    all_points.push_back(obj.start);
    for (const auto& cp : obj.controlPoints) {
        all_points.push_back(cp);
    }
    all_points.push_back(obj.end);

    fast_vector<Vec2> approx_points;
    float minX = start.x;
    float maxX = start.x;
    float minY = start.y;
    float maxY = start.y;

    if (obj.lineMode == LineMode::Straight) {
        approx_points = all_points;
        
        for (const auto& p : approx_points) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }

        int num_segments = approx_points.size() > 1 ? approx_points.size() - 1 : 0;
        obj.triCount = num_segments * 2;
    } else {
        if (all_points.size() < 2) {
            obj.triCount = 0;
        } else {
            if (obj.controlPoints.empty()) {
                Vec2 dir = obj.end - obj.start;
                float len = sqrt(dir.x * dir.x + dir.y * dir.y);
                
                if (len > 1e-6f) {
                    Vec2 perp = {-dir.y / len, dir.x / len};
                    Vec2 mid = {(obj.start.x + obj.end.x) / 2.0f, (obj.start.y + obj.end.y) / 2.0f};
                    
                    mid.x += perp.x * (len / 4.0f);
                    mid.y += perp.y * (len / 4.0f);
                    
                    all_points = {obj.start, mid, obj.end};
                }
            }

            const int num_per_segment = 20;
            for (size_t i = 0; i < all_points.size() - 1; ++i) {
                Vec2 p0 = all_points[i > 0 ? i - 1 : 0];
                Vec2 p1 = all_points[i];
                Vec2 p2 = all_points[i + 1];
                Vec2 p3 = all_points[i + 1 < all_points.size() - 1 ? i + 2 : all_points.size() - 1];
                
                for (int k = 0; k < num_per_segment; ++k) {
                    float t = static_cast<float>(k) / static_cast<float>(num_per_segment - 1);
                    float t2 = t * t;
                    float t3 = t2 * t;
                    
                    Vec2 p;
                    p.x = 0.5f * ((-t3 + 2 * t2 - t) * p0.x + (3 * t3 - 5 * t2 + 2) * p1.x + (-3 * t3 + 4 * t2 + t) * p2.x + (t3 - t2) * p3.x);
                    p.y = 0.5f * ((-t3 + 2 * t2 - t) * p0.y + (3 * t3 - 5 * t2 + 2) * p1.y + (-3 * t3 + 4 * t2 + t) * p2.y + (t3 - t2) * p3.y);
                    
                    approx_points.push_back(p);
                    
                    minX = std::min(minX, p.x);
                    maxX = std::max(maxX, p.x);
                    minY = std::min(minY, p.y);
                    maxY = std::max(maxY, p.y);
                }
            }

            if (!approx_points.empty()) {
                approx_points.back() = obj.end;
            }

            int num_segments = approx_points.size() > 1 ? approx_points.size() - 1 : 0;
            obj.triCount = num_segments * 2;
        }
    }

    obj.bounds = {minX - width / 2.0f, minY - width / 2.0f, maxX - minX + width, maxY - minY + width};

    return AddObjectInternal(obj);
}

DUCKER_API void DuckerNative_RemoveObject(uint32_t objectId) {
    if (state == nullptr)  {
        return;
    }

    auto it = state->objectIdToIndex.find(objectId);
    if (it != state->objectIdToIndex.end()) {
        size_t indexToRemove = it->second;
        
        if (state->objects.size() > 1 && indexToRemove < state->objects.size() - 1) {
            RenderObject& lastObject = state->objects.back();
            state->objects[indexToRemove] = lastObject;
            state->objectIdToIndex[lastObject.id] = indexToRemove;
        }

        state->objects.pop_back();
        state->objectIdToIndex.erase(it);
    }
}

DUCKER_API void DuckerNative_SetObjectCornerRadius(uint32_t objectId, float radius) {
    RenderObject* obj = FindObject(objectId);
    if (obj != nullptr) {
        if (obj->type != ObjectType::RoundedRect) {
            return;
        }

        const char* uniformName = "cornerRadius";
        UniformValue& val = obj->uniforms[uniformName];
        val.type = UniformType::UNIFORM_FLOAT;
        val.data.resize(sizeof(float));
        memcpy(val.data.data(), &radius, sizeof(float));
    }
}

DUCKER_API void DuckerNative_SetObjectRotation(uint32_t objectId, float rotation) {
    RenderObject* obj = FindObject(objectId);
    if (obj != nullptr) {
        obj->rotation = rotation;
    }
}

DUCKER_API void DuckerNative_SetObjectRotationOrigin(uint32_t objectId, Vec2 origin) {
    RenderObject* obj = FindObject(objectId);
    if (obj != nullptr) {
        obj->rotationOrigin = origin;
    }
}

DUCKER_API void DuckerNative_SetObjectRotationAndOrigin(uint32_t objectId, float rotation, Vec2 origin) {
    RenderObject* obj = FindObject(objectId);
    if (obj != nullptr) {
        obj->rotation = rotation;
        obj->rotationOrigin = origin;
    }
}

DUCKER_API void DuckerNative_SetObjectElevation(uint32_t objectId, int elevation) {
    RenderObject* obj = FindObject(objectId);
    if (obj != nullptr) {
        obj->elevation = elevation;
        state->needsSort = true; // Тени влияют на порядок
    }
}

/*
    Функция загрузки шрифта через stb_true_type
*/

DUCKER_API uint32_t DuckerNative_LoadFont(const char* filepath, float size) {
    if (state == nullptr)  {
        return 0;
    }
    
    FILE* file = fopen(filepath, "rb");
    if (file == nullptr)  {
        return 0;
    }
    
    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    fast_vector<unsigned char> ttf_buffer;
    ttf_buffer.resize(fsize);
    
    fread(ttf_buffer.data(), 1, fsize, file);
    fclose(file);

    /*
        Используем размер атласа 4096 на 4096 для поддержки большого размера
        самих шрифтов. Для оптимизации можно поставить 2048 на 2048, но в
        таком случае загрузка шрифтов большого размера может закончится
        ошибкой.

        Единной системы которая подходит для всех шрифтов и размеров сразу НЕТ
        используем прямое значение
    */

    Font font;
    font.size = size;
    font.atlasWidth = 4096;
    font.atlasHeight = 4096;
    
    fast_vector<unsigned char> bitmap;
    bitmap.resize(font.atlasWidth * font.atlasHeight);
    
    stbtt_pack_context context;
    if (!stbtt_PackBegin(&context, bitmap.data(), font.atlasWidth, font.atlasHeight, 0, 1, nullptr)) {
        return 0;
    }
    
    stbtt_PackSetOversampling(&context, 2, 2);
    stbtt_pack_range ranges[] = {
        {size, 32, nullptr, 96, font.char_data, 0, 0},
        {size, 0x0400, nullptr, 256, font.char_data + 96, 0, 0}
    };
    int num_ranges = sizeof(ranges) / sizeof(ranges[0]);

    if (!stbtt_PackFontRanges(&context, ttf_buffer.data(), 0, ranges, num_ranges)) {
         stbtt_PackEnd(&context);
         return 0;
    }
    stbtt_PackEnd(&context);

    glGenTextures(1, &font.textureId);
    glBindTexture(GL_TEXTURE_2D, font.textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, font.atlasWidth, font.atlasHeight, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    uint32_t fontId = state->nextFontId;
    state->nextFontId = state->nextFontId + 1;
    state->fonts[fontId] = font;
    return fontId;
}

const char* utf8_to_codepoint(const char *p, unsigned int *dst) {
    const unsigned char *s = (const unsigned char*)p;
    
    if (s[0] < 0x80) { 
        *dst = s[0]; 
        return p + 1; 
    }
    
    if ((s[0] & 0xe0) == 0xc0) { 
        *dst = ((s[0] & 0x1f) << 6) | (s[1] & 0x3f); 
        return p + 2; 
    }
    
    if ((s[0] & 0xf0) == 0xe0) { 
        *dst = ((s[0] & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f); 
        return p + 3; 
    }
    
    if ((s[0] & 0xf8) == 0xf0) { 
        *dst = ((s[0] & 0x07) << 18) | ((s[1] & 0x3f) << 12) | ((s[2] & 0x3f) << 6) | (s[3] & 0x3f); 
        return p + 4; 
    }
    
    *dst = '?'; 
    return p + 1;
}

DUCKER_API void DuckerNative_DrawText(uint32_t fontId, const char* text, Vec2 position, Vec4 color, int zIndex,
        float rotation, Vec2 origin) {
    if (state == nullptr || text == nullptr) return;

    auto it = state->fonts.find(fontId);
    if (it == state->fonts.end()) return;

    const Font& font = it->second;
    float x = position.x;
    float y = position.y;

    float angle = rotation * 3.1415926535f / 180.0f;
    float cos_a = cos(angle);
    float sin_a = sin(angle);

    const char* p = text;
    while (*p) {
        unsigned int codepoint;
        p = utf8_to_codepoint(p, &codepoint);
        
        int index = -1;
        if (codepoint >= 32 && codepoint < 128) index = codepoint - 32;
        else if (codepoint >= 0x0400 && codepoint <= 0x04FF) index = 96 + (codepoint - 0x0400);

        if (index != -1) {
            stbtt_aligned_quad q;
            stbtt_GetPackedQuad(font.char_data, font.atlasWidth, font.atlasHeight, index, &x, &y, &q, 0);

            float x0_local = q.x0, y0_local = q.y0;
            float x1_local = q.x1, y1_local = q.y1;

            float rot_x0 = x0_local - position.x - origin.x, rot_y0 = y0_local - position.y - origin.y;
            float rot_x1 = x1_local - position.x - origin.x, rot_y1 = y1_local - position.y - origin.y;
            
            Vec2 v0 = {rot_x0 * cos_a - rot_y0 * sin_a + position.x + origin.x, rot_x0 * sin_a + rot_y0 * cos_a + position.y + origin.y};
            Vec2 v1 = {rot_x1 * cos_a - rot_y0 * sin_a + position.x + origin.x, rot_x1 * sin_a + rot_y0 * cos_a + position.y + origin.y};
            Vec2 v2 = {rot_x1 * cos_a - rot_y1 * sin_a + position.x + origin.x, rot_x1 * sin_a + rot_y1 * cos_a + position.y + origin.y};
            Vec2 v3 = {rot_x0 * cos_a - rot_y1 * sin_a + position.x + origin.x, rot_x0 * sin_a + rot_y1 * cos_a + position.y + origin.y};
            
            RenderObject obj;
            obj.type = ObjectType::Glyph;
            obj.bounds = {v0.x, v0.y, v1.x - v0.x, v3.y - v0.y}; 
            obj.uvRect = {q.s0, q.t0, q.s1, q.t1};
            
            UniformValue val_v0;
            val_v0.type = UniformType::UNIFORM_VEC2;
            val_v0.data.resize(sizeof(Vec2));
            memcpy(val_v0.data.data(), &v0, sizeof(Vec2));

            UniformValue val_v1;
            val_v1.type = UniformType::UNIFORM_VEC2;
            val_v1.data.resize(sizeof(Vec2));
            memcpy(val_v1.data.data(), &v1, sizeof(Vec2));

            UniformValue val_v2;
            val_v2.type = UniformType::UNIFORM_VEC2;
            val_v2.data.resize(sizeof(Vec2));
            memcpy(val_v2.data.data(), &v2, sizeof(Vec2));

            UniformValue val_v3;
            val_v3.type = UniformType::UNIFORM_VEC2;
            val_v3.data.resize(sizeof(Vec2));
            memcpy(val_v3.data.data(), &v3, sizeof(Vec2));

            obj.uniforms["v0"] = std::move(val_v0);
            obj.uniforms["v1"] = std::move(val_v1);
            obj.uniforms["v2"] = std::move(val_v2);
            obj.uniforms["v3"] = std::move(val_v3);
            
            obj.color = color;
            obj.zIndex = zIndex;
            obj.textureId = font.textureId;
            
            AddObjectInternal(obj);
        }
    }
}

DUCKER_API Vec2 DuckerNative_GetTextSize(uint32_t fontId, const char* text) {
    if (state == nullptr)  {
        return {0.0f, 0.0f};
    }

    auto it = state->fonts.find(fontId);
    if (it == state->fonts.end()) {
        return {0.0f, 0.0f};
    }

    const Font& font = it->second;
    float x = 0.0f;
    float y = 0.0f;
    float min_y = 0.0f;
    float max_y = 0.0f;
    
    const char* p = text;
    while (*p) {
        unsigned int codepoint;
        p = utf8_to_codepoint(p, &codepoint);

        int index = -1;
        if (codepoint >= 32 && codepoint < 128) {
            index = codepoint - 32;
        }

        else if (codepoint >= 0x0400 && codepoint <= 0x04FF) {
            index = 96 + (codepoint - 0x0400);
        }

        if (index != -1) {
            stbtt_aligned_quad q;
            stbtt_GetPackedQuad(font.char_data, font.atlasWidth, font.atlasHeight, index, &x, &y, &q, 1);
            if (q.y0 < min_y)  {
                min_y = q.y0;
            }

            if (q.y1 > max_y) {
                max_y = q.y1;
            }
        }
    }

    return { x, max_y - min_y };
}

DUCKER_API void DuckerNative_DeleteFont(uint32_t fontId) {
    if (state == nullptr)  {
        return;
    }

    auto it = state->fonts.find(fontId);
    if (it != state->fonts.end()) {
        glDeleteTextures(1, &it->second.textureId);
        state->fonts.erase(it);
    }
}

DUCKER_API uint32_t DuckerNative_LoadTexture(const char* filepath, int* outWidth, 
        int* outHeight) {
    int width;
    int height;
    int nrChannels;
    unsigned char *data = stbi_load(filepath, &width, &height, &nrChannels, 0);

    if (data == nullptr) {
        return 0;
    }

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    GLenum format = GL_RGB;
    if (nrChannels == 1)  {
        format = GL_RED;
    } else if (nrChannels == 3)  {
        format = GL_RGB;
    } else if (nrChannels == 4) {
        format = GL_RGBA;
    }
    
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);
    
    if (outWidth != nullptr)  {
        *outWidth = width;
    } if (outHeight != nullptr) {
        *outHeight = height;
    }

    return textureID;
}

DUCKER_API void DuckerNative_DeleteTexture(uint32_t textureId) {
    if (textureId > 0) {
        glDeleteTextures(1, &textureId);
    }
}

DUCKER_API uint32_t DuckerNative_CreateShader(const char* fragmentShaderSource) {
    if (state == nullptr) {
        return 0;
    }
    
    ShaderProgram prog = CreateShaderProgramInternal(UNIVERSAL_VS_SRC, fragmentShaderSource);
    if (prog.id == 0)  {
        return 0;
    }

    uint32_t id = state->nextCustomShaderId;
    state->nextCustomShaderId = state->nextCustomShaderId + 1;
    state->shaders[id] = prog;
    return id;
}

DUCKER_API void DuckerNative_DeleteShader(uint32_t shaderId) {
    if (state == nullptr || shaderId < 100)  {
        return;
    }

    auto it = state->shaders.find(shaderId);
    if (it != state->shaders.end()) {
        glDeleteProgram(it->second.id);
        state->shaders.erase(it);
    }
}

DUCKER_API void DuckerNative_SetObjectShader(uint32_t objectId, uint32_t shaderId) {
    RenderObject* obj = FindObject(objectId);
    if (obj != nullptr) {
        if (obj->shaderId != shaderId) {
            obj->shaderId = shaderId;
            state->needsSort = true;
        }
    }
}

DUCKER_API void DuckerNative_SetObjectUniform(uint32_t objectId, const char* name, UniformType type, const void* data) {
    RenderObject* obj = FindObject(objectId);
    if (obj != nullptr) {
        UniformValue val;
        val.type = type;
        size_t size = 0;

        switch (type) {
            case UniformType::UNIFORM_FLOAT: size = sizeof(float); break;
            case UniformType::UNIFORM_VEC2:  size = sizeof(Vec2);  break;
            case UniformType::UNIFORM_VEC3:  size = sizeof(Vec3);  break;
            case UniformType::UNIFORM_VEC4:  size = sizeof(Vec4);  break;
            case UniformType::UNIFORM_INT:   size = sizeof(int);   break;
        }
        
        if (size > 0) {
            val.data.resize(size);
            memcpy(val.data.data(), data, size);
            obj->uniforms[name] = std::move(val);
        }
    }
}

DUCKER_API void DuckerNative_SetObjectBorder(uint32_t objectId, float borderWidth, Vec4 borderColor) {
    RenderObject* obj = FindObject(objectId);
    if (obj != nullptr) {
        obj->borderWidth = borderWidth;
        obj->borderColor = borderColor;

        obj->uniforms["borderWidth"] = { UniformType::UNIFORM_FLOAT };
        UniformValue& bwVal = obj->uniforms["borderWidth"];
        bwVal.data.resize(sizeof(float));
        memcpy(bwVal.data.data(), &borderWidth, sizeof(float));

        obj->uniforms["borderColor"] = { UniformType::UNIFORM_VEC4 };
        UniformValue& bcVal = obj->uniforms["borderColor"];
        bcVal.data.resize(sizeof(Vec4));
        memcpy(bcVal.data.data(), &borderColor, sizeof(Vec4));
    }
}

DUCKER_API void DuckerNative_BeginContainer(RectF bounds) {
    if (state == nullptr) return;
    
    Vec2 offset = {0.0f, 0.0f};
    if(!state->containerStack.empty())
        offset = state->containerStack.back();
    
    RectF newScissor = {bounds.x + offset.x, bounds.y + offset.y, bounds.w, bounds.h};
    if (!state->scissorStack.empty()) {
        const RectF& parentScissor = state->scissorStack.back();
        float r1 = newScissor.x + newScissor.w;
        float r2 = parentScissor.x + parentScissor.w;
        float b1 = newScissor.y + newScissor.h;
        float b2 = parentScissor.y + parentScissor.h;

        newScissor.x = std::max(newScissor.x, parentScissor.x);
        newScissor.y = std::max(newScissor.y, parentScissor.y);
        newScissor.w = std::min(r1, r2) - newScissor.x;
        newScissor.h = std::min(b1, b2) - newScissor.y;
    }

    newScissor.w = std::max(0.0f, newScissor.w);
    newScissor.h = std::max(0.0f, newScissor.h);

    Vec2 newOffset = {bounds.x + offset.x, bounds.y + offset.y};
    state->containerStack.push_back(newOffset);
    state->scissorStack.push_back(newScissor);
}

DUCKER_API void DuckerNative_EndContainer() {
    if (state == nullptr || state->containerStack.empty())  {
        return;
    }

    state->containerStack.pop_back();
    state->scissorStack.pop_back();
}

DUCKER_API void DuckerNative_Render() {
    if (state == nullptr || state->objects.empty())
        return;
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_SCISSOR_TEST);

    if (state->needsSort) {
        std::sort(state->objects.begin(), state->objects.end(), [](const RenderObject& a, const RenderObject& b) {
            if (a.zIndex != b.zIndex) {
                return a.zIndex < b.zIndex;
            }
            
            uint32_t shader_a = a.shaderId != 0 ? a.shaderId : (a.type == ObjectType::Line ? 5 : static_cast<uint32_t>(a.type) + 1);
            uint32_t shader_b = b.shaderId != 0 ? b.shaderId : (b.type == ObjectType::Line ? 5 : static_cast<uint32_t>(b.type) + 1);
            
            if (shader_a != shader_b)
                return shader_a < shader_b;
            
            if (a.textureId != b.textureId)
                return a.textureId < b.textureId;
            
            if (a.type == ObjectType::Line && b.type == ObjectType::Line) {
                if (a.lineMode != b.lineMode)
                    return a.lineMode < b.lineMode;
                
                if (a.lineWidth != b.lineWidth)
                    return a.lineWidth < b.lineWidth;
            }
            
            return memcmp(&a.scissorRect, &b.scissorRect, sizeof(RectF)) < 0;
        });
    
        for (size_t i = 0; i < state->objects.size(); ++i) {
            state->objectIdToIndex[state->objects[i].id] = i;
        }
    
        state->needsSort = false;
    }

    std::map<float, fast_vector<RenderObject>> blurGroups;

    for (const auto& obj : state->objects) {
        if (obj.elevation <= 0 || !obj.visible || (obj.type != ObjectType::RoundedRect && obj.type != ObjectType::Circle && obj.type != ObjectType::Rect)) continue;

        auto presetIt = state->shadowPresets.find(obj.elevation);
        if (presetIt == state->shadowPresets.end()) continue;

        for (const auto& layer : presetIt->second) {
            RenderObject shadowObj = obj;
            shadowObj.color = {0.0f, 0.0f, 0.0f, layer.opacity};
            shadowObj.bounds.y += layer.yOffset;

            float s = layer.spread;
            shadowObj.bounds.x -= s;
            shadowObj.bounds.y -= s;
            shadowObj.bounds.w += 2 * s;
            shadowObj.bounds.h += 2 * s;

            shadowObj.uniforms["quadSize"] = { UniformType::UNIFORM_VEC2 };
            UniformValue& qsVal = shadowObj.uniforms["quadSize"];
            qsVal.data.resize(sizeof(Vec2));
            Vec2 quadSize = {shadowObj.bounds.w, shadowObj.bounds.h};
            memcpy(qsVal.data.data(), &quadSize, sizeof(Vec2));

            if (shadowObj.type == ObjectType::RoundedRect) {
                Vec2 shapeSize;
                memcpy(&shapeSize, shadowObj.uniforms["shapeSize"].data.data(), sizeof(Vec2));
                shapeSize.x += 2 * s;
                shapeSize.y += 2 * s;
                memcpy(shadowObj.uniforms["shapeSize"].data.data(), &shapeSize, sizeof(Vec2));

                float cornerRadius;
                memcpy(&cornerRadius, shadowObj.uniforms["cornerRadius"].data.data(), sizeof(float));
                cornerRadius += s;
                memcpy(shadowObj.uniforms["cornerRadius"].data.data(), &cornerRadius, sizeof(float));
            } else if (shadowObj.type == ObjectType::Circle) {
                float shapeRadius;
                memcpy(&shapeRadius, shadowObj.uniforms["shapeRadius"].data.data(), sizeof(float));
                shapeRadius += s;
                memcpy(shadowObj.uniforms["shapeRadius"].data.data(), &shapeRadius, sizeof(float));
            }

            shadowObj.textureId = 0;
            shadowObj.borderWidth = 0.0f;
            shadowObj.shaderId = 0;
            shadowObj.scissorRect = {0.0f, 0.0f, static_cast<float>(state->screenWidth), static_cast<float>(state->screenHeight)}; // Полный экран для теней

            blurGroups[layer.blurRadius].push_back(shadowObj);
        }
    }

    // Рендерим тени
    for (const auto& groupPair : blurGroups) {
        float blurRadius = groupPair.first;
        const fast_vector<RenderObject>& group = groupPair.second;

        RenderObjects(group, state->shadowFBO);
        ApplyGaussianBlurAndComposite(blurRadius);
    }

    RenderObjects(state->objects, 0);

    glDisable(GL_SCISSOR_TEST);
}