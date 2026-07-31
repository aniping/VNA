vna_add_test(
    vna_board_contract_tests
    board_contract_tests.cpp
    vna::board_mock
)

vna_add_test(
    vna_runtime_contract_tests
    runtime_contract_tests.cpp
    vna::runtime
)

vna_add_test(
    vna_store_contract_tests
    store_contract_tests.cpp
    vna::runtime
)

vna_add_test(
    vna_acquisition_contract_tests
    acquisition_contract_tests.cpp
    vna::runtime
)
