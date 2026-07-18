add_library(VnaBoardMock STATIC
    mock_board.h
    mock_board.cpp
)
add_library(vna::board_mock ALIAS VnaBoardMock)

target_include_directories(VnaBoardMock PUBLIC
    $<BUILD_INTERFACE:${VNA_SOURCE_DIR}>
)
target_link_libraries(VnaBoardMock PUBLIC vna::runtime)
vna_configure_target(VnaBoardMock)
