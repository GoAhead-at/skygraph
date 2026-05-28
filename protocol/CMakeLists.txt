add_library(skygraph_protocol INTERFACE)
add_library(skygraph::protocol ALIAS skygraph_protocol)

target_include_directories(skygraph_protocol
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(skygraph_protocol INTERFACE cxx_std_23)
