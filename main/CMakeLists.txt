idf_component_register(
    SRCS
        main.cpp
    INCLUDE_DIRS
        .
    REQUIRES
        live2d_pgl
        display
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
