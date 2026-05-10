set(LIVE2D_COMPONENT_ROOT ${CMAKE_CURRENT_LIST_DIR}/vendor)
set(LIVE2D_ROOT ${LIVE2D_COMPONENT_ROOT}/live2d)
set(LIVE2D_CORE_DIR ${LIVE2D_ROOT}/Core)
set(LIVE2D_FRAMEWORK_DIR ${LIVE2D_ROOT}/Framework)
set(LIVE2D_SAMPLE_COMMON_DIR ${LIVE2D_ROOT}/Samples/Common)
set(LIVE2D_SAMPLE_DEMO_DIR ${LIVE2D_ROOT}/Samples/PortableGL/Demo/src)
set(STB_DIR ${LIVE2D_ROOT}/Samples/OpenGL/thirdParty/stb)
set(LIVE2D_CORE_PREBUILT_LIB ${LIVE2D_CORE_DIR}/lib/libLive2DCubismCore.a)

file(GLOB LIVE2D_FRAMEWORK_SOURCES
    ${LIVE2D_FRAMEWORK_DIR}/src/*.cpp
    ${LIVE2D_FRAMEWORK_DIR}/src/Effect/*.cpp
    ${LIVE2D_FRAMEWORK_DIR}/src/Id/*.cpp
    ${LIVE2D_FRAMEWORK_DIR}/src/Math/*.cpp
    ${LIVE2D_FRAMEWORK_DIR}/src/Model/*.cpp
    ${LIVE2D_FRAMEWORK_DIR}/src/Motion/*.cpp
    ${LIVE2D_FRAMEWORK_DIR}/src/Physics/*.cpp
    ${LIVE2D_FRAMEWORK_DIR}/src/Rendering/*.cpp
    ${LIVE2D_FRAMEWORK_DIR}/src/Rendering/PortableGL/*.cpp
    ${LIVE2D_FRAMEWORK_DIR}/src/Type/*.cpp
    ${LIVE2D_FRAMEWORK_DIR}/src/Utils/*.cpp
)

set(LIVE2D_SAMPLE_COMMON_SOURCES
    ${LIVE2D_SAMPLE_COMMON_DIR}/LAppAllocator_Common.cpp
    ${LIVE2D_SAMPLE_COMMON_DIR}/LAppModel_Common.cpp
    ${LIVE2D_SAMPLE_COMMON_DIR}/LAppSprite_Common.cpp
    ${LIVE2D_SAMPLE_COMMON_DIR}/LAppTextureManager_Common.cpp
    ${LIVE2D_SAMPLE_COMMON_DIR}/LAppView_Common.cpp
    ${LIVE2D_SAMPLE_COMMON_DIR}/LAppWavFileHandler_Common.cpp
    ${LIVE2D_SAMPLE_COMMON_DIR}/TouchManager_Common.cpp
)

set(LIVE2D_SAMPLE_DEMO_SOURCES
    ${LIVE2D_SAMPLE_DEMO_DIR}/LAppAssetProvider.cpp
    ${LIVE2D_SAMPLE_DEMO_DIR}/LAppDefine.cpp
    ${LIVE2D_SAMPLE_DEMO_DIR}/LAppDelegate.cpp
    ${LIVE2D_SAMPLE_DEMO_DIR}/LAppLive2DManager.cpp
    ${LIVE2D_SAMPLE_DEMO_DIR}/LAppModel.cpp
    ${LIVE2D_SAMPLE_DEMO_DIR}/LAppPal.cpp
    ${LIVE2D_SAMPLE_DEMO_DIR}/LAppPerformanceMonitor.cpp
    ${LIVE2D_SAMPLE_DEMO_DIR}/LAppSprite.cpp
    ${LIVE2D_SAMPLE_DEMO_DIR}/LAppSpriteShader.cpp
    ${LIVE2D_SAMPLE_DEMO_DIR}/LAppTextureManager.cpp
    ${LIVE2D_SAMPLE_DEMO_DIR}/LAppView.cpp
    ${LIVE2D_SAMPLE_DEMO_DIR}/LAppX11Window.cpp
)

idf_component_register(
    SRCS
        ${CMAKE_CURRENT_LIST_DIR}/live2d_pgl_app.cpp
        ${CMAKE_CURRENT_LIST_DIR}/pgl_esp32s3_f32_simd.S
        ${LIVE2D_FRAMEWORK_SOURCES}
        ${LIVE2D_SAMPLE_COMMON_SOURCES}
        ${LIVE2D_SAMPLE_DEMO_SOURCES}
    INCLUDE_DIRS
        include
        ${LIVE2D_CORE_DIR}/include
        ${LIVE2D_FRAMEWORK_DIR}/src
        ${LIVE2D_SAMPLE_COMMON_DIR}
        ${LIVE2D_SAMPLE_DEMO_DIR}
        ${STB_DIR}
    PRIV_INCLUDE_DIRS
        ${CMAKE_CURRENT_LIST_DIR}
    REQUIRES
        esp_timer
        heap
)

if(NOT EXISTS ${LIVE2D_CORE_PREBUILT_LIB})
    message(FATAL_ERROR "Missing prebuilt Cubism Core library: ${LIVE2D_CORE_PREBUILT_LIB}")
endif()

add_library(Live2DCubismCore STATIC IMPORTED)
set_target_properties(Live2DCubismCore PROPERTIES
    IMPORTED_LOCATION ${LIVE2D_CORE_PREBUILT_LIB}
    INTERFACE_INCLUDE_DIRECTORIES ${LIVE2D_CORE_DIR}/include
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PRIVATE CSM_TARGET_ESP_PGL)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=missing-field-initializers
    -Wno-error=unknown-pragmas
)
target_link_libraries(${COMPONENT_LIB} PRIVATE m)
target_link_libraries(${COMPONENT_LIB} PRIVATE Live2DCubismCore)

set(BIG_SQUIRREL_RESOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../live2d/Samples/Resources/A_full)

target_add_binary_data(${COMPONENT_LIB}
    ${BIG_SQUIRREL_RESOURCE_DIR}/大松鼠.moc3
    BINARY
    RENAME_TO big_squirrel_moc3)
target_add_binary_data(${COMPONENT_LIB}
    ${BIG_SQUIRREL_RESOURCE_DIR}/大松鼠.cdi3.json
    BINARY
    RENAME_TO big_squirrel_cdi3_json)
target_add_binary_data(${COMPONENT_LIB}
    ${BIG_SQUIRREL_RESOURCE_DIR}/大松鼠.512/texture_00.png
    BINARY
    RENAME_TO big_squirrel_texture_00_png)
